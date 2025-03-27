// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include "job.h"

#include <QDialog>

class MonitorPrivate;
class Monitor : public QDialog {
    Q_OBJECT
public:
    Monitor(QWidget* parent = nullptr);
    virtual ~Monitor();

private:
    QScopedPointer<MonitorPrivate> p;
};
