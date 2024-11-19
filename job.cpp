// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "queue.h"

#include <QObject>
#include <QMutex>
#include <QDateTime>
#include <QPointer>

#include <QDebug>

OS::OS(QObject* parent)
: QObject(parent)
{
}

Preprocess::Preprocess(QObject* parent)
: QObject(parent)
{
}

Postprocess::Postprocess(QObject* parent)
: QObject(parent)
{
}

class JobPrivate : public QObject
{
    Q_OBJECT
    public:
        JobPrivate();
        void init();

    public:
        QDateTime created;
        QUuid uuid;
        QUuid dependson;
        QString id;
        QString filename;
        QString name;
        QString command;
        QString dir;
        QStringList arguments;
        QString output;
        QString startin;
        QString log;
        bool overwrite;
        int pid;
        int priority;
        Job::Status status;
        OS* os;
        Preprocess* preprocess;
        Postprocess* postprocess;
        QPointer<Job> job;
        mutable QMutex mutex;
};

JobPrivate::JobPrivate()
: pid(0)
, priority(10)
, status(Job::Waiting)
, overwrite(false)
, uuid(QUuid::createUuid())
{
    created = QDateTime::currentDateTime();
}

void
JobPrivate::init()
{
    os = new OS(job.data());
    preprocess = new Preprocess(job.data());
    postprocess = new Postprocess(job.data());
}

#include "job.moc"

Job::Job()
: p(new JobPrivate())
{
    p->job = this;
    p->init();
}

Job::~Job()
{
}

QStringList
Job::arguments() const
{
    QMutexLocker locker(&p->mutex);
    return p->arguments;
}

QString
Job::command() const
{
    QMutexLocker locker(&p->mutex);
    return p->command;
}

QDateTime
Job::created() const
{
    QMutexLocker locker(&p->mutex); // Lock the mutex
    return p->created;
}

QUuid
Job::dependson() const
{
    QMutexLocker locker(&p->mutex);
    return p->dependson;
}

QString
Job::dir() const
{
    QMutexLocker locker(&p->mutex);
    return p->dir;
}

QString
Job::filename() const
{
    QMutexLocker locker(&p->mutex);
    return p->filename;
}

QString
Job::id() const
{
    QMutexLocker locker(&p->mutex);
    return p->id;
}

QString
Job::name() const
{
    QMutexLocker locker(&p->mutex);
    return p->name;
}

QString
Job::log() const
{
    QMutexLocker locker(&p->mutex);
    return p->log;
}

QString
Job::output() const
{
    QMutexLocker locker(&p->mutex);
    return p->output;
}

bool
Job::overwrite() const
{
    QMutexLocker locker(&p->mutex);
    return p->overwrite;
}

int
Job::pid() const
{
    QMutexLocker locker(&p->mutex);
    return p->pid;
}


int
Job::priority() const
{
    QMutexLocker locker(&p->mutex);
    return p->priority;
}

QString
Job::startin() const
{
    QMutexLocker locker(&p->mutex);
    return p->startin;
}

Job::Status
Job::status() const
{
    QMutexLocker locker(&p->mutex);
    return p->status;
}

QUuid
Job::uuid() const
{
    QMutexLocker locker(&p->mutex);
    return p->uuid;
}

OS*
Job::os()
{
    QMutexLocker locker(&p->mutex);
    return p->os;
}

Preprocess*
Job::preprocess()
{
    QMutexLocker locker(&p->mutex);
    return p->preprocess;
}

Postprocess*
Job::postprocess()
{
    QMutexLocker locker(&p->mutex);
    return p->postprocess;
}

void
Job::setArguments(const QStringList& arguments)
{
    QMutexLocker locker(&p->mutex);
    if (p->arguments != arguments) {
        p->arguments = arguments;
        argumentsChanged(arguments);
    }
}

void
Job::setCommand(const QString& command)
{
    QMutexLocker locker(&p->mutex);
    if (p->command != command) {
        p->command = command;
        commandChanged(command);
    }
}

void
Job::setDependson(QUuid dependson)
{
    QMutexLocker locker(&p->mutex);
    if (p->dependson != dependson) {
        p->dependson = dependson;
        dependsonChanged(dependson);
    }
}

void
Job::setDir(const QString& dir)
{
    QMutexLocker locker(&p->mutex);
    if (p->dir != dir) {
        p->dir = dir;
        dirChanged(dir);
    }
}

void
Job::setFilename(const QString& filename)
{
    QMutexLocker locker(&p->mutex);
    if (p->filename != filename) {
        p->filename = filename;
        filenameChanged(filename);
    }
}

void
Job::setId(const QString& id)
{
    QMutexLocker locker(&p->mutex); // Lock the mutex
    if (p->id != id) {
        p->id = id;
        idChanged(id);
    }
}

void
Job::setLog(const QString& log)
{
    QMutexLocker locker(&p->mutex);
    if (p->log != log) {
        p->log = log;
        logChanged(log);
    }
}

void
Job::setName(const QString& name)
{
    QMutexLocker locker(&p->mutex);
    if (p->name != name) {
        p->name = name;
        nameChanged(name);
    }
}

void
Job::setOutput(const QString& output)
{
    QMutexLocker locker(&p->mutex);
    if (p->output != output) {
        p->output = output;
        outputChanged(output);
    }
}

void
Job::setOverwrite(bool overwrite)
{
    QMutexLocker locker(&p->mutex);
    if (p->overwrite != overwrite) {
        p->overwrite = overwrite;
        overwriteChanged(overwrite);
    }
}

void
Job::setPid(int pid)
{
    QMutexLocker locker(&p->mutex);
    if (p->pid != pid) {
        p->pid = pid;
        pidChanged(pid);
    }
}

void
Job::setPriority(int priority)
{
    QMutexLocker locker(&p->mutex);
    if (p->priority != priority) {
        p->priority = priority;
        priorityChanged(priority);
    }
}

void
Job::setStartin(const QString& startin)
{
    QMutexLocker locker(&p->mutex);
    if (p->startin != startin) {
        p->startin = startin;
        startinChanged(startin);
    }
}

void
Job::setStatus(Status status)
{
    QMutexLocker locker(&p->mutex);
    if (p->status != status) {
        p->status = status;
        statusChanged(status);
    }
}
