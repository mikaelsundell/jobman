// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once
#include <QMainWindow>

class Urlfilter : public QObject
{
    Q_OBJECT
    public:
        Urlfilter(QObject* object = nullptr);
        virtual ~Urlfilter();

    Q_SIGNALS:
        void urlChanged(const QUrl& url);
    
    protected:
        bool eventFilter(QObject* obj, QEvent *event) override;
};
