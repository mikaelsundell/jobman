// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include "preset.h"

#include <QDialog>

class OptionsDialogPrivate;
class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    OptionsDialog(QWidget* parent = nullptr);
    virtual ~OptionsDialog();
    void update(QSharedPointer<Preset> preset);

private:
    QScopedPointer<OptionsDialogPrivate> p;
};
