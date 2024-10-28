// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include "preset.h"

#include <QDialog>

class OptionsPrivate;
class Options : public QDialog
{
    Q_OBJECT
    public:
        Options(QWidget* parent = nullptr);
        virtual ~Options();

    public Q_SLOTS:
        void update(QSharedPointer<Preset> preset);
    
    private:
        QScopedPointer<OptionsPrivate> p;
};
