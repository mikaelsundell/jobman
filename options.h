// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QDialog>

class OptionsPrivate;
class Options : public QDialog
{
    Q_OBJECT
    public:
    Options(QWidget* parent = nullptr);
        virtual ~Options();

    private:
        QScopedPointer<OptionsPrivate> p;
};
