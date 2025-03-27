// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include "preset.h"

#include <QWidget>

class OptionsWidgetPrivate;
class OptionsWidget : public QWidget {
    Q_OBJECT
public:
    OptionsWidget(QWidget* parent = nullptr);
    virtual ~OptionsWidget();
    void update(QSharedPointer<Preset> preset);

private:
    QScopedPointer<OptionsWidgetPrivate> p;
};
