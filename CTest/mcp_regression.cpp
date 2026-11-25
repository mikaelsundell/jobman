// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "mcp_regression.h"
#include "jobman.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QSettings>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <future>
#include <stdexcept>

namespace {
void
require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct HttpResult {
    int status;
    QByteArray body;
};

class Client {
public:
    explicit Client(quint16 port)
        : port(port)
    {}

    HttpResult http(const QByteArray& body, const QByteArray& extraHeaders = {}, const QByteArray& path = "/mcp",
                    const QByteArray& hostOverride = {}, const QByteArray& lengthOverride = {})
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        require(socket.waitForConnected(3000), "MCP connection failed");
        const QByteArray host = hostOverride.isEmpty() ? "127.0.0.1:" + QByteArray::number(port) : hostOverride;
        const QByteArray length = lengthOverride.isEmpty() ? QByteArray::number(body.size()) : lengthOverride;
        const QByteArray request = "POST " + path + " HTTP/1.1\r\nHost: " + host
                                   + "\r\nContent-Type: application/json\r\nContent-Length: " + length + "\r\n"
                                   + extraHeaders + "\r\n" + body;
        // Split every request to exercise incremental HTTP header/body capture.
        require(socket.write(request.left(11)) == 11, "Could not send MCP request header");
        socket.waitForBytesWritten(3000);
        require(socket.write(request.mid(11)) == request.size() - 11, "Could not send MCP request body");
        socket.waitForBytesWritten(3000);
        QByteArray response;
        QElapsedTimer elapsed;
        elapsed.start();
        while (socket.state() != QAbstractSocket::UnconnectedState) {
            response += socket.readAll();
            if (!socket.waitForReadyRead(3000) && socket.state() != QAbstractSocket::UnconnectedState)
                require(elapsed.elapsed() < 10000, "MCP response timed out");
        }
        response += socket.readAll();
        const qsizetype split = response.indexOf("\r\n\r\n");
        require(split >= 0, "MCP response had no HTTP headers");
        const auto statusLine = response.left(response.indexOf("\r\n")).split(' ');
        require(statusLine.size() >= 2, "MCP HTTP status line was malformed");
        return { statusLine[1].toInt(), response.mid(split + 4) };
    }

    QJsonObject rpc(const QString& method, const QJsonObject& params = {})
    {
        const QString operation = method + ':' + params.value("name").toString();
        if (operation != lastOperation)
            qInfo().noquote() << "MCP request" << operation;
        lastOperation = operation;
        const QJsonObject request { { "jsonrpc", "2.0" }, { "id", ++id }, { "method", method }, { "params", params } };
        const auto response = http(QJsonDocument(request).toJson(QJsonDocument::Compact));
        require(response.status == 200, "MCP RPC returned an unexpected HTTP status");
        const QJsonObject document = QJsonDocument::fromJson(response.body).object();
        require(document.value("id").toInt() == id, "MCP response id did not match the request");
        return document;
    }

    QJsonObject call(const QString& name, const QJsonObject& arguments = {}, bool expectError = false)
    {
        const QJsonObject response = rpc("tools/call", { { "name", name }, { "arguments", arguments } });
        require(!response.contains("error"), "MCP tool returned an RPC error");
        const QJsonObject result = response.value("result").toObject();
        require(result.value("isError").toBool() == expectError, "MCP tool error status was unexpected");
        return result;
    }

    QJsonValue text(const QString& name, const QJsonObject& arguments = {})
    {
        const QJsonArray content = call(name, arguments).value("content").toArray();
        require(content.size() == 1, "MCP text result had an unexpected content count");
        const QJsonDocument document = QJsonDocument::fromJson(content[0].toObject().value("text").toString().toUtf8());
        require(!document.isNull(), "MCP tool result was not valid JSON");
        return document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
    }

    quint16 port;
    int id = 0;
    QString lastOperation;
};

QJsonObject
poll(const std::function<QJsonObject()>& query, const std::function<bool(const QJsonObject&)>& ready,
     int timeoutMilliseconds = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMilliseconds) {
        const QJsonObject value = query();
        if (ready(value))
            return value;
        QThread::msleep(5);
    }
    throw std::runtime_error("Timed out waiting for an MCP-driven UI/queue operation");
}

