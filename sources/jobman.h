// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once
#include <QMainWindow>
#include <QUrl>

class JobmanPrivate;
class Jobman : public QMainWindow {
    Q_OBJECT
public:
    Jobman();
    virtual ~Jobman();
    bool startMcpServer(quint16 port = 37652);
    void stopMcpServer();
    bool isMcpServerRunning() const;
    QUrl mcpServerUrl() const;
    QString mcpServerError() const;

private:
    QScopedPointer<JobmanPrivate> p;
};
