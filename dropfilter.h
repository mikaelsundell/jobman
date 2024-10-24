// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once
#include <QMainWindow>

class Dropfilter : public QObject
{
    Q_OBJECT
    public:
        Dropfilter(QObject* object = nullptr);
        virtual ~Dropfilter();

    Q_SIGNALS:
        void textChanged(const QString& text);
    
    protected:
        bool eventFilter(QObject* obj, QEvent *event) override;
};
