// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once
#include <QMainWindow>

class JobmanPrivate;
class Jobman : public QMainWindow {
    Q_OBJECT
public:
    Jobman();
    virtual ~Jobman();

private:
    QScopedPointer<JobmanPrivate> p;
};
