// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// HTTP transport adapted from Stageviz's local MCP server.

#include "mcpserver.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <utility>

namespace {
constexpr qsizetype maxHeaderSize = 32 * 1024;
constexpr qsizetype maxBodySize = 1024 * 1024;
QJsonObject
jsonRpcError(const QJsonValue& id, int code, const QString& message)
{
    return { { "jsonrpc", "2.0" }, { "id", id }, { "error", QJsonObject { { "code", code }, { "message", message } } } };
}
QJsonObject
toolError(const QString& message)
{
    return { { "isError", true },
             { "content", QJsonArray { QJsonObject { { "type", "text" }, { "text", message } } } } };
}
}  // namespace

class McpServerPrivate {
public:
    explicit McpServerPrivate(McpServer* owner);
    void acceptConnections();
    void writeResponse(QTcpSocket* socket, int status, const QByteArray& reason, const QByteArray& body = {},
                       const QByteArray& contentType = "application/json", const QByteArray& corsOrigin = {});
    QByteArray errorBody(int code, const QString& message);
    bool allowedHost(const QByteArray& host) const;
    QByteArray allowedOrigin(const QByteArray& origin) const;
    void readRequest(QTcpSocket* socket);
    QJsonObject handleMessage(const QJsonObject& request, bool* hasResponse);
    QJsonObject callTool(const QString& name, const QJsonObject& args);

    struct Data {
        McpServer* q = nullptr;
        QTcpServer* server = nullptr;
        QString error;
        QJsonArray tools;
        McpServer::ToolHandler handler;
    } d;
};

McpServerPrivate::McpServerPrivate(McpServer* owner)
{
    d.q = owner;
    d.server = new QTcpServer(owner);
    QObject::connect(d.server, &QTcpServer::newConnection, d.q, [this]() { acceptConnections(); });
}

