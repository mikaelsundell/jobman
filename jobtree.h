// Copyright 2022-present Contributors to the automator project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/automator

#pragma once

#include <QTreeWidget>

class JobTreePrivate;
class JobTree : public QTreeWidget
{
    Q_OBJECT
    public:
        JobTree(QWidget* parent = nullptr);
        virtual ~JobTree();
    
    protected:
        void keyPressEvent(QKeyEvent* event) override;
        void mousePressEvent(QMouseEvent *event) override;
    
    private:
        QScopedPointer<JobTreePrivate> p;
};