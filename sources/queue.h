// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include "job.h"

#include <QObject>
#include <QScopedPointer>

class QueuePrivate;
class Queue : public QObject {
    Q_OBJECT

public:
    static Queue* instance();
    QUuid beginBatch(int chunks = 256);
    void endBatch(const QUuid& uuid);
    QUuid submit(QSharedPointer<Job> job, const QUuid& batch = QUuid());
    QList<QUuid> submit(const QList<QSharedPointer<Job>>& jobs, const QUuid& batch = QUuid());
    void start(const QUuid& uuid);
    void stop(const QUuid& uuid);
    void restart(const QUuid& uuid);
    void restart(const QList<QUuid>& uuids);
    void remove(const QUuid& uuid);
    void remove(const QList<QUuid>& uuids);
    int threads() const;
    void setThreads(int threads);
    bool isBatch();
    bool isProcessing();

Q_SIGNALS:
    void batchSubmitted(const QList<QSharedPointer<Job>>& jobs);
    void jobsSubmitted(const QList<QSharedPointer<Job>>& jobs);
    void jobsProcessed(const QList<QUuid>& uuids);
    void jobsRemoved(const QList<QUuid>& uuids);

private:
    Queue();
    ~Queue();
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    class Deleter {
    public:
        static void cleanup(Queue* pointer) { delete pointer; }
    };
    static QScopedPointer<Queue, Deleter> pi;
    QScopedPointer<QueuePrivate> p;
};