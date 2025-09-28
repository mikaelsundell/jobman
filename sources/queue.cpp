// Copyright 2022-present Contributors to the jobman project.
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
    QThreadPool threadpool;
    QMap<QUuid, QSharedPointer<Job>> alljobs;
    QList<QSharedPointer<Job>> waitingjobs;
    QSet<QUuid> completedjobs;
    QMap<QUuid, QList<QSharedPointer<Job>>> dependentjobs;
    QMap<QUuid, QSharedPointer<Job>> removedjobs;
    QMap<QString, QUuid> exclusivejobs;
    QMap<QUuid, QList<QSharedPointer<Job>>> batchjobs;
    QMap<QUuid, int> batchchunks;
    QPointer<Queue> queue;
};

QueuePrivate::QueuePrivate()
    : threads(1)
{
    threadpool.setMaxThreadCount(threads);
    threadpool.setExpiryTimeout(-1);
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
    threadpool.setMaxThreadCount(threads);
}

QUuid
QueuePrivate::beginBatch(int chunks)
{
    QUuid uuid = QUuid::createUuid();
    batchjobs[uuid] = QList<QSharedPointer<Job>>();
    batchchunks[uuid] = chunks;
    return uuid;
}

void
QueuePrivate::endBatch(const QUuid& uuid)
{
    if (batchjobs.contains(uuid)) {
        if (!batchjobs[uuid].isEmpty()) {
            queue->batchSubmitted(batchjobs[uuid]);
        }
        batchjobs.remove(uuid);
        batchchunks.remove(uuid);
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
        alljobs.insert(job->uuid(), job);
        bool failed = false;
        if (!job->dependson().isNull() && alljobs[job->dependson()]->status() == Job::Failed) { // edge case, dependson job already failed when added
            job->setStatus(Job::Failed);
            queue->jobProcessed(job->uuid());
            failed = true;
        }
        if (!failed) {
            if (job->dependson().isNull() || completedjobs.contains(job->dependson())) {
                waitingjobs.append(job);
            }
            else {
                dependentjobs[job->dependson()].append(job);
            }
        }
    }
    processNextJobs();
    processRemovedJobs();
    if (!batch.isNull()) {
        batchjobs[batch].append(job);
        int chunksize = batchchunks[batch];
        if (batchjobs[batch].size() % chunksize == 0) {
            queue->batchSubmitted(batchjobs[batch]);
            batchjobs[batch].clear();
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
        QSharedPointer<Job> job = alljobs[uuid];
        if (job->status() == Job::Stopped) {
            job->setStatus(Job::Waiting);
            waitingjobs.append(job);
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
        QSharedPointer<Job> job = alljobs[uuid];
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
                QSharedPointer<Job> job = alljobs[jobUuid];
                if (job->status() != Job::Running) {
                    job->setStatus(Job::Waiting);
                    if (job->dependson().isNull()) {
                        waitingjobs.append(job);
                    }
                    else {
                        if (!dependentjobs[job->dependson()].contains(job)) {
                            dependentjobs[job->dependson()].append(job);
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
                    for (QSharedPointer<Job>& dependentJob : alljobs) {
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
    QList<QUuid> dependentuuids;
    {
        QMutexLocker locker(&mutex);
        if (alljobs.contains(uuid)) {
            QSharedPointer<Job> job = alljobs.take(uuid);
            removedjobs.insert(uuid, job);  // mark as removed for threads
            if (job->status() == Job::Running) {
                int pid = job->pid();
                if (pid > 0) {
                    Process::kill(job->pid());
                }
            }
            for (auto it = alljobs.begin(); it != alljobs.end(); ++it) {
                QSharedPointer<Job> job = it.value();
                if (job->dependson() == uuid) {
                    dependentuuids.append(job->uuid());
                }
            }
            dependentjobs.remove(uuid);  // prevent from trying to fail dependent deleted jobs
            waitingjobs.removeAll(job);
            completedjobs.remove(uuid);
            queue->jobProcessed(uuid);  // mark as processed, it's not removed
        }
    }
    for (const QUuid& dependentuuid : dependentuuids) {
        remove(dependentuuid);
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
                               "Could not create directory, a directory or file with the same name already exists: %1\n")
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
                    log += QString("\nStarted:\n%1\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
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
    for (int i = 0; i < waitingjobs.size(); ++i) {
        QSharedPointer<Job> job = waitingjobs[i];
        if (job->exclusive() && exclusivejobs.contains(job->command()))
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
            exclusivejobs[nextjob->command()] = nextjob->uuid();
        }

        return waitingjobs.takeAt(index);
    }
    return QSharedPointer<Job>();
}

void
QueuePrivate::processNextJobs()
{
    QMutexLocker locker(&mutex);
    qint64 free = threadpool.maxThreadCount() - threadpool.activeThreadCount();
    qint64 jobsprocess = qMin(waitingjobs.size(), free);
    if (jobsprocess == 0) {
        return;
    }
    QList<QSharedPointer<Job>> jobsrun;
    for (int i = 0; i < jobsprocess; ++i) {
        if (waitingjobs.isEmpty()) {
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
        QFuture<void> future = QtConcurrent::run(&threadpool, [this, job]() { processJob(job); });
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
    removedjobs.clear();  // safe to clear at submit, all event are processed
}

void
QueuePrivate::processDependentJobs(const QUuid& dependsonId)
{
    if (dependentjobs.contains(dependsonId)) {
        for (QSharedPointer<Job> job : dependentjobs[dependsonId]) {
            waitingjobs.append(job);
        }
        dependentjobs.remove(dependsonId);
    }
}

void
QueuePrivate::failDependentJobs(const QUuid& dependsonId)
{
    if (dependentjobs.contains(dependsonId)) {
        for (QSharedPointer<Job> job : dependentjobs[dependsonId]) {
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
        dependentjobs.remove(dependsonId);
    }
}

void
QueuePrivate::failCompletedJobs(const QUuid& uuid, const QUuid& dependsonId)
{
    if (alljobs.contains(dependsonId)) {
        QSharedPointer<Job> job = alljobs[dependsonId];
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
        for (QSharedPointer<Job>& job : alljobs) {
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
        waitingjobs.clear();
        dependentjobs.clear();
        alljobs.clear();
        completedjobs.clear();
        removedjobs.clear();
    }
    threadpool.clear();
    threadpool.waitForDone();
    thread.quit();
    thread.wait();
}

bool
QueuePrivate::isProcessing()
{
    QMutexLocker locker(&mutex);
    if (threadpool.activeThreadCount() > 0) {
        return true;
    }
    if (!waitingjobs.isEmpty()) {
        return true;
    }
    for (const QSharedPointer<Job> job : alljobs) {
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
        if (!removedjobs.contains(uuid)) {
            QSharedPointer<Job> job = alljobs[uuid];
            if (status == Job::Completed ||  // only test finished jobs
                status == Job::Failed || status == Job::DependencyFailed || status == Job::Stopped) {
                if (job->exclusive()) {
                    const QString command = job->command();
                    if (exclusivejobs.contains(command)) {
                        Q_ASSERT(exclusivejobs[command] == job->uuid());
                        exclusivejobs.remove(command);
                    }
                }
            }
            if (status == Job::Completed) {
                completedjobs.insert(uuid);  // mark the job as completed
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
