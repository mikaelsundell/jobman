// Copyright 2022-present Contributors to the eventfilter project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/eventfilter

#include "clickfilter.h"

#include <QMouseEvent>

Clickfilter::Clickfilter(QObject* parent)
    : QObject(parent)
{}

Clickfilter::~Clickfilter() {}

bool
Clickfilter::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            emit pressed();
        }
    }
    return QObject::eventFilter(obj, event);
}
