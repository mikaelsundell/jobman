// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QScopedPointer>
#include <QString>
#include <QUuid>

struct OS {
    QStringList searchpaths;
    QList<QPair<QString, QString>> environmentvars;
};

struct Preprocess {
    struct Copyoriginal {
        QString filename;
        bool valid() const { return (!filename.isEmpty()); }
    };
    Copyoriginal copyoriginal;
};

struct Postprocess : public QObject {
};

class JobPrivate;
class Job : public QObject {
    Q_OBJECT

public:
    enum Status { Waiting, Running, Completed, Failed, DependencyFailed, Stopped };
    Q_ENUM(Status)

public:
    Job();
    virtual ~Job();
    QStringList arguments() const;
    QString command() const;
    QDateTime created() const;
    QUuid dependson() const;
    QString dir() const;
    QString filename() const;
    QString id() const;
    QString name() const;
    QString log() const;
    QString output() const;
    bool exclusive() const;
    bool overwrite() const;
    int pid() const;
    int priority() const;
    QString startin() const;
    Status status() const;
    QUuid uuid() const;
    OS& os();
    Preprocess& preprocess();
    Postprocess& postprocess();
    void setArguments(const QStringList& arguments);
    void setCommand(const QString& command);
    void setDependson(QUuid dependson);
    void setDir(const QString& dir);
    void setFilename(const QString& filename);
    void setId(const QString& id);
    void setLog(const QString& log);
    void setName(const QString& name);
    void setOutput(const QString& output);
    void setExclusive(bool exclusive);
    void setOverwrite(bool overwrite);
    void setPid(int pid);
    void setPriority(int priority);
    void setStartin(const QString& startin);
    void setStatus(Status status);

Q_SIGNALS:
    void argumentsChanged(const QStringList& arguments);
    void commandChanged(const QString& command);
    void dependsonChanged(QUuid uuid);
    void dirChanged(QString dir);
    void filenameChanged(const QString& filename);
    void idChanged(const QString& id);
    void logChanged(const QString& log);
    void nameChanged(const QString& name);
    void outputChanged(const QString& output);
    void exclusiveChanged(bool exclusive);
    void overwriteChanged(bool overwrite);
    void pidChanged(int pid);
    void priorityChanged(int priority);
    void startinChanged(const QString& startin);
    void statusChanged(Status status);

private:
    QScopedPointer<JobPrivate> p;
};
