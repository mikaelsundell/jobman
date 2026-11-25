// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "queue.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <future>
#include <stdexcept>

namespace {
void
require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void
waitFor(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        require(timer.elapsed() < 15000, "Timed out waiting for queue/signals");
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
}

class Probe : public QObject {
public:
    struct Record {
        QSharedPointer<Job> job;
        QList<Job::Status> statuses;
        int logs = 0;
        int pids = 0;
        int priorities = 0;
        int processed = 0;
        int removed = 0;
        QString lastLog;
    };

    explicit Probe(const QString& child)
        : child(child)
        , queue(Queue::instance())
    {
        require(temporary.isValid(), "Could not create queue fixtures");
        qRegisterMetaType<QList<QSharedPointer<Job>>>();
        connect(queue, &Queue::jobsSubmitted, this, [this](const QList<QSharedPointer<Job>>& jobs) {
            checkThread();
            checkJobs(jobs);
            submitted += jobs.size();
        });
        connect(queue, &Queue::batchSubmitted, this, [this](const QList<QSharedPointer<Job>>& jobs) {
            checkThread();
            checkJobs(jobs);
            batches.append(jobs.size());
        });
        connect(queue, &Queue::jobsProcessed, this, [this](const QList<QUuid>& uuids) {
            checkThread();
            for (const QUuid& uuid : uuids) {
                valid = valid && names.contains(uuid);
                if (names.contains(uuid))
                    ++records[names.value(uuid)].processed;
            }
        });
        connect(queue, &Queue::jobsRemoved, this, [this](const QList<QUuid>& uuids) {
            checkThread();
            for (const QUuid& uuid : uuids) {
                valid = valid && names.contains(uuid);
                if (names.contains(uuid))
                    ++records[names.value(uuid)].removed;
            }
        });
    }

    QSharedPointer<Job> add(const QString& name, const QSharedPointer<Job>& dependency = {}, int exitCode = 0,
                            int delay = 30)
    {
        QSharedPointer<Job> job(new Job());
        job->setName(name);
        job->setCommand(child);
        job->setFilename(child);
        job->setDir(temporary.path());
        job->setOutput(temporary.filePath(name + ".txt"));
        job->setOverwrite(true);
        if (dependency)
            job->setDependson(dependency->uuid());
        job->setArguments({ "job", job->output(), name, dependency ? dependency->output() : "none",
                            QString::number(exitCode), QString::number(delay) });
        records[name].job = job;
        names[job->uuid()] = name;
        // Match the monitor: status/priority/log notifications must run queued on the UI thread.
        connect(
            job.data(), &Job::statusChanged, this,
            [this, name](Job::Status status) {
                checkThread();
                records[name].statuses.append(status);
                if (status == Job::Running) {
                    starts.append(name);
                    ++running;
                    maximumRunning = qMax(maximumRunning, running);
                }
                else if (status == Job::Completed || status == Job::Failed || status == Job::Stopped) {
                    --running;
                }
            },
            Qt::QueuedConnection);
        connect(
            job.data(), &Job::logChanged, this,
            [this, name](const QString& log) {
                checkThread();
                if (records[name].job->log().isEmpty())
                    valid = false;
                ++records[name].logs;
                records[name].lastLog = log;
            },
            Qt::QueuedConnection);
        connect(
            job.data(), &Job::pidChanged, this,
            [this, name](int pid) {
                checkThread();
                valid = valid && pid > 0;
                ++records[name].pids;
            },
            Qt::QueuedConnection);
        connect(
            job.data(), &Job::priorityChanged, this,
            [this, name](int priority) {
                checkThread();
                valid = valid && records[name].job->priority() == priority;
                ++records[name].priorities;
            },
            Qt::QueuedConnection);
        return job;
    }

