﻿// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "queue.h"
#include "platform.h"
#include "process.h"

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrent>


#define THREAD_FUNC_SAFE() \
    static QMutex mutex;   \
    QMutexLocker locker(&mutex);
#define THREAD_OBJECT_SAFE(obj) \
    static QMutex obj##_mutex;  \
    QMutexLocker locker(&obj##_mutex);

QScopedPointer<Queue, Queue::Deleter> Queue::pi;

class QueuePrivate : public QObject {
    Q_OBJECT
public:
    QueuePrivate();
    void init();
    void updateThreadCount();
    QUuid beginBatch(int chunks = 32);
    void endBatch(const QUuid& uuid);
    QUuid submit(QSharedPointer<Job> job, const QUuid& batch);
    void start(const QUuid& uuid);
    void stop(const QUuid& uuid);
    void restart(const QUuid& uuid);
    void restart(const QList<QUuid>& uuids);
    void remove(const QUuid& uuid);
    void processJob(QSharedPointer<Job> job);
    QSharedPointer<Job> findNextJob();
    void processNextJobs();
    void processRemovedJobs();
    void processDependentJobs(const QUuid& dependsonUuid);
    void failDependentJobs(const QUuid& dependsonId);
    void failCompletedJobs(const QUuid& uuid, const QUuid& dependsonId);
    void killJobs();
    bool isProcessing();

public Q_SLOTS:
    void statusChanged(const QUuid& uuid, Job::Status status);

Q_SIGNALS:
    void notifyStatusChanged(const QUuid& uuid, Job::Status status);

public:
    QString elapsedtime(qint64 milliseconds);
    QString filesize(const QString& filename);
    int threads;
    QMutex mutex;
    QThread thread;
    QThreadPool threadPool;
    QMap<QUuid, QSharedPointer<Job>> allJobs;
    QList<QSharedPointer<Job>> waitingJobs;
    QSet<QUuid> completedJobs;
    QMap<QUuid, QList<QSharedPointer<Job>>> dependentJobs;
    QMap<QUuid, QSharedPointer<Job>> removedJobs;
    QMap<QString, QUuid> exclusiveJobs;
    QMap<QUuid, QList<QSharedPointer<Job>>> batchJobs;
    QMap<QUuid, int> batchChunks;
    QPointer<Queue> queue;
};

QueuePrivate::QueuePrivate()
    : threads(1)
{
    threadPool.setMaxThreadCount(threads);
    threadPool.setExpiryTimeout(-1);
}

void
QueuePrivate::init()
{
    moveToThread(&thread);
    // treads
    QThreadPool::globalInstance()->setThreadPriority(QThread::LowPriority);  // scheduled less often than ui thread
    // connect
    connect(this, &QueuePrivate::notifyStatusChanged, this, &QueuePrivate::statusChanged, Qt::QueuedConnection);
    updateThreadCount();
    thread.start();
}

void
QueuePrivate::updateThreadCount()
{
    threadPool.setMaxThreadCount(threads);
}

QUuid
QueuePrivate::beginBatch(int chunks)
{
    QUuid uuid = QUuid::createUuid();
    batchJobs[uuid] = QList<QSharedPointer<Job>>();
    batchChunks[uuid] = chunks;
    return uuid;
}


void
QueuePrivate::endBatch(const QUuid& uuid)
{
    if (batchJobs.contains(uuid)) {
        if (!batchJobs[uuid].isEmpty()) {
            queue->batchSubmitted(batchJobs[uuid]);
        }
        batchJobs.remove(uuid);
        batchChunks.remove(uuid);
    }
}

QUuid
QueuePrivate::submit(QSharedPointer<Job> job, const QUuid& batch)
{
    {
        QMutexLocker locker(&mutex);
        QString log = QString("Uuid:\n"
                              "%1\n\n"
                              "Created:\n"
                              "%2\n\n"
                              "Filename:\n"
                              "%3 (%4)\n\n"
                              "Command:\n"
                              "%5 %6\n")
                          .arg(job->uuid().toString())
                          .arg(job->created().toString("yyyy-MM-dd HH:mm:ss"))
                          .arg(job->filename())
                          .arg(filesize(job->filename()))
                          .arg(job->command())
                          .arg(job->arguments().join(' '));
        job->setLog(log);
        allJobs.insert(job->uuid(), job);
        if (job->dependson().isNull() || completedJobs.contains(job->dependson())) {
            waitingJobs.append(job);
        }
        else {
            dependentJobs[job->dependson()].append(job);
        }
    }
    processNextJobs();
    processRemovedJobs();
    if (!batch.isNull()) {
        batchJobs[batch].append(job);
        int chunkSize = batchChunks[batch];
        if (batchJobs[batch].size() % chunkSize == 0) {
            queue->batchSubmitted(batchJobs[batch]);
            batchJobs[batch].clear();
        }
    }
    else {
        queue->jobSubmitted(job);
    }
    return job->uuid();
}

void
QueuePrivate::start(const QUuid& uuid)
{
    bool start = false;
    {
        QMutexLocker locker(&mutex);
        QSharedPointer<Job> job = allJobs[uuid];
        if (job->status() == Job::Stopped) {
            job->setStatus(Job::Waiting);
            waitingJobs.append(job);
            QString log = QString("Uuid:\n"
                                  "%1\n\n"
                                  "Command:\n"
                                  "%2 %3\n")
                              .arg(job->uuid().toString())
                              .arg(job->command())
                              .arg(job->arguments().join(' '));
            job->setLog(log);
            start = true;
        }
    }
    if (start) {
        processNextJobs();
    }
}

void
QueuePrivate::stop(const QUuid& uuid)
{
    {
        QMutexLocker locker(&mutex);
        QSharedPointer<Job> job = allJobs[uuid];
        if (job->status() == Job::Running) {
            job->setStatus(Job::Stopped);
            int pid = job->pid();
            if (pid > 0) {
                Process::kill(job->pid());
            }
            QString log = QString("Uuid:\n"
                                  "%1\n\n"
                                  "Command:\n"
                                  "%2 %3\n")
                              .arg(job->uuid().toString())
                              .arg(job->command())
                              .arg(job->arguments().join(' '));
            job->setLog(log);
        }
    }
    processNextJobs();
}

void
QueuePrivate::restart(const QUuid& uuid)
{
    restart(QList<QUuid> { uuid });
}

void
QueuePrivate::restart(const QList<QUuid>& uuids)
{
    {
        QMutexLocker locker(&mutex);
        for (QUuid uuid : uuids) {
            std::function<void(const QUuid&)> restartJob = [&](const QUuid& jobUuid) {
                QSharedPointer<Job> job = allJobs[jobUuid];
                if (job->status() != Job::Running) {
                    job->setStatus(Job::Waiting);
                    if (job->dependson().isNull()) {
                        waitingJobs.append(job);
                    }
                    else {
                        if (!dependentJobs[job->dependson()].contains(job)) {
                            dependentJobs[job->dependson()].append(job);
                        }
                    }
                    QString log = QString("Uuid:\n"
                                          "%1\n\n"
                                          "Command:\n"
                                          "%2 %3\n")
                                      .arg(job->uuid().toString())
                                      .arg(job->command())
                                      .arg(job->arguments().join(' '));
                    QString startin = job->startin();
                    if (!startin.isEmpty()) {
                        log += QString("Startin:\n"
                                       "%1\n")
                                   .arg(startin);
                    }
                    job->setLog(log);
                    for (QSharedPointer<Job>& dependentJob : allJobs) {
                        if (dependentJob->dependson() == jobUuid) {
                            restartJob(dependentJob->uuid());
                        }
                    }
                }
            };
            restartJob(uuid);
        }
    }
    processNextJobs();
}

void
QueuePrivate::remove(const QUuid& uuid)
{
    QList<QUuid> dependentUuids;
    {
        QMutexLocker locker(&mutex);
        if (allJobs.contains(uuid)) {
            QSharedPointer<Job> job = allJobs.take(uuid);
            removedJobs.insert(uuid, job);  // mark as removed for threads
            if (job->status() == Job::Running) {
                int pid = job->pid();
                if (pid > 0) {
                    Process::kill(job->pid());
                }
            }
            for (auto it = allJobs.begin(); it != allJobs.end(); ++it) {
                QSharedPointer<Job> job = it.value();
                if (job->dependson() == uuid) {
                    dependentUuids.append(job->uuid());
                }
            }
            dependentJobs.remove(uuid);  // prevent from trying to fail dependent deleted jobs
            waitingJobs.removeAll(job);
            completedJobs.remove(uuid);
            queue->jobProcessed(uuid);  // mark as processed, it's not removed
        }
    }
    for (const QUuid& dependentUuid : dependentUuids) {
        remove(dependentUuid);
    }
    queue->jobRemoved(uuid);
}

void
QueuePrivate::processJob(QSharedPointer<Job> job)
{
    QString log = job->log();
    QFileInfo commandInfo(job->command());
    if (commandInfo.isAbsolute() && !commandInfo.exists()) {
        log += QString("\nCommand error:\nCommand path could not be found: %1\n").arg(job->command());
        job->setStatus(Job::Failed);
    }
    else {
        QString command = job->command();
        if (!commandInfo.isAbsolute()) {
            for (QString searchpath : job->os().searchpaths) {
                QString filepath = QDir::cleanPath(QDir(searchpath).filePath(command));
                if (QFile::exists(filepath)) {
                    command = filepath;
                    break;
                }
            }
        }
        job->setStatus(Job::Running);
        bool valid = false;
        // test output
        QString output = job->output();
        if (output.size()) {
            if (!job->overwrite()) {
                QFileInfo fileInfo(output);
                if (fileInfo.exists()) {
                    log += QString("\nStatus:\n"
                                   "Output file already exists: %1\n")
                               .arg(output);
                    job->setStatus(Job::Failed);
                }
                else {
                    valid = true;
                }
            }
            else {
                valid = true;
            }
        }
        // test dir
        if (valid) {
            QString dirname = job->dir();
            QFileInfo dirInfo(dirname);
            if (!dirInfo.exists()) {
                QDir dir;
                if (!dir.mkdir(dirname)) {
                    log += QString("\nStatus:\n"
                                   "Could not create directory: %1\n")
                               .arg(dirname);
                    job->setStatus(Job::Failed);
                }
                else {
                    valid = true;
                }
            }
            else if (!dirInfo.isDir()) {
                log += QString("\nStatus:\n"
                               "Could not create directory, a file with the same name already exists: %1\n")
                           .arg(dirname);
                job->setStatus(Job::Failed);
            }
            else {
                valid = true;
            }
        }
        // process
        if (valid) {
            bool failed = false;
            bool stopped = false;
            // pre process
            Preprocess::Copyoriginal copyoriginal = job->preprocess().copyoriginal;
            if (copyoriginal.valid()) {
                QFileInfo fileInfo(copyoriginal.filename);
                QString originalname = QDir(job->dir()).filePath(fileInfo.baseName() + "_original." + fileInfo.suffix());
                QFile file(fileInfo.filePath());
                log += QString("\nPre-process:");
                log += QString("\nCopy original: %1 to %2\n").arg(copyoriginal.filename).arg(originalname);
                if (QFile::exists(originalname)) {
                    if (job->overwrite()) {
                        if (!QFile::remove(originalname)) {
                            log += QString("\nFailed to remove existing file: %1\n").arg(originalname);
                            log += QString("\nStatus:\n%1\n").arg("Pre-process failed");
                            job->setStatus(Job::Failed);
                            failed = true;
                        }
                    }
                    else {
                        log += QString("\nFile exists but overwrite is not set: %1\n").arg(originalname);
                        job->setStatus(Job::Failed);
                        failed = true;
                    }
                }
                if (!failed) {
                    if (!file.copy(originalname)) {
                        log += QString("\nPre-process output:\n%1\n").arg(file.errorString());
                        log += QString("\nStatus:\n%1\n").arg("Pre-process failed");
                        job->setStatus(Job::Failed);
                        failed = true;
                    }
                }
            }
            if (!failed) {
                // process
                QScopedPointer<Process> process(new Process());
                QString standardoutput;
                QString standarderror;
                if (process->exists(command)) {
                    QElapsedTimer elapsed;
                    elapsed.start();
                    process->run(command, job->arguments(), job->startin(), job->os().environmentvars);
                    int pid = process->pid();
                    job->setPid(pid);
                    QList<QPair<QString, QString>> environmentvars = job->os().environmentvars;
                    if (environmentvars.count()) {
                        log += QString("\nEnvironment:\n");
                        for (const QPair<QString, QString>& environmentvar : environmentvars) {
                            log += QString("%1=%2\n").arg(environmentvar.first).arg(environmentvar.second);
                        }
                    }
                    QList<QString> searchpaths = job->os().searchpaths;
                    if (searchpaths.count()) {
                        log += QString("\nSearch paths:\n");
                        for (const QString& searchpath : searchpaths) {
                            log += QString("%1\n").arg(searchpath);
                        }
                    }
                    log += QString("\nProcess id:\n%1\n").arg(pid);
                    job->setLog(log);
                    if (process->wait()) {
                        job->setStatus(Job::Completed);
                        log += QString("\nStatus:\n%1\n").arg("Command completed");
                    }
                    else {
                        if (job->status() == Job::Stopped) {
                            stopped = true;
                        }
                        else {
                            failed = true;
                        }
                    }
                    standardoutput = process->standardOutput();
                    standarderror = process->standardError();
                    qint64 milliseconds = elapsed.elapsed();
                    log += QString("\nElapsed time:\n%1\n").arg(elapsedtime(milliseconds));
                }
                else {
                    standarderror = "Command does not exists, make sure command can be "
                                    "found in system or application search paths";
                    failed = true;
                }
                if (failed) {
                    log += QString("\nStatus:\n%1\n").arg("Command failed");
                    log += QString("\nExit code:\n%1\n").arg(process->exitCode());
                    switch (process->exitStatus()) {
                    case Process::Normal: {
                        log += QString("\nExit status:\n%1\n").arg("Normal");
                    } break;
                    case Process::Crash: {
                        log += QString("\nExit status:\n%1\n").arg("Crash");
                    } break;
                    }
                    job->setStatus(Job::Failed);
                }
                if (stopped) {
                    log += QString("\nStatus:\n%1\n").arg("Command stopped");
                }
                if (!standardoutput.isEmpty()) {
                    log += QString("\nCommand output:\n%1").arg(standardoutput);
                }
                if (!standarderror.isEmpty()) {
                    log += QString("\nCommand error:\n%1").arg(standarderror);
                }
            }
        }
    }
    job->setLog(log);
    if (job->status() == Job::Failed) {
        if (!job->dependson().isNull()) {
            failCompletedJobs(job->uuid(), job->dependson());
        }
    }
    queue->jobProcessed(job->uuid());
}

QSharedPointer<Job>
QueuePrivate::findNextJob()
{
    int index = -1;
    QSharedPointer<Job> nextjob;
    for (int i = 0; i < waitingJobs.size(); ++i) {
        QSharedPointer<Job> job = waitingJobs[i];
        if (job->exclusive() && exclusiveJobs.contains(job->command()))
            continue;

        if (!nextjob) {
            nextjob = job;
            index = i;
        }
        else if (job->priority() > nextjob->priority()) {
            nextjob = job;
            index = i;
        }
        else if (job->priority() == nextjob->priority() && job->created() < nextjob->created()) {
            nextjob = job;
            index = i;
        }
    }
    if (index != -1) {
        if (nextjob->exclusive()) {
            exclusiveJobs[nextjob->command()] = nextjob->uuid();
        }

        return waitingJobs.takeAt(index);
    }
    return QSharedPointer<Job>();
}

void
QueuePrivate::processNextJobs()
{
    QMutexLocker locker(&mutex);
    qint64 free = threadPool.maxThreadCount() - threadPool.activeThreadCount();
    qint64 jobsprocess = qMin(waitingJobs.size(), free);
    if (jobsprocess == 0) {
        return;
    }
    QList<QSharedPointer<Job>> jobsrun;
    for (int i = 0; i < jobsprocess; ++i) {
        if (waitingJobs.isEmpty()) {
            break;
        }
        QSharedPointer<Job> job = findNextJob();
        if (job) {
            jobsrun.append(job);
        }
        else {
            break;
        }
    }
    for (QSharedPointer<Job>& job : jobsrun) {
        QFuture<void> future = QtConcurrent::run(&threadPool, [this, job]() { processJob(job); });
        QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, job]() {
            watcher->deleteLater();
            statusChanged(job->uuid(), job->status());
        });
        watcher->setFuture(future);
    }
}

