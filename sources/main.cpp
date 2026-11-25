// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "jobman.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

int
main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(APP_NAME);
    app.setApplicationVersion(APP_VERSION_STRING);
    QCommandLineParser parser;
    parser.setApplicationDescription("Jobman batch processing");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption mcp("mcp", "Start the local MCP server.");
    const QCommandLineOption mcpPort("mcp-port", "Loopback MCP port; 0 selects an available port. Requires --mcp.",
                                     "port", "37652");
    parser.addOption(mcp);
    parser.addOption(mcpPort);
    parser.process(app);
    bool validPort = false;
    const int port = parser.value(mcpPort).toInt(&validPort);
    if (!validPort || port < 0 || port > 65535 || (parser.isSet(mcpPort) && !parser.isSet(mcp))) {
        qCritical() << "Use --mcp with --mcp-port in the range 0..65535.";
        return 1;
    }
    Jobman jobman;
    if (parser.isSet(mcp) && !jobman.startMcpServer(static_cast<quint16>(port))) {
        qCritical().noquote() << "MCP startup failed:" << jobman.mcpServerError();
        return 1;
    }
    jobman.show();
    return app.exec();
}
