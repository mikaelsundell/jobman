// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once

#include <QList>
#include <QScopedPointer>
#include <QString>
#include <QVariant>

struct Option {
    public:
        Option() = default;
        
    public:
        QString name;
        QString type;
        QVariant value;
        QVariant defaultValue;
        QVariant minimum;
        QVariant maximum;
        QList<QPair<QString, QVariant>> options;
};

class Task {
    public:
        Task() = default;
    
    public:
        QString id;
        QString name;
        QString command;
        QString extension;
        QString arguments;
        QString startin;
        QString dependson;
        QStringList documentation;
};

class PresetPrivate;
class Preset
{
    public:
        Preset();
        virtual ~Preset();
        bool read(const QString& filename);
        bool valid() const;
        QString error() const;
        QString filename() const;

    public:
        QString name() const;
        QList<Option> options() const;
        QList<Task> tasks() const;
    
    private:
        QScopedPointer<PresetPrivate> p;
};