QString
findPath(const QJsonObject& object, const QString& name)
{
    if (object.value("objectName").toString() == name)
        return object.value("path").toString();
    for (const QJsonValue& child : object.value("children").toArray()) {
        const QString path = findPath(child.toObject(), name);
        if (!path.isEmpty())
            return path;
    }
    return {};
}
}  // namespace

void
runMcpRegression(Jobman& window, const QString& fixtureDirectory)
{
    auto* enabled = window.findChild<QCheckBox*>("mcpEnabled");
    auto* maximum = window.findChild<QLabel*>("label_5");
    require(enabled && maximum && maximum->text() == "Max jobs", "MCP checkbox or Max jobs label is missing");
    require(!window.isMcpServerRunning() && !enabled->isChecked(), "MCP unexpectedly started automatically");
    auto* help = window.findChild<QMenu*>("menuHelp");
    require(help, "Help menu is missing");
    const auto actions = help->actions();
    require(actions.size() >= 3 && actions.last()->objectName() == "helpMcpServer"
                && actions.last()->text() == "MCP Server" && actions[actions.size() - 2]->isSeparator()
                && actions[actions.size() - 3]->objectName() == "helpOpenGithubIssues",
            "Help menu must end with GitHub Issues, a divider, and MCP Server documentation");
    require(!window.findChild<QAction*>("helpToggleMcpServer") && !window.findChild<QAction*>("helpCopyMcpEndpoint"),
            "Extra MCP server controls remained in Help");
    require(window.startMcpServer(0), "Could not start the MCP server on an available port");
    require(enabled->isChecked() && enabled->toolTip().contains(window.mcpServerUrl().toString()),
            "MCP checkbox did not follow actual server state");
    const quint16 port = static_cast<quint16>(window.mcpServerUrl().port());
    const QString accepted = fixtureDirectory + "/mcpinput.png";
    const QString second = fixtureDirectory + "/mcpsecond.png";
    const QString rejected = fixtureDirectory + "/mcpinput.avif";
    for (const QString& path : { accepted, second, rejected }) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write("synthetic input") > 0, "Could not create MCP input");
    }
    auto work = std::async(std::launch::async, [=]() {
        Client client(port);
        const auto initialized = client.rpc("initialize", { { "protocolVersion", "2025-11-25" },
                                                            { "capabilities", QJsonObject() },
                                                            { "clientInfo", QJsonObject { { "name", "Jobman CTest" },
                                                                                          { "version", "1" } } } });
        require(initialized.value("result").toObject().value("serverInfo").toObject().value("name") == "Jobman",
                "MCP initialize did not identify Jobman");
        require(client.http("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}").status == 202,
                "MCP notification was not accepted");
        require(client.rpc("tools/list").value("result").toObject().value("tools").toArray().size() >= 14,
                "MCP tools were not registered");
        require(client.text("jobman_get_status").toObject().value("readyForFiles").toBool(),
                "MCP reported UI not ready");
        require(client.text("jobman_list_presets").toArray().size() == 1,
                "MCP preset list differs from the application");
        client.call("jobman_select_preset", { { "id", "filedrop-regression" } });
        client.call("jobman_select_preset", { { "id", "missing" } }, true);
        client.call("jobman_set_output_directory", { { "path", fixtureDirectory + "/out" } });
        client.call("jobman_set_output_directory", { { "path", "relative/missing" } }, true);
        client.call("jobman_submit_files", { { "paths", 42 } }, true);
        client.call("jobman_get_status", { { "unexpected", true } }, true);
        client.call("unknown_tool", {}, true);
        require(client.rpc("unknown_method").value("error").toObject().value("code").toInt() == -32601,
                "Unknown MCP method did not return the JSON-RPC error");
        const QJsonObject snapshot = client.text("jobman_get_qt_state", { { "depth", 12 }, { "limit", 1000 } })
                                         .toObject()
                                         .value("root")
                                         .toObject();
        const QString checkbox = findPath(snapshot, "copyOriginal");
        const QString docs = findPath(snapshot, "helpMcpServer");
        require(!checkbox.isEmpty() && !docs.isEmpty(),
                "MCP Qt snapshot omitted controls or the Help documentation link");
        require(client.text("jobman_get_qt_state", { { "path", docs }, { "depth", 0 } })
                    .toObject()
                    .value("root")
                    .toObject()
                    .value("text")
                    .toString()
                    .contains("MCP"),
                "Help MCP documentation action missing");
        client.call("jobman_qt_action", { { "path", checkbox }, { "action", "set_checked" }, { "value", true } });
        require(client.text("jobman_get_qt_state", { { "path", checkbox }, { "depth", 0 } })
                    .toObject()
                    .value("root")
                    .toObject()
                    .value("checked")
                    .toBool(),
                "MCP Qt checkbox action did not reach the UI");
        client.call("jobman_qt_action", { { "path", checkbox }, { "action", "set_checked" }, { "value", false } });
        client.call("jobman_qt_action", { { "path", "Jobman/not-a-widget" }, { "action", "click" } }, true);
        client.call("jobman_submit_files", { { "paths", QJsonArray { rejected } } });
        const QJsonObject dialog = poll([&]() { return client.text("jobman_get_dialog").toObject(); },
                                        [](const QJsonObject& value) { return value.value("active").toBool(); });
        require(dialog.value("title") == "Files Skipped", "Rejected MCP submission did not show the normal dialog");
        client.call("jobman_respond_dialog", { { "response", "close" } });
        poll([&]() { return client.text("jobman_get_status").toObject(); },
             [](const QJsonObject& value) { return value.value("readyForFiles").toBool(); });
        require(client.text("jobman_list_jobs").toObject().value("total").toInt() == 0,
                "Rejected MCP file entered the queue");
        const QJsonObject image = client.call("jobman_capture_widget").value("content").toArray().first().toObject();
        require(
            image.value("type") == "image"
                && !QImage::fromData(QByteArray::fromBase64(image.value("data").toString().toLatin1()), "PNG").isNull(),
            "MCP screenshot was not a valid PNG");
        client.call("jobman_submit_files", { { "paths", QJsonArray { accepted } } });
        const QJsonObject listing = poll([&]() { return client.text("jobman_list_jobs").toObject(); },
                                         [](const QJsonObject& value) { return value.value("total").toInt() == 1; });
        const QString uuid = listing.value("jobs").toArray().first().toObject().value("uuid").toString();
        const QJsonObject job = poll(
            [&]() {
                return client.text("jobman_get_job", { { "uuid", uuid } }).toObject();
            },
            [](const QJsonObject& value) { return value.value("status") == "Completed"; });
        poll([&]() { return client.text("jobman_get_status").toObject(); },
             [](const QJsonObject& value) { return !value.value("processing").toBool(); });
        const QJsonObject finished = client.text("jobman_get_job", { { "uuid", uuid } }).toObject();
        require(finished.value("log").toString().contains("stdout:mcpinput"), "MCP job log lost the captured output");
        QFile artifact(fixtureDirectory + "/out/mcpinput.out");
        require(artifact.open(QIODevice::ReadOnly) && artifact.readAll() == "artifact:mcpinput\n",
                "MCP job produced a different result");
        client.call("jobman_control_job", { { "uuid", uuid }, { "action", "restart" } });
        poll(
            [&]() {
                return client.text("jobman_get_job", { { "uuid", uuid } }).toObject();
            },
            [&](const QJsonObject& value) {
                return value.value("status") == "Completed" && value.value("pid") != job.value("pid");
            });
        poll([&]() { return client.text("jobman_get_status").toObject(); },
             [](const QJsonObject& value) { return !value.value("processing").toBool(); });
        client.call("jobman_control_job", { { "uuid", uuid }, { "action", "remove" } });
        poll([&]() { return client.text("jobman_list_jobs").toObject(); },
             [](const QJsonObject& value) { return value.value("total").toInt() == 0; });
        client.call("jobman_get_job", { { "uuid", uuid } }, true);
        poll([&]() { return client.text("jobman_get_status").toObject(); },
             [](const QJsonObject& value) { return value.value("readyForFiles").toBool(); });
        client.call("jobman_submit_files", { { "paths", QJsonArray { accepted, second } } });
        const QJsonObject multiple = poll([&]() { return client.text("jobman_list_jobs").toObject(); },
                                          [](const QJsonObject& value) { return value.value("total").toInt() == 2; });
        for (const QJsonValue& value : multiple.value("jobs").toArray()) {
            const QString added = value.toObject().value("uuid").toString();
            poll(
                [&]() {
                    return client.text("jobman_get_job", { { "uuid", added } }).toObject();
                },
                [](const QJsonObject& value) { return value.value("status") == "Completed"; });
        }
        poll([&]() { return client.text("jobman_get_status").toObject(); },
             [](const QJsonObject& value) { return !value.value("processing").toBool(); });
        for (const QString& name : { QString("mcpinput"), QString("mcpsecond") }) {
            QFile result(fixtureDirectory + "/out/" + name + ".out");
            require(result.open(QIODevice::ReadOnly) && result.readAll() == "artifact:" + name.toUtf8() + '\n',
                    "Multi-file MCP submission produced a different result");
        }
        for (const QJsonValue& value : multiple.value("jobs").toArray())
            client.call("jobman_control_job", { { "uuid", value.toObject().value("uuid") }, { "action", "remove" } });
        client.call("jobman_save_preferences");
        require(client.http("{}", {}, "/wrong").status == 404, "Invalid endpoint was accepted");
        require(client.http("{}", {}, "/mcp", "evil.example").status == 403, "Non-local Host was accepted");
        require(client.http("{}", "Origin: https://evil.example\r\n").status == 403, "Non-local Origin was accepted");
        require(client.http("{").status == 400, "Malformed JSON was accepted");
        require(client.http("{}", {}, "/mcp", {}, "1048577").status == 413, "Oversized request was accepted");
        require(client.http("{}", "Content-Length: 2\r\n").status == 400, "Duplicate framing header was accepted");
    });
    QElapsedTimer timeout;
    timeout.start();
    QTimer dialogWatchdog;
    QObject::connect(&dialogWatchdog, &QTimer::timeout, &window, [&]() {
        if (work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready || timeout.elapsed() > 20000) {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                dialog->reject();
        }
    });
    dialogWatchdog.start(20);
    while (work.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        require(timeout.elapsed() < 25000, "MCP external controller did not finish");
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    dialogWatchdog.stop();
    work.get();
    enabled->setChecked(false);
    require(!window.isMcpServerRunning() && !enabled->isChecked(), "MCP checkbox did not stop the server");
    enabled->setChecked(true);
    require(window.isMcpServerRunning() && enabled->isChecked(), "MCP checkbox did not restart the server");
    const auto restartProcess = [&](const QString& mode) {
        QSettings(APP_IDENTIFIER, APP_NAME).sync();
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("JOBMAN_TEST_SETTINGS_ID", QString::number(QCoreApplication::applicationPid()));
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), { mode });
        require(process.waitForStarted(5000), "Could not start MCP persistence test process");
        QElapsedTimer timeout;
        timeout.start();
        while (process.state() != QProcess::NotRunning && timeout.elapsed() < 10000) {
            process.waitForFinished(20);
            QCoreApplication::processEvents();
        }
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished();
        }
        const QByteArray output = process.readAllStandardError();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            qCritical().noquote() << output;
        require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                "MCP preference restoration failed in a fresh application process");
    };
    restartProcess("mcp_restore_on");
    require(QSettings(APP_IDENTIFIER, APP_NAME).value("mcpEnabled").toBool(),
            "Closing the application erased the saved MCP switch");
    restartProcess("mcp_restore_override");
    QSettings(APP_IDENTIFIER, APP_NAME).setValue("mcpPort", window.mcpServerUrl().port());
    restartProcess("mcp_restore_failed");
    window.stopMcpServer();
    require(!enabled->isChecked(), "MCP checkbox did not follow server shutdown");
    restartProcess("mcp_restore_off");
}