void
QueuePrivate::processRemovedJobs()
{
    removedJobs.clear();  // safe to clear at submit, all event are processed
}

void
QueuePrivate::processDependentJobs(const QUuid& dependsonId)
{
    if (dependentJobs.contains(dependsonId)) {
        for (QSharedPointer<Job> job : dependentJobs[dependsonId]) {
            waitingJobs.append(job);
        }
        dependentJobs.remove(dependsonId);
    }
}

void
QueuePrivate::failDependentJobs(const QUuid& dependsonId)
{
    if (dependentJobs.contains(dependsonId)) {
        for (QSharedPointer<Job> job : dependentJobs[dependsonId]) {
            QString log = QString("Uuid:\n"
                                  "%1\n\n"
                                  "Command:\n"
                                  "%2 %3\n\n"
                                  "Status:\n"
                                  "Command cancelled, dependent job failed: %4")
                              .arg(job->uuid().toString())
                              .arg(job->command())
                              .arg(job->arguments().join(' '))
                              .arg(dependsonId.toString());
            job->setLog(log);
            job->setStatus(Job::Failed);
            queue->jobProcessed(job->uuid());
            notifyStatusChanged(job->uuid(), job->status());
            failDependentJobs(job->uuid());
        }
        dependentJobs.remove(dependsonId);
    }
}

