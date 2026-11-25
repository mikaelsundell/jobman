// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QScopedPointer>
#include <QUrl>

#include <functional>

class McpServerPrivate;

/**
 * @brief Hosts an opt-in, loopback-only MCP Streamable HTTP endpoint.
 *
 * Tool handlers execute on the owning Qt thread. The transport follows Stageviz's
 * stateless JSON request/response implementation.
 */
class McpServer : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 defaultPort = 37652;
    using ToolHandler = std::function<QJsonObject(const QString&, const QJsonObject&)>;

    explicit McpServer(QObject* parent = nullptr);
    ~McpServer() override;
    void setTools(const QJsonArray& tools, ToolHandler handler);
    bool start(quint16 port = defaultPort);
    void stop();
    bool isRunning() const;
    QUrl url() const;
    QString errorString() const;

Q_SIGNALS:
    void runningChanged(bool running);
    void errorOccurred(const QString& error);

private:
    Q_DISABLE_COPY_MOVE(McpServer)
    QScopedPointer<McpServerPrivate> p;
};