void
McpServerPrivate::acceptConnections()
{
    while (d.server->hasPendingConnections()) {
        QTcpSocket* socket = d.server->nextPendingConnection();
        if (!socket)
            continue;
        socket->setReadBufferSize(maxHeaderSize + maxBodySize);
        QTimer::singleShot(15000, socket, [socket]() { socket->abort(); });
        QObject::connect(socket, &QTcpSocket::readyRead, d.q, [this, socket]() { readRequest(socket); });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void
McpServerPrivate::writeResponse(QTcpSocket* socket, int status, const QByteArray& reason, const QByteArray& body,
                                const QByteArray& contentType, const QByteArray& corsOrigin)
{
    if (!socket)
        return;
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\nConnection: close\r\n";
    if (!corsOrigin.isEmpty()) {
        response += "Access-Control-Allow-Origin: " + corsOrigin + "\r\n";
        response += "Access-Control-Allow-Methods: POST, OPTIONS\r\n";
        response += "Access-Control-Allow-Headers: Content-Type, Accept, Mcp-Protocol-Version\r\n";
        response += "Vary: Origin\r\n";
    }
    response += "\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray
McpServerPrivate::errorBody(int code, const QString& message)
{
    return QJsonDocument(jsonRpcError(QJsonValue(QJsonValue::Null), code, message)).toJson(QJsonDocument::Compact);
}

bool
McpServerPrivate::allowedHost(const QByteArray& host) const
{
    const QByteArray port = QByteArray::number(d.server->serverPort());
    return host.compare("127.0.0.1:" + port, Qt::CaseInsensitive) == 0
           || host.compare("localhost:" + port, Qt::CaseInsensitive) == 0;
}

QByteArray
McpServerPrivate::allowedOrigin(const QByteArray& origin) const
{
    if (origin.isEmpty())
        return {};
    const QUrl url(QString::fromUtf8(origin));
    if (url.scheme() != QLatin1String("http") || url.port(-1) != d.server->serverPort()
        || (url.host() != QLatin1String("127.0.0.1") && url.host() != QLatin1String("localhost")))
        return {};
    return origin;
}

void
McpServerPrivate::readRequest(QTcpSocket* socket)
{
    if (!socket || socket->property("mcpResponded").toBool())
        return;

    QByteArray buffer = socket->property("mcpRequestBuffer").toByteArray();
    buffer += socket->readAll();
    if (buffer.size() > maxHeaderSize + maxBodySize) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 413, "Payload Too Large", "Request body is too large.", "text/plain");
        return;
    }

    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffer.size() > maxHeaderSize) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 431, "Request Header Fields Too Large", "Request headers are too large.",
                          "text/plain");
            return;
        }
        socket->setProperty("mcpRequestBuffer", buffer);
        return;
    }

    if (headerEnd > maxHeaderSize) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 431, "Request Header Fields Too Large", "Request headers are too large.", "text/plain");
        return;
    }
    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    if (lines.isEmpty()) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 400, "Bad Request", "Malformed request.", "text/plain");
        return;
    }
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() != 3) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 400, "Bad Request", "Malformed request line.", "text/plain");
        return;
    }

    QHash<QByteArray, QByteArray> headers;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype separator = line.indexOf(':');
        if (separator <= 0)
            continue;
        const QByteArray name = line.left(separator).trimmed().toLower();
        if (headers.contains(name) && (name == "content-length" || name == "host" || name == "origin")) {
            socket->setProperty("mcpResponded", true);
            writeResponse(socket, 400, "Bad Request", "Duplicate framing header.", "text/plain");
            return;
        }
        headers.insert(name, line.mid(separator + 1).trimmed());
    }

    const QByteArray origin = allowedOrigin(headers.value("origin"));
    const QByteArray host = headers.value("host");
    if (!allowedHost(host) || (!headers.value("origin").isEmpty() && origin.isEmpty())) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 403, "Forbidden", "Only local MCP clients are allowed.", "text/plain");
        return;
    }

    const QByteArray method = requestLine.at(0);
    const QByteArray path = requestLine.at(1);
    if (method == "OPTIONS" && path == "/mcp") {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 204, "No Content", {}, "text/plain", origin);
        return;
    }
    if (method != "POST" || path != "/mcp") {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 404, "Not Found", "MCP endpoint not found.", "text/plain", origin);
        return;
    }
    if (!headers.value("transfer-encoding").isEmpty()) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 400, "Bad Request", "Transfer-Encoding is not supported.", "text/plain", origin);
        return;
    }
    if (!headers.value("content-type").toLower().startsWith("application/json")) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, 415, "Unsupported Media Type", "Expected application/json.", "text/plain", origin);
        return;
    }

    bool lengthOk = false;
    const qlonglong contentLength = headers.value("content-length").toLongLong(&lengthOk);
    if (!lengthOk || contentLength < 1 || contentLength > maxBodySize) {
        socket->setProperty("mcpResponded", true);
        writeResponse(socket, contentLength > maxBodySize ? 413 : 400,
                      contentLength > maxBodySize ? "Payload Too Large" : "Bad Request", "Invalid Content-Length.",
                      "text/plain", origin);
        return;
    }

    const qsizetype bodyOffset = headerEnd + 4;
    if (buffer.size() - bodyOffset < contentLength) {
        socket->setProperty("mcpRequestBuffer", buffer);
        return;
    }

    socket->setProperty("mcpResponded", true);
    const QByteArray body = buffer.mid(bodyOffset, static_cast<qsizetype>(contentLength));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        writeResponse(socket, 400, "Bad Request", errorBody(-32700, "Invalid JSON request."), "application/json",
                      origin);
        return;
    }

    bool hasResponse = false;
    const QJsonObject response = handleMessage(document.object(), &hasResponse);
    if (!hasResponse) {
        writeResponse(socket, 202, "Accepted", {}, "application/json", origin);
        return;
    }
    writeResponse(socket, 200, "OK", QJsonDocument(response).toJson(QJsonDocument::Compact), "application/json",
                  origin);
}