void
QueuePrivate::failCompletedJobs(const QUuid& uuid, const QUuid& dependsonId)
{
    if (allJobs.contains(dependsonId)) {
        QSharedPointer<Job> job = allJobs[dependsonId];
        QString log = job->log();
        log += QString("\nDependent error:\n%1").arg("Dependent job failed: %1").arg(uuid.toString());
        job->setLog(log);
        job->setStatus(Job::DependencyFailed);
        if (!job->dependson().isNull()) {
            failCompletedJobs(dependsonId, job->dependson());
        }
    }
}

void
QueuePrivate::killJobs()
{
    {
        QMutexLocker locker(&mutex);
        for (QSharedPointer<Job>& job : allJobs) {
            if (job->status() == Job::Running) {
                job->setStatus(Job::Stopped);
                if (job->pid() > 0) {
                    Process::kill(job->pid());
                }
            }
        }
    }
    {
        QMutexLocker locker(&mutex);
        waitingJobs.clear();
        dependentJobs.clear();
        allJobs.clear();
        completedJobs.clear();
        removedJobs.clear();
    }
    threadPool.clear();
    threadPool.waitForDone();
    thread.quit();
    thread.wait();
}

bool
QueuePrivate::isProcessing()
{
    QMutexLocker locker(&mutex);
    if (threadPool.activeThreadCount() > 0) {
        return true;
    }
    if (!waitingJobs.isEmpty()) {
        return true;
    }
    for (const QSharedPointer<Job> job : allJobs) {
        if (job->status() == Job::Running) {
            return true;
        }
    }
    return false;
}

