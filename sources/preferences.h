// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QDialog>

class PreferencesPrivate;
class Preferences : public QDialog {
    Q_OBJECT
public:
    Preferences(QWidget* parent = nullptr);
    virtual ~Preferences();

private:
    QScopedPointer<PreferencesPrivate> p;
};
