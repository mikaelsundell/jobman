// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QList>
#include <QScopedPointer>
#include <QString>
#include <QVariant>

struct Option : public QObject {
    public:
        Option(QObject* parent = nullptr);

    public:
        QString id;
        QString name;
        QString flag;
        QString type;
        QVariant value;
        QVariant defaultvalue;
        QVariant minimum;
        QVariant maximum;
        QList<QPair<QString, QVariant>> options;
};

class Task : public QObject {
    public:
        Task(QObject* parent = nullptr);
    
    public:
        QString id;
        QString name;
        QString command;
        QString extension;
        QString output;
        QString arguments;
        QString startin;
        QString dependson;
        QStringList documentation;
};

class PresetPrivate;
class Preset : public QObject
{
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

    public:
        QString name() const;
        QList<Option*> options() const;
        QList<Task*> tasks() const;
    
    private:
        QScopedPointer<PresetPrivate> p;
};