void
QueuePrivate::statusChanged(const QUuid& uuid, Job::Status status)
{
    {
        QMutexLocker locker(&mutex);
        if (!removedJobs.contains(uuid)) {
            QSharedPointer<Job> job = allJobs[uuid];
            if (status == Job::Completed ||  // only test finished jobs
                status == Job::Failed || status == Job::DependencyFailed || status == Job::Stopped) {
                if (job->exclusive()) {
                    const QString command = job->command();
                    if (exclusiveJobs.contains(command)) {
                        Q_ASSERT(exclusiveJobs[command] == job->uuid());
                        exclusiveJobs.remove(command);
                    }
                }
            }
            if (status == Job::Completed) {
                completedJobs.insert(uuid);  // Mark the job as completed
                processDependentJobs(uuid);
            }
            else if (status == Job::Failed) {
                failDependentJobs(uuid);
            }
        }
    }
    processNextJobs();
}

QString
QueuePrivate::elapsedtime(qint64 milliseconds)
{
    qint64 seconds = milliseconds / 1000;
    qint64 hours = seconds / 3600;
    qint64 minutes = (seconds % 3600) / 60;
    qint64 secs = seconds % 60;

    QStringList parts;
    if (hours > 0) {
        parts << QString::number(hours) + " hour" + (hours > 1 ? "s" : "");
    }
    if (minutes > 0) {
        parts << QString::number(minutes) + " minute" + (minutes > 1 ? "s" : "");
    }
    if (secs > 0 || parts.isEmpty()) {
        parts << QString::number(secs) + " second" + (secs != 1 ? "s" : "");
    }
    return parts.join(", ");
}

