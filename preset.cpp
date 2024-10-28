// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "preset.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

#include <QDebug>

class PresetPrivate : public QObject
{
    public:
        PresetPrivate();
        void init();
        bool read();
    
    public:
        QString id;
        QString error;
        QString filename;
        QString name;
        QList<QString> description;
        QList<Option> options;
        QList<Task> tasks;
        QUuid uuid;
        bool valid;
};

PresetPrivate::PresetPrivate()
: valid(false)
, uuid(QUuid::createUuid())
{
}

void
PresetPrivate::init()
{
}

bool
PresetPrivate::read()
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("Failed to open file: %1").arg(filename);
        valid = false;
        return valid;
    }
    QByteArray jsonData = file.readAll();
    file.close();
    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(jsonData, &jsonError);
    if (document.isNull()) {
        error = QString("Failed to create json document for file:\n"
                        "%1\n\n"
                        "Parse error:\n"
                        "%2 at offset %3")
                .arg(filename)
                .arg(jsonError.errorString())
                .arg(jsonError.offset);
        valid = false;
        return valid;
    }
    if (!document.isObject()) {
        error = QString("Json document is not an object for file: %1").arg(filename);
        valid = false;
        return valid;
    }
    QJsonObject json = document.object();
    if (json.contains("id") && json["id"].isString()) {
        id = json["id"].toString();
    }
    else {
        error = QString("Json for preset: \"%1\" contains no a unique id").arg(filename);
        valid = false;
        return valid;
    }
    if (json.contains("name") && json["name"].isString()) {
        name = json["name"].toString();
    }
    if (json.contains("options") && json["options"].isArray()) {
        QJsonArray optionsArray = json["options"].toArray();
        for (int i = 0; i < optionsArray.size(); ++i) {
            QJsonObject jsonoption = optionsArray[i].toObject();
            Option option;
            if (jsonoption.contains("name") && jsonoption["name"].isString()) option.name = jsonoption["name"].toString();
            if (jsonoption.contains("flag") && jsonoption["flag"].isString()) option.flag = jsonoption["flag"].toString();
            if (jsonoption.contains("type") && jsonoption["type"].isString()) option.type = jsonoption["type"].toString();
            if (jsonoption.contains("value")) option.value = jsonoption["value"].toVariant();
            if (jsonoption.contains("default")) option.defaultvalue = jsonoption["default"].toVariant();
            if (jsonoption.contains("minimum")) option.minimum = jsonoption["minimum"].toVariant();
            if (jsonoption.contains("maximum")) option.maximum = jsonoption["maximum"].toVariant();
            if (jsonoption.contains("options") && jsonoption["options"].isArray()) {
                QJsonArray optionsArray = jsonoption["options"].toArray();
                for (const QJsonValue &opt : optionsArray) {
                    QJsonObject optObj = opt.toObject();
                    QString label;
                    QVariant optValue;
                    if (optObj.contains("label") && optObj["label"].isString())
                        label = optObj["label"].toString();
                    if (optObj.contains("value"))
                        optValue = optObj["value"].toVariant();
                    option.options.append(qMakePair(label, optValue));
                }
            }
            if (!option.name.isEmpty() && !option.type.isEmpty() && !option.defaultvalue.isValid() && !option.value.isValid()) {
                if (option.name.length() > 0) {
                    error = QString("Json for option: \"%1\" does not contain all required attributes").arg(option.name);
                } else {
                    error = QString("Json for option: %1 does not contain all required attributes").arg(i);
                }
                
                if (option.name.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("name");
                }
                
                if (option.type.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("type");
                }
                
                if (option.defaultvalue.isValid()) {
                    error += QString("\nMissing attribute: %1").arg("default");
                }
                
                if (option.value.isValid()) {
                    error += QString("\nMissing attribute: %1").arg("value");
                }
                valid = false;
                return valid;
            }
            else {
                if (!(option.type == "Slider" || option.type == "Dropdown" || option.type == "Text" || option.type == "File")) {
                    error = QString("Json for option: %1 contains an invalid type: %1, valid types are Slider, Dropdown, Text and File").arg(i);
                    valid = false;
                    return valid;
                }
            }
            options.append(option);
        }
    }
    if (json.contains("tasks") && json["tasks"].isArray()) {
        QJsonArray tasksArray = json["tasks"].toArray();
        for (int i = 0; i < tasksArray.size(); ++i) {
            QJsonObject jsontask = tasksArray[i].toObject();
            Task task;
            if (jsontask.contains("id") && jsontask["id"].isString()) task.id = jsontask["id"].toString();
            if (jsontask.contains("name") && jsontask["name"].isString()) task.name = jsontask["name"].toString();
            if (jsontask.contains("command") && jsontask["command"].isString()) task.command = jsontask["command"].toString();
            if (jsontask.contains("extension") && jsontask["extension"].isString()) task.extension = jsontask["extension"].toString();
            if (jsontask.contains("arguments") && jsontask["arguments"].isString()) task.arguments = jsontask["arguments"].toString();
            if (jsontask.contains("startin") && jsontask["startin"].isString()) task.startin = jsontask["startin"].toString();
            if (jsontask.contains("dependson") && jsontask["dependson"].isString()) task.dependson = jsontask["dependson"].toString();
            if (jsontask.contains("documentation") && jsontask["documentation"].isArray()) {
                QJsonArray docarray = jsontask["documentation"].toArray();
                for (int i = 0; i < docarray.size(); ++i) {
                    task.documentation.append(docarray[i].toString());
                }
            }
            if (!task.id.isEmpty() && !task.name.isEmpty() && !task.command.isEmpty() && !task.extension.isEmpty() && !task.arguments.isEmpty()) {
                if (task.dependson.length() > 0) {
                    bool found = false;
                    for (int j = 0; j < tasks.size(); ++j) {
                        if (tasks[j].id == task.dependson) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        error = QString("Json for task: \"%1\" contains a dependson id that can not be found").arg(task.name);
                        valid = false;
                        return valid;
                    }
                }
                tasks.append(task);
            } else {
                if (task.name.length() > 0) {
                    error = QString("Json for task: \"%1\" does not contain all required attributes").arg(task.name);
                } else {
                    error = QString("Json for task: %1 does not contain all required attributes").arg(i);
                }
                if (task.id.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("id");
                }
                
                if (task.name.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("name");
                }
                
                if (task.command.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("command");
                }
                
                if (task.extension.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("extension");
                }
                
                if (task.arguments.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("arguments");
                }
                valid = false;
                return valid;
            }
        }
    }
    valid = true;
    return valid;
}


Preset::Preset()
: p(new PresetPrivate())
{
    p->init();
}

Preset::~Preset()
{
}

bool
Preset::read(const QString& filename)
{
    p->filename = filename;
    return p->read();
}

bool
Preset::valid() const
{
    return p->valid;
}

QString
Preset::error() const
{
    return p->error;
}

QString
Preset::id() const
{
    return p->id;
}

QString
Preset::filename() const
{
    return p->filename;
}

QString
Preset::name() const
{
    return p->name;
}

QList<Option>
Preset::options() const
{
    return p->options;
}

QList<Task>
Preset::tasks() const
{
    return p->tasks;
}

QUuid
Preset::uuid() const
{
    return p->uuid;
}
