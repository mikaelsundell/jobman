// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include "preset.h"

#include <QObject>

struct Paths {
public:
    bool overwrite;
    bool copyoriginal;
    bool createpaths;
    QString searchpaths;
    QString outputpath;
};

class ProcessorPrivate;
class Processor : public QObject {
    Q_OBJECT
public:
    Processor(QObject* parent = nullptr);
    virtual ~Processor();
    QList<QUuid> submit(const QList<QString>& files, const QSharedPointer<Preset>& preset, const Paths& paths);
    QList<QUuid> submit(const QSharedPointer<Preset>& preset, const Paths& paths);

Q_SIGNALS:
    void fileSubmitted(const QString& file);

private:
    QScopedPointer<ProcessorPrivate> p;
};