    void idle()
    {
        waitFor([&]() { return !queue->isProcessing(); });
        // Barrier through the queue thread, then drain its queued receiver notifications.
        queue->threads();
        QCoreApplication::processEvents();
        require(valid, "A notification ran on the wrong thread or carried an invalid value");
        for (const Record& record : records)
            require(record.lastLog == record.job->log(), "Final logChanged payload did not reach the receiver");
    }

    void completed(const QString& name, int runs = 1)
    {
        const Record& record = records[name];
        require(record.job->status() == Job::Completed, "Job did not complete");
        require(record.processed == runs, "jobsProcessed was missing or duplicated");
        require(record.pids == runs, "pidChanged was missing or duplicated");
        require(record.logs >= 3 * runs, "logChanged notifications were lost");
        QFile artifact(record.job->output());
        require(artifact.open(QIODevice::ReadOnly) && artifact.readAll() == "artifact:" + name.toUtf8() + '\n',
                "Queue produced the wrong output artifact");
        require(record.job->log().contains("Command output:\nstdout:" + name + '\n')
                    && record.job->log().contains("Command error:\nstderr:" + name + '\n'),
                "Final log lost or mixed stdout/stderr");
    }

    QJsonObject report() const
    {
        QJsonObject jobs;
        for (auto it = records.cbegin(); it != records.cend(); ++it) {
            const Record& record = it.value();
            QJsonArray statuses;
            for (Job::Status status : record.statuses)
                statuses.append(int(status));
            QFile artifact(record.job->output());
            const QString output = artifact.open(QIODevice::ReadOnly) ? QString::fromUtf8(artifact.readAll())
                                                                      : QString();
            const QString log = record.job->log();
            const QString stdoutText = log.contains("\nCommand output:\n")
                                           ? log.section("\nCommand output:\n", 1).section("\nCommand error:\n", 0, 0)
                                           : QString();
            const QString stderrText = log.contains("\nCommand error:\n")
                                           ? log.section("\nCommand error:\n", 1).section("\nDependent error:\n", 0, 0)
                                           : QString();
            jobs[it.key()] = QJsonObject { { "status", int(record.job->status()) },
                                           { "statuses", statuses },
                                           { "processed", record.processed },
                                           { "removed", record.removed },
                                           { "pids", record.pids },
                                           { "logs", record.logs },
                                           { "priorities", record.priorities },
                                           { "artifact", output },
                                           { "stdout", stdoutText },
                                           { "stderr", stderrText } };
        }
        QJsonArray batchSizes;
        for (int size : batches)
            batchSizes.append(size);
        return QJsonObject { { "jobs", jobs }, { "submitted", submitted }, { "batches", batchSizes } };
    }

    void checkThread() { valid = valid && QThread::currentThread() == QCoreApplication::instance()->thread(); }

    void checkJobs(const QList<QSharedPointer<Job>>& jobs)
    {
        for (const auto& job : jobs) {
            if (!job || !names.contains(job->uuid())) {
                valid = false;
                continue;
            }
            valid = valid && records.value(names.value(job->uuid())).job == job;
        }
    }

