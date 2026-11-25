// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "filedrop.h"
#include "jobman.h"
#include "mcp_regression.h"
#include "message.h"
#include "processor.h"
#include "question.h"
#include "queue.h"
#include "theme_regression.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMimeData>
#include <QProgressBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>

#include <functional>
#include <stdexcept>

namespace {
void
require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void
waitFor(const std::function<bool()>& condition, const char* failure)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        require(timer.elapsed() < 5000, failure);
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}

void
writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size(), "Could not write fixture");
}

void
dropFiles(Filedrop* drop, const QStringList& files)
{
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString& file : files)
        urls.append(QUrl::fromLocalFile(file));
    mime.setUrls(urls);
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(drop, &enter);
    require(enter.isAccepted(), "Filedrop rejected the drag event");
    QDropEvent event(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(drop, &event);
    require(event.isAccepted(), "Filedrop rejected the drop event");
}

struct SettingsCleanup {
    ~SettingsCleanup()
    {
        QSettings settings(APP_IDENTIFIER, APP_NAME);
        settings.clear();
        settings.sync();
    }
};

void
checkPresetExecution(Jobman& window, const QString& directory, const QString& child)
{
    auto* processor = window.findChild<Processor*>();
    require(processor, "Processor was not found");
    Queue* queue = Queue::instance();
    QList<QSharedPointer<Job>> observed;
    const auto remember = [&](const QList<QSharedPointer<Job>>& jobs) { observed.append(jobs); };
    const auto submitted = QObject::connect(queue, &Queue::jobsSubmitted, &window, remember);
    const auto batch = QObject::connect(queue, &Queue::batchSubmitted, &window, remember);
    Paths paths { true, false, false, directory, directory + "/out" };
    const QString input = directory + "/input with spaces.png";
    writeFile(input, "synthetic input");
    for (const QString& type : { QString("file"), QString("command") }) {
        QJsonArray tasks;
        for (int i = 1; i <= 3; ++i) {
            const QString id = QString("0%1").arg(i);
            const QString tag = i == 1 ? "%options:tag%" : QString("task%1").arg(i);
            const QString prerequisite = i == 1 ? "none" : "%task:input%";
            QJsonObject task { { "id", id },
                               { "name", "Task " + id },
                               { "command", child },
                               { "extension", "txt" },
                               { "output", paths.outputpath + '/' + type + " output " + id + ".txt" },
                               { "arguments", "job   %task:output%  " + tag + ' ' + prerequisite
                                                  + "   %options:exitcode% 5 %options:unused%" },
                               { "exclusive", "true" } };
            if (i > 1)
                task["dependson"] = QString("0%1").arg(i - 1);
            tasks.append(task);
        }
        const QJsonArray options {
            QJsonObject {
                { "id", "tag" }, { "name", "Tag" }, { "type", "text" }, { "value", "task1" }, { "valueonly", true } },
            QJsonObject { { "id", "exitcode" },
                          { "name", "Exit code" },
                          { "type", "integer" },
                          { "value", 0 },
                          { "default", 0 },
                          { "valueonly", true } },
            QJsonObject { { "id", "unused" },
                          { "name", "Optional" },
                          { "type", "checkbox" },
                          { "value", true },
                          { "flagonly", true },
                          { "flag", "--unused" },
                          { "toggle", "Enable optional flag" } }
        };
        const QJsonObject definition {
            { "id", "chain" }, { "name", "Chain" }, { "type", type }, { "options", options }, { "tasks", tasks }
        };
        const QString filename = directory + "/chain.json";
        writeFile(filename, QJsonDocument(definition).toJson());
        QSharedPointer<Preset> preset(new Preset());
        require(preset->read(filename), qPrintable(preset->error()));
        const auto uuids = type == "file" ? processor->submit({ input }, preset, paths)
                                          : processor->submit(preset, paths);
        require(uuids.size() == 3, "Processor did not submit the complete chain");
        waitFor([&]() { return observed.size() == 3 && !queue->isProcessing(); }, "Preset chain did not finish");
        for (const auto& job : observed) {
            require(job->status() == Job::Completed && job->exclusive(), "Preset chain failed or lost exclusivity");
            require(!job->arguments().contains(QString()) && !job->arguments().join(' ').contains("%task:"),
                    "Preset arguments contained an empty token or unresolved task input");
            QFile output(job->output());
            require(output.open(QIODevice::ReadOnly)
                        && output.readAll() == "artifact:task" + job->id().right(1).toUtf8() + '\n',
                    "Preset chain output differed");
            if (job->id() != "01")
                require(!job->dependson().isNull() && QFileInfo::exists(job->arguments().value(3)),
                        "Chained task did not receive its predecessor's output path");
        }
        queue->remove(uuids.first());
        QCoreApplication::processEvents();
        observed.clear();
    }
    QObject::disconnect(submitted);
    QObject::disconnect(batch);
}
}  // namespace

