// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QList>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QUuid>

class JobPrivate;
class Job : public QObject {
    Q_OBJECT
    
    public:
        enum Status {
            Waiting,
            Running,
            Completed,
            Failed,
            Dependency,
            Stopped
        };
        Q_ENUM(Status)

    public:
        Job();
        virtual ~Job();
        QStringList arguments() const;
        QString command() const;
        QDateTime created() const;
        QUuid dependson() const;
        QString filename() const;
        QString id() const;
        QString name() const;
        QString log() const;
        QString output() const;
        int pid() const;
        int priority() const;
        QString startin() const;
        Status status() const;
        QUuid uuid() const;
        void setArguments(const QStringList& arguments);
        void setCommand(const QString& command);
        void setDependson(QUuid dependson);
        void setFilename(const QString& filename);
        void setId(const QString& id);
        void setLog(const QString& log);
        void setName(const QString& name);
        void setOutput(const QString& output);
        void setPid(int pid);
        void setPriority(int priority);
        void setStartin(const QString& startin);
        void setStatus(Status status);
    
    Q_SIGNALS:
        void argumentsChanged(const QStringList& arguments);
        void commandChanged(const QString& command);
        void dependsonChanged(QUuid uuid);
        void filenameChanged(const QString& filename);
        void idChanged(const QString& id);
        void logChanged(const QString& log);
        void nameChanged(const QString& name);
        void outputChanged(const QString& output);
        void pidChanged(int pid);
        void priorityChanged(int priority);
        void startinChanged(const QString& startin);
        void statusChanged(Status status);
    
    private:
        QScopedPointer<JobPrivate> p;
};
