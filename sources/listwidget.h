// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QListWidget>

class PairItemPrivate;
class PairItem : public QWidget {
    Q_OBJECT
public:
    PairItem(QWidget* parent = nullptr);
    PairItem(const QString& name, const QString& value, QWidget* parent = nullptr);
    virtual ~PairItem();
    QString name() const;
    QString value() const;
    bool isChecked() const;
    void setName(const QString& name);
    void setValue(const QString& value);
    void setChecked(bool checked);

private:
    QScopedPointer<PairItemPrivate> p;
};

class ListWidgetPrivate;
class ListWidget : public QListWidget {
    Q_OBJECT
public:
    ListWidget(QWidget* parent = nullptr);
    virtual ~ListWidget();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    QScopedPointer<ListWidgetPrivate> p;
};