int
main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try {
        const QString name = application.arguments().value(1);
        if (name.startsWith("mcp_restore_")) {
            Jobman restored;
            int errors = 0;
            QTimer dismissError;
            QObject::connect(&dismissError, &QTimer::timeout, &restored, [&]() {
                if (auto* message = qobject_cast<Message*>(QApplication::activeModalWidget())) {
                    ++errors;
                    message->accept();
                }
            });
            dismissError.start(10);
            QUrl explicitUrl;
            if (name == "mcp_restore_override") {
                require(restored.startMcpServer(0), "Explicit MCP startup failed");
                explicitUrl = restored.mcpServerUrl();
            }
            QCoreApplication::processEvents();
            const bool expected = name != "mcp_restore_off" && name != "mcp_restore_failed";
            require(restored.isMcpServerRunning() == expected
                        && restored.findChild<QCheckBox*>("mcpEnabled")->isChecked() == expected,
                    "MCP switch was not restored in a new process");
            if (!explicitUrl.isEmpty())
                require(restored.mcpServerUrl() == explicitUrl, "Saved startup replaced the explicit endpoint");
            if (name == "mcp_restore_failed") {
                require(errors == 1 && !QSettings(APP_IDENTIFIER, APP_NAME).value("mcpEnabled").toBool(),
                        "Failed restoration left automatic startup enabled");
                restored.findChild<QCheckBox*>("mcpEnabled")->setChecked(true);
                require(errors == 2 && !restored.isMcpServerRunning()
                            && !QSettings(APP_IDENTIFIER, APP_NAME).value("mcpEnabled").toBool(),
                        "Failed manual retry did not keep the switch and preference disabled");
            }
            return 0;
        }
        const QString child = QFileInfo(application.arguments().value(2)).absoluteFilePath();
        QTemporaryDir temporary;
        require(temporary.isValid(), "Could not create file-drop fixtures");
        QDir root(temporary.path());
        require(root.mkdir("presets") && root.mkdir("out") && root.mkdir("inputs"),
                "Could not create fixture directories");
        const QString presets = root.filePath("presets");
        const QString output = root.filePath("out");
        const QJsonObject task { { "id", "convert" },
                                 { "name", "Synthetic conversion" },
                                 { "command", name == "mcp_large" ? root.filePath("missing-executable") : child },
                                 { "extension", "out" },
                                 { "output", "%outputdir%/%outputbase%.%outputext%" },
                                 { "arguments", "job %task:output% %inputbase% none 0 10" } };
        const QJsonObject preset { { "id", "filedrop-regression" },
                                   { "name", "File-drop regression" },
                                   { "filter", name == "filedrop_all_files" ? "*.*" : "*.png" },
                                   { "tasks", QJsonArray { task } } };
        const QString presetFile = QDir(presets).filePath("test.json");
        writeFile(presetFile, QJsonDocument(preset).toJson());
        if (name == "preset_errors_ui") {
            auto invalid = preset;
            invalid["options"]
                = QJsonArray { QJsonObject { { "id", "width" },
                                             { "name", "Width" },
                                             { "type", "dropdown" },
                                             { "value", 400 },
                                             { "default", 999 },
                                             { "options", QJsonArray { QJsonObject { { "value", 400 } } } } } };
            writeFile(QDir(presets).filePath("bad-dropdown.json"), QJsonDocument(invalid).toJson());
            invalid = preset;
            invalid["options"] = QJsonObject();
            writeFile(QDir(presets).filePath("bad-container.json"), QJsonDocument(invalid).toJson());
            writeFile(QDir(presets).filePath("zzz-duplicate.json"), QJsonDocument(preset).toJson());
        }
        // testsettings.h redirects every production QSettings caller to a unique organization.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.filePath("settings"));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root.filePath("system-settings"));
        const SettingsCleanup cleanup;
        QSettings settings(APP_IDENTIFIER, APP_NAME);
        require(settings.organizationName().startsWith("com.github.mikaelsundell.jobman.tests."),
                "Test settings were not isolated");
        settings.setValue("presetsfrom", presets);
        settings.setValue("presetsselected", presetFile);
        settings.setValue("saveto", output);
        settings.setValue("copyoriginal", false);
        settings.setValue("createfolders", false);
        settings.sync();

        QString validationErrors;
        int validationReports = 0;
        QTimer dismissValidation;
        QObject::connect(&dismissValidation, &QTimer::timeout, [&]() {
            if (auto* message = qobject_cast<Message*>(QApplication::activeModalWidget())) {
                ++validationReports;
                validationErrors = message->findChild<QTextBrowser*>("message")->toPlainText();
                message->accept();
            }
        });
        if (name == "preset_errors_ui")
            dismissValidation.start(5);
        Jobman window;
        window.show();
        auto* drop = window.findChild<Filedrop*>("filedrop");
        auto* pages = window.findChild<QStackedWidget*>("presettype");
        auto* status = window.findChild<QLabel*>("filedropLabel");
        auto* progress = window.findChild<QProgressBar*>("filedropProgress");
        auto* presetsBox = window.findChild<QComboBox*>("presets");
        require(drop && pages && status && progress && presetsBox, "Main-window file-drop widgets missing");
        waitFor([&]() { return presetsBox->currentText() == "File-drop regression" && drop->isEnabled(); },
                "Synthetic preset did not load");
        if (name == "preset_errors_ui") {
            dismissValidation.stop();
            require(validationReports == 1 && validationErrors.contains("bad-dropdown.json")
                        && validationErrors.contains(".default") && validationErrors.contains("Allowed values")
                        && validationErrors.contains("bad-container.json")
                        && validationErrors.contains("preset.options")
                        && validationErrors.contains("zzz-duplicate.json")
                        && validationErrors.contains("Duplicate preset id"),
                    "Preset loading did not aggregate actionable errors for every invalid file");
            require(presetsBox->count() == 1 && !Queue::instance()->isProcessing(),
                    "Invalid or duplicate presets were selectable or submitted work");
            return 0;
        }
        if (name == "preset_execution") {
            checkPresetExecution(window, temporary.path(), child);
            return 0;
        }
        if (name == "mcp") {
            runMcpRegression(window, temporary.path());
            require(window.grab().save(application.arguments().value(3)), "Could not capture MCP UI screenshot");
            qInfo()
                << "PASS MCP transport, tools, job execution, Qt inspection/actions/screenshots, dialogs, and checkbox";
            return 0;
        }
        if (name == "mcp_large") {
            runMcpLargeRegression(window, temporary.path());
            return 0;
        }
        if (name == "ui_theme") {
            runUiRegression(window, temporary.path());
            require(window.grab().save(application.arguments().value(3)),
                    "Could not capture the shared-theme screenshot");
            qInfo() << "PASS shared UI theme, standalone views, icon states, DPI caching, and refresh";
            return 0;
        }
        int scans = 0;
        QObject::connect(pages, &QStackedWidget::currentChanged, &window, [&](int page) {
            if (page == 2)
                ++scans;
        });
        QList<QSharedPointer<Job>> submitted;
        Queue* queue = Queue::instance();
        QObject::connect(queue, &Queue::batchSubmitted, &window,
                         [&](const QList<QSharedPointer<Job>>& jobs) { submitted.append(jobs); });
        int skipped = 0;
        int questions = 0;
        bool dialogsValid = true;
        QTimer dialogs;
        QObject::connect(&dialogs, &QTimer::timeout, &window, [&]() {
            if (auto* message = qobject_cast<Message*>(QApplication::activeModalWidget())) {
                auto* title = message->findChild<QLabel*>("title");
                dialogsValid = dialogsValid && title && title->text() == "Files Skipped";
                ++skipped;
                message->accept();
            }
            else if (auto* question = qobject_cast<Question*>(QApplication::activeModalWidget())) {
                ++questions;
                question->reject();
            }
        });
        dialogs.start(1);
        const auto dropAndWait = [&](const QStringList& files) {
            const int before = scans;
            dropFiles(drop, files);
            waitFor([&]() { return scans > before && pages->currentIndex() == 0; },
                    "File drop did not return to the ready state");
            require(drop->isEnabled() && !progress->isVisible(), "File drop controls did not reset");
            require(!status->text().startsWith("Added file:"), "Rejected file was still reported as added");
            require(dialogsValid, "An unexpected error dialog appeared");
        };
        const QString accepted = root.filePath(name == "filedrop_all_files" ? "accepted" : "accepted.PNG");
        const QString rejected = root.filePath("rejected.avif");
        writeFile(accepted, "synthetic input");
        writeFile(rejected, "synthetic input");
        if (name == "filedrop_rejected_recovery") {
            dropAndWait({ rejected });
            require(skipped == 1 && submitted.isEmpty() && !queue->isProcessing(), "Rejected file entered the queue");
            for (const QString& filename : { QString("no-extension"), QString("partial.p") }) {
                const QString path = root.filePath(filename);
                writeFile(path, "synthetic input");
                dropAndWait({ path });
            }
            require(skipped == 3 && submitted.isEmpty(), "Filter accepted an empty or partial extension");
        }
        else if (name == "filedrop_empty_directory") {
            dropAndWait({ root.filePath("inputs") });
            require(submitted.isEmpty(), "Empty directory entered the queue");
        }
        else if (name == "filedrop_declined_directory") {
            for (int i = 0; i < 11; ++i)
                writeFile(root.filePath(QString("inputs/input%1.png").arg(i)), "synthetic input");
            dropAndWait({ root.filePath("inputs") });
            require(questions == 1 && submitted.isEmpty(), "Declined directory entered the queue");
        }
        else if (name != "filedrop_mixed" && name != "filedrop_all_files") {
            throw std::runtime_error("Unknown file-drop regression");
        }
        require(window.grab().save(application.arguments().value(3)), "Could not capture ready-state screenshot");
        // Every rejected/empty/declined case must allow a later accepted drop.
        dropAndWait(name == "filedrop_mixed" ? QStringList { accepted, rejected } : QStringList { accepted });
        waitFor([&]() { return submitted.size() == 1 && !queue->isProcessing(); }, "Accepted drop did not finish");
        require(submitted.first()->filename() == QFileInfo(accepted).absoluteFilePath()
                    && submitted.first()->status() == Job::Completed,
                "Wrong file was submitted, or accepted file did not complete");
        QFile artifact(QDir(output).filePath("accepted.out"));
        const bool opened = artifact.open(QIODevice::ReadOnly);
        const QByteArray contents = opened ? artifact.readAll() : QByteArray();
        if (!opened || contents != "artifact:accepted\n") {
            qCritical() << "Output" << artifact.fileName() << "opened" << opened << "contents" << contents
                        << "job output" << submitted.first()->output() << "log" << submitted.first()->log();
            throw std::runtime_error("Accepted drop output differs");
        }
        if (name == "filedrop_mixed")
            require(skipped == 1, "Mixed drop did not report the rejected file");
        auto* processor = window.findChild<Processor*>();
        require(processor, "Submission processor was not found");
        processor->fileSubmitted(QString(240, QChar('x')) + ".png");
        require(status->text().startsWith("Submitted file: ") && status->text().endsWith(" - 1/1 100%")
                    && status->text().contains(QChar(0x2026))
                    && QFontMetrics(status->font()).horizontalAdvance(status->text()) <= status->width(),
                "Long submitted filename was not elided around the progress suffix");
        qInfo().noquote() << "PASS" << name;
        return 0;
    } catch (const std::exception& error) {
        qCritical().noquote() << "FAIL filedrop" << error.what();
        return 1;
    }
}
