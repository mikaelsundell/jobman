// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QList>
#include <QScopedPointer>
#include <QString>
#include <QVariant>

class Option : public QObject {
public:
    Option();
    virtual ~Option();
    QString id;
    QString name;
    QString description;
    QString flag;
    QString type;
    QString toggle;
    QVariant value;
    QVariant defaultvalue;
    QVariant minimum;
    QVariant maximum;
    QVariant flagonly;
    bool enabled;
    QList<QPair<QString, QVariant>> options;
};

class Task : public QObject {
public:
    Task();
    virtual ~Task();
    QString id;
    QString name;
    QString command;
    QString extension;
    QString output;
    QString arguments;
    QString startin;
    QString dependson;
    QStringList documentation;
    QVariant exclusive;
};

class PresetPrivate;
class Preset : public QObject {
    Q_OBJECT
public:
    Preset();
    virtual ~Preset();
    bool read(const QString& filename);
    bool valid() const;
    QString id() const;
    QString error() const;
    QString filename() const;
    QUuid uuid() const;
    QString name() const;
    QString type() const;
    QString filter() const;
    QList<QSharedPointer<Option>> options() const;
    QList<QSharedPointer<Task>> tasks() const;

private:
    QScopedPointer<PresetPrivate> p;
};