QJsonObject
McpServerPrivate::handleMessage(const QJsonObject& request, bool* hasResponse)
{
    const QJsonValue id = request.value("id");
    const bool hasId = request.contains("id");
    if (request.value("jsonrpc").toString() != QLatin1String("2.0") || !request.value("method").isString()) {
        *hasResponse = true;
        return jsonRpcError(id, -32600, "Invalid Request");
    }

    const QString method = request.value("method").toString();
    const QJsonObject params = request.value("params").toObject();
    if (method.startsWith(QLatin1String("notifications/"))) {
        *hasResponse = false;
        return {};
    }
    if (!hasId) {
        *hasResponse = true;
        return jsonRpcError(QJsonValue(QJsonValue::Null), -32600, "Requests must include an id.");
    }

    *hasResponse = true;
    QJsonValue result;
    if (method == QLatin1String("initialize")) {
        result = QJsonObject {
            { "protocolVersion", "2025-11-25" },
            { "capabilities", QJsonObject { { "tools", QJsonObject { { "listChanged", false } } } } },
            { "serverInfo", QJsonObject { { "name", "Jobman" }, { "version", QString::fromUtf8(APP_VERSION_STRING) } } },
            { "instructions",
              "Jobman tools operate on the running desktop application and its existing queue. "
              "File submission is asynchronous. Inspect job status/logs and respond to application dialogs through the named tools. "
              "Qt inspection/actions/screenshots run on the application's Qt thread." }
        };
    }
    else if (method == QLatin1String("ping")) {
        result = QJsonObject {};
    }
    else if (method == QLatin1String("tools/list")) {
        result = QJsonObject { { "tools", d.tools } };
    }
    else if (method == QLatin1String("tools/call")) {
        const QString name = params.value("name").toString();
        if (params.contains("arguments") && !params.value("arguments").isObject())
            return jsonRpcError(id, -32602, "Tool arguments must be an object.");
        result = callTool(name, params.value("arguments").toObject());
    }
    else {
        *hasResponse = true;
        return jsonRpcError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
    }

    return { { "jsonrpc", "2.0" }, { "id", id }, { "result", result } };
}

QJsonObject
McpServerPrivate::callTool(const QString& name, const QJsonObject& args)
{
    Q_ASSERT(QThread::currentThread() == d.q->thread());
    for (const QJsonValue& value : d.tools) {
        const QJsonObject definition = value.toObject();
        if (definition.value("name").toString() != name)
            continue;
        const QJsonObject schema = definition.value("inputSchema").toObject();
        const QJsonObject properties = schema.value("properties").toObject();
        for (const QJsonValue& required : schema.value("required").toArray()) {
            if (!args.contains(required.toString()))
                return toolError("Missing argument: " + required.toString());
        }
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (!properties.contains(it.key()))
                return toolError("Unknown argument: " + it.key());
            const QJsonObject property = properties.value(it.key()).toObject();
            const QString type = property.value("type").toString();
            const QJsonValue argument = it.value();
            if ((type == "string" && !argument.isString()) || (type == "array" && !argument.isArray())
                || (type == "boolean" && !argument.isBool()) || (type == "object" && !argument.isObject())
                || (type == "integer"
                    && (!argument.isDouble() || std::floor(argument.toDouble()) != argument.toDouble())))
                return toolError("Invalid type for argument: " + it.key());
            if ((property.contains("minimum") && argument.toDouble() < property.value("minimum").toDouble())
                || (property.contains("maximum") && argument.toDouble() > property.value("maximum").toDouble()))
                return toolError("Argument out of range: " + it.key());
        }
        return d.handler ? d.handler(name, args) : toolError("Jobman tool handler is unavailable.");
    }
    return toolError("Unknown Jobman tool: " + name);
}

void
McpServer::setTools(const QJsonArray& tools, ToolHandler handler)
{
    p->d.tools = tools;
    p->d.handler = std::move(handler);
}

McpServer::McpServer(QObject* parent)
    : QObject(parent)
    , p(new McpServerPrivate(this))
{}

McpServer::~McpServer() = default;

bool
McpServer::start(quint16 port)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (p->d.server->isListening())
        return true;
    if (!p->d.server->listen(QHostAddress::LocalHost, port)) {
        p->d.error = p->d.server->errorString();
        Q_EMIT errorOccurred(p->d.error);
        return false;
    }
    p->d.error.clear();
    Q_EMIT runningChanged(true);
    return true;
}

void
McpServer::stop()
{
    if (!p->d.server->isListening())
        return;
    p->d.server->close();
    const auto sockets = p->d.server->findChildren<QTcpSocket*>();
    for (QTcpSocket* socket : sockets)
        socket->disconnectFromHost();
    Q_EMIT runningChanged(false);
}

bool
McpServer::isRunning() const
{
    return p->d.server->isListening();
}

QUrl
McpServer::url() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(isRunning() ? p->d.server->serverPort() : defaultPort));
}

QString
McpServer::errorString() const
{
    return p->d.error;
}
