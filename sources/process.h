// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QObject>

class ProcessPrivate;
class Process : public QObject {
    Q_OBJECT

public:
    enum Status { Normal, Crash };
    Q_ENUM(Status)

public:
    Process();
    virtual ~Process();
    void run(const QString& command, const QStringList& arguments, const QString& startin = QString(),
             const QList<QPair<QString, QString>>& environmentvars = QList<QPair<QString, QString>>());
    bool wait();
    bool exists(const QString& command);
    void kill();
    int pid() const;
    QString standardOutput() const;
    QString standardError() const;
    int exitCode() const;
    Status exitStatus() const;

public:
    static void kill(int pid);

private:
    QScopedPointer<ProcessPrivate> p;
};
