// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QDialog>

class MessagePrivate;
class Message : public QDialog {
    Q_OBJECT
public:
    Message(QWidget* parent = nullptr);
    virtual ~Message();
    void setTitle(const QString& title);
    void setMessage(const QString& message);

    static bool showMessage(QWidget* parent, const QString& title, const QString& message);

private:
    QScopedPointer<MessagePrivate> p;
};