void
runMcpLargeRegression(Jobman& window, const QString& fixtureDirectory)
{
    constexpr int fileCount = 2001;
    const QString directory = fixtureDirectory + "/many-inputs";
    QJsonArray explicitPaths;
    for (int i = 0; i < fileCount; ++i) {
        const QString parent = directory + QString("/group%1").arg(i % 10);
        require(QDir().mkpath(parent), "Could not create large folder fixture");
        const QString path = parent + QString("/input%1.%2").arg(i).arg(i % 7 ? "png" : "PNG");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write("synthetic input") > 0, "Could not create large input");
        if (i < 1001)
            explicitPaths.append(path);
    }
    require(window.startMcpServer(0), "Could not start MCP for large submission test");
    const auto port = static_cast<quint16>(window.mcpServerUrl().port());
    auto work = std::async(std::launch::async, [=]() {
        Client client(port);
        client.call("jobman_submit_files", { { "paths", QJsonArray() } }, true);
        client.call("jobman_list_jobs", { { "limit", 0 } }, true);
        client.call("jobman_list_jobs", { { "limit", 1.5 } }, true);
        client.call("jobman_list_jobs", { { "limit", 1e100 } }, true);
        client.call("jobman_list_jobs", { { "cursor", "invalid-uuid" } }, true);
        const auto submitFolder = [&]() {
            client.call("jobman_submit_files", { { "paths", QJsonArray { directory } } });
            const QJsonObject dialog = poll([&]() { return client.text("jobman_get_dialog").toObject(); },
                                            [](const QJsonObject& value) { return value.value("active").toBool(); });
            require(dialog.value("text").toString().contains(QString::number(fileCount)),
                    "Folder confirmation did not include every input");
        };
        submitFolder();
        client.call("jobman_respond_dialog", { { "response", "no" } });
        poll([&]() { return client.text("jobman_get_status").toObject(); },
             [](const QJsonObject& value) { return value.value("readyForFiles").toBool(); });
        require(client.text("jobman_list_jobs").toObject().value("total").toInt() == 0,
                "Declined large folder entered the queue");
        submitFolder();
        client.call("jobman_respond_dialog", { { "response", "yes" } });
        const auto waitForJobs = [&](qsizetype expected) {
            poll([&]() { return client.text("jobman_get_status").toObject(); },
                 [=](const QJsonObject& value) {
                     return value.value("readyForFiles").toBool() && !value.value("processing").toBool()
                            && value.value("jobCount").toInt() == expected;
                 },
                 60000);
        };
        waitForJobs(fileCount);
        QSet<QString> uuids;
        QSet<QString> filenames;
        QString cursor;
        do {
            const QJsonObject page
                = client.text("jobman_list_jobs", { { "limit", 127 }, { "cursor", cursor } }).toObject();
            const QJsonArray jobs = page.value("jobs").toArray();
            require(!jobs.isEmpty() && jobs.size() <= 127 && page.value("total").toInt() == fileCount,
                    "Large queue page had an incorrect size or total");
            for (const QJsonValue& value : jobs) {
                const auto job = value.toObject();
                const QString uuid = job.value("uuid").toString();
                require(!uuids.contains(uuid) && job.value("status") == "Failed",
                        "Large folder duplicated a UUID or lost its expected launch failure");
                uuids.insert(uuid);
                filenames.insert(job.value("filename").toString());
            }
            cursor = page.value("nextCursor").toString();
            require(page.value("truncated").toBool() == !cursor.isEmpty(), "Pagination completion flags disagreed");
        } while (!cursor.isEmpty());
        require(uuids.size() == fileCount && filenames.size() == fileCount,
                "Pagination omitted jobs or folder scanning omitted inputs");
        const QJsonObject first = client.text("jobman_list_jobs", { { "limit", 1 } }).toObject();
        cursor = first.value("nextCursor").toString();
        const QJsonObject next = client.text("jobman_list_jobs", { { "limit", 1 }, { "cursor", cursor } }).toObject();
        client.call("jobman_control_job", { { "uuid", cursor }, { "action", "remove" } });
        poll([&]() { return client.text("jobman_get_status").toObject(); },
             [](const QJsonObject& value) { return value.value("jobCount").toInt() == fileCount - 1; });
        require(client.text("jobman_list_jobs", { { "limit", 1 }, { "cursor", cursor } }).toObject().value("jobs")
                    == next.value("jobs"),
                "Deleting the cursor job broke pagination");
        client.call("jobman_submit_files", { { "paths", explicitPaths } });
        waitForJobs(fileCount - 1 + explicitPaths.size());
        const QJsonObject all = client.text("jobman_list_jobs", { { "limit", 10000 } }).toObject();
        require(all.value("jobs").toArray().size() == fileCount - 1 + explicitPaths.size()
                    && all.value("nextCursor").toString().isEmpty() && !all.value("truncated").toBool(),
                "Explicit submission or an uncapped listing retained the old job limits");
    });
    QElapsedTimer timeout;
    timeout.start();
    while (work.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        require(timeout.elapsed() < 90000, "Large MCP regression timed out");
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    work.get();
    window.stopMcpServer();
    qInfo()
        << "PASS large folder acceptance/decline, 1001 explicit paths, complete pagination, cursor removal, and failure results";
}