    QString child;
    Queue* queue;
    QTemporaryDir temporary;
    QMap<QString, Record> records;
    QMap<QUuid, QString> names;
    QList<int> batches;
    QStringList starts;
    int submitted = 0;
    int running = 0;
    int maximumRunning = 0;
    bool valid = true;
};

void
runCase(const QString& name, Probe& probe)
{
    Queue* queue = probe.queue;
    queue->setThreads(2);
    if (name == "job_timestamps") {
        Job job;
        const QDateTime created = job.created();
        require(!job.started().isValid() && !job.finished().isValid(), "A new job already had execution times");
        QList<Job::Status> observed;
        bool readable = true;
        QObject::connect(
            &job, &Job::statusChanged, &job,
            [&](Job::Status status) {
                // A direct slot must be able to read the job without deadlocking on its mutex.
                readable = readable && job.status() == status;
                if (status == Job::Running)
                    readable = readable && job.started().isValid() && !job.finished().isValid();
                else if (status != Job::Waiting)
                    readable = readable && job.finished().isValid();
                observed.append(status);
            },
            Qt::DirectConnection);
        for (const auto terminal : { Job::Completed, Job::Failed, Job::Stopped }) {
            job.setStatus(Job::Running);
            const QDateTime started = job.started();
            job.setStatus(Job::Running);
            require(job.started() == started, "Repeated Running status reset the start time");
            job.setStatus(terminal);
            require(job.finished() >= started && job.created() == created, "Execution timestamps were inconsistent");
            job.setStatus(Job::Waiting);
        }
        Job dependent;
        dependent.setStatus(Job::DependencyFailed);
        require(!dependent.started().isValid() && dependent.finished().isValid(),
                "A dependency failure acquired an execution start time or lost its finish time");
        require(readable && observed.size() == 9, "Status signals changed count or exposed incomplete timestamp state");
    }
    else if (name == "queue_signals" || name == "queue_worker_submission") {
        QList<QSharedPointer<Job>> jobs;
        for (int i = 0; i < 8; ++i) {
            auto job = probe.add(QString("job%1").arg(i), {}, 0, 100);
            job->setPriority(22);
            jobs.append(job);
        }
        if (name == "queue_worker_submission") {
            auto submission = std::async(std::launch::async, [queue, jobs]() { return queue->submit(jobs); });
            waitFor([&]() { return submission.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
            const QList<QUuid> uuids = submission.get();
            require(uuids.size() == jobs.size(), "Worker-thread submission lost jobs");
            for (int i = 0; i < jobs.size(); ++i)
                require(uuids[i] == jobs[i]->uuid(), "Worker-thread submission returned the wrong UUID");
        }
        else {
            queue->submit(jobs);
        }
        probe.idle();
        require(probe.submitted == 8 && probe.maximumRunning == 2, "Submission delivery or concurrency limit changed");
        for (const auto& job : jobs) {
            probe.completed(job->name());
            const auto& record = probe.records[job->name()];
            require(record.statuses == QList<Job::Status> { Job::Running, Job::Completed } && record.priorities == 1,
                    "Queued status/priority notification sequence changed");
        }
    }
    else if (name == "queue_dependencies") {
        auto first = probe.add("first");
        auto second = probe.add("second", first);
        auto third = probe.add("third", second);
        queue->submit({ first, second, third });
        probe.idle();
        require(probe.starts == QStringList { "first", "second", "third" }, "Dependency execution order changed");
        for (const QString& id : probe.starts)
            probe.completed(id);
    }
    else if (name == "queue_failure_propagation") {
        auto first = probe.add("first", {}, 7);
        auto second = probe.add("second", first);
        queue->submit({ first, second });
        probe.idle();
        require(first->status() == Job::DependencyFailed && second->status() == Job::Failed,
                "Existing failure-propagation states changed");
        require(probe.starts == QStringList { "first" }, "Failed dependency allowed child to execute");
        require(probe.records["first"].processed == 1 && probe.records["second"].processed == 1,
                "Failure notifications were missing or duplicated");
        require(!QFileInfo::exists(first->output()) && !QFileInfo::exists(second->output()),
                "Failed jobs unexpectedly produced artifacts");
    }
    else if (name == "queue_priority_exclusive") {
        auto low = probe.add("low");
        auto high = probe.add("high");
        auto middle = probe.add("middle");
        low->setPriority(0);
        high->setPriority(100);
        middle->setPriority(50);
        for (const auto& job : { low, high, middle })
            job->setExclusive(true);
        queue->submit({ low, high, middle });
        probe.idle();
        require(probe.starts == QStringList { "high", "middle", "low" } && probe.maximumRunning == 1,
                "Priority or exclusive scheduling changed");
        for (const QString& id : probe.starts)
            probe.completed(id);
    }
    else if (name == "queue_batch") {
        auto first = probe.add("first");
        auto second = probe.add("second");
        auto third = probe.add("third");
        const QUuid batch = queue->beginBatch(2);
        queue->submit({ first, second, third }, batch);
        queue->endBatch(batch);
        probe.idle();
        require(probe.batches == QList<int> { 2, 1 } && probe.submitted == 0 && !queue->isBatch(),
                "Batch chunk delivery changed");
        for (const auto& job : { first, second, third })
            probe.completed(job->name());
    }
    else if (name == "queue_stop_start" || name == "queue_restart") {
        auto job = probe.add("first", {}, 0, name == "queue_stop_start" ? 300 : 30);
        queue->submit(job);
        if (name == "queue_stop_start") {
            waitFor([&]() {
                return job->status() == Job::Running && job->pid() > 0 && job->log().contains("\nStarted:\n");
            });
            queue->stop(job->uuid());
            probe.idle();
            require(job->status() == Job::Stopped && probe.records["first"].processed == 1,
                    "Stop did not preserve the stopped state and completion notification");
            queue->start(job->uuid());
        }
        else {
            probe.idle();
            probe.completed("first");
            const QDateTime created = job->created();
            const QString filename = job->log().section("\nFilename:\n", 1, 1).section('\n', 0, 0);
            const QDateTime beforeRestart = QDateTime::currentDateTime();
            queue->restart(job->uuid());
            const QString restartedLog = job->log();
            const QString timestamp = restartedLog.section("Restarted:\n", 1, 1).section('\n', 0, 0);
            const QDateTime restarted = QDateTime::fromString(timestamp, "yyyy-MM-dd HH:mm:ss");
            require(restarted.isValid() && beforeRestart.secsTo(restarted) >= -1
                        && restarted.secsTo(QDateTime::currentDateTime()) >= 0,
                    "Restarted log timestamp was missing or incorrect");
            require(restartedLog.contains("\nFilename:\n" + filename + '\n') && !filename.isEmpty()
                        && !restartedLog.contains("\nCreated:\n") && job->created() == created,
                    "Restart did not restore the filename/size header or changed the creation time");
        }
        probe.idle();
        probe.completed("first", 2);
        const auto initial = name == "queue_stop_start" ? Job::Stopped : Job::Completed;
        require(probe.records["first"].statuses
                    == QList<Job::Status> { Job::Running, initial, Job::Waiting, Job::Running, Job::Completed },
                "Stop/start or restart notification sequence changed");
    }
    else if (name == "queue_remove") {
        auto first = probe.add("first", {}, 0, 1000);
        auto second = probe.add("second", first);
        queue->submit({ first, second });
        waitFor([&]() {
            return first->status() == Job::Running && first->pid() > 0 && first->log().contains("\nStarted:\n");
        });
        queue->remove(first->uuid());
        probe.idle();
        require(probe.records["first"].removed == 1 && probe.records["second"].removed == 1,
                "Removal did not notify both root and dependent");
        require(probe.starts == QStringList { "first" } && !QFileInfo::exists(first->output())
                    && !QFileInfo::exists(second->output()),
                "Removal allowed a child to run or left an unexpected artifact");
    }
    else {
        throw std::runtime_error("Unknown queue regression case");
    }
    require(probe.valid, "Queued receiver thread or signal payload was incorrect");
}
}  // namespace

int
main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const QString name = application.arguments().value(1);
    try {
        Probe probe(QFileInfo(application.arguments().value(2)).absoluteFilePath());
        runCase(name, probe);
        qInfo().noquote() << "RESULT" << QJsonDocument(probe.report()).toJson(QJsonDocument::Compact);
        qInfo().noquote() << "PASS" << name;
        return 0;
    } catch (const std::exception& error) {
        qCritical().noquote() << "FAIL" << name << error.what();
        return 1;
    }
}