QString
QueuePrivate::filesize(const QString& filename)
{
    QFileInfo fileinfo(filename);
    qint64 size = fileinfo.size();
    QString stringsize;
    if (size < 1024) {
        stringsize = QString("%1 B").arg(size);
    }
    else if (size < 1024 * 1024) {
        stringsize = QString::number(size / 1024.0, 'f', 1) + " KB";
    }
    else if (size < 1024LL * 1024 * 1024) {
        stringsize = QString::number(size / (1024.0 * 1024.0), 'f', 1) + " MB";
    }
    else {
        stringsize = QString::number(size / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
    }
    return stringsize;
}

#include "queue.moc"

Queue::Queue()
    : p(new QueuePrivate())
{
    p->queue = this;
    p->init();
}

Queue::~Queue() { p->killJobs(); }

Queue*
Queue::instance()
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    if (!pi) {
        pi.reset(new Queue());
    }
    return pi.data();
}

QUuid
Queue::beginBatch(int chunks)
{
    return p->beginBatch(chunks);
}

void
Queue::endBatch(const QUuid& uuid)
{
    return p->endBatch(uuid);
}

QUuid
Queue::submit(QSharedPointer<Job> job, const QUuid& uuid)
{
    if (QThread::currentThread() == &p->thread) {
        return p->submit(job, uuid);
    }
    QUuid result;
    QMetaObject::invokeMethod(
        p.data(), [&]() { result = p->submit(job, uuid); }, Qt::BlockingQueuedConnection);
    return result;
}

void
Queue::start(const QUuid& uuid)
{
    p->start(uuid);
}

void
Queue::stop(const QUuid& uuid)
{
    p->stop(uuid);
}

void
Queue::restart(const QUuid& uuid)
{
    p->restart(uuid);
}

void
Queue::restart(const QList<QUuid>& uuids)
{
    p->restart(uuids);
}

void
Queue::remove(const QUuid& uuid)
{
    p->remove(uuid);
}

int
Queue::threads() const
{
    return p->threads;
}

void
Queue::setThreads(int threads)
{
    p->threads = threads;
    p->updateThreadCount();
    p->processNextJobs();
}

bool
Queue::isProcessing()
{
    return p->isProcessing();
}
