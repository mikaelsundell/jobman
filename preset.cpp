// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "preset.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QPointer>

#include <QDebug>

Option::Option(QObject* parent)
: QObject(parent)
{
}

Task::Task(QObject* parent)
: QObject(parent)
{
}

class PresetPrivate
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
        QList<Option*> options;
        QList<Task*> tasks;
        QUuid uuid;
        bool valid;
        QPointer<Preset> preset;
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
            Option* option = new Option(preset.data());
            if (jsonoption.contains("id") && jsonoption["id"].isString()) option->id = jsonoption["id"].toString();
            if (jsonoption.contains("name") && jsonoption["name"].isString()) option->name = jsonoption["name"].toString();
            if (jsonoption.contains("flag") && jsonoption["flag"].isString()) option->flag = jsonoption["flag"].toString();
            if (jsonoption.contains("type") && jsonoption["type"].isString()) option->type = jsonoption["type"].toString();
            if (jsonoption.contains("value")) option->value = jsonoption["value"].toVariant();
            if (jsonoption.contains("default")) option->defaultvalue = jsonoption["default"].toVariant();
            if (jsonoption.contains("minimum")) option->minimum = jsonoption["minimum"].toVariant();
            if (jsonoption.contains("maximum")) option->maximum = jsonoption["maximum"].toVariant();
            if (jsonoption.contains("switch")) option->switchvalue = jsonoption["switch"].toVariant();
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
                    option->options.append(qMakePair(label, optValue));
                }
            }
            if (option->options.count()) {
                bool hasdefault, hasvalue = false;
                for (const QPair<QString, QVariant>& pair : option->options) {
                    if (pair.second == option->defaultvalue) {
                        hasdefault = true;
                    }
                    if (pair.second == option->value) {
                        hasvalue = true;
                    }
                }
                if (!hasdefault) {
                    error = QString("Json for option: \"%1\" does not contain default value in options").arg(option->name);
                    valid = false;
                    return valid;
                }
                if (!hasvalue) {
                    error = QString("Json for option: \"%1\" does not contain value in options").arg(option->name);
                    valid = false;
                    return valid;
                }
            }
            if (!option->id.isEmpty() &&
                !option->name.isEmpty() &&
                !option->flag.isEmpty() &&
                !option->type.isEmpty() && 
                !option->defaultvalue.isValid() &&
                !option->value.isValid()) {
                if (option->id.isEmpty()) {
                    error += QString("\nMissing required attribute: %1").arg("id");
                }
                if (option->name.isEmpty()) {
                    error += QString("\nMissing required attribute: %1").arg("id");
                }
                if (option->flag.isEmpty()) {
                    error += QString("\nMissing required attribute: %1").arg("flag");
                }
                if (option->type.isEmpty()) {
                    error += QString("\nMissing required attribute: %1").arg("type");
                }
                if (option->defaultvalue.isValid()) {
                    error += QString("\nMissing required attribute: %1").arg("default");
                }
                if (option->value.isValid()) {
                    error += QString("\nMissing required attribute: %1").arg("value");
                }
                valid = false;
                return valid;
            }
            else {
                if (!(option->type.toLower() == "checkbox" || 
                      option->type.toLower() == "double" || 
                      option->type.toLower() == "doubleslider" || 
                      option->type.toLower() == "dropdown" ||
                      option->type.toLower() == "file" || 
                      option->type.toLower() == "int" ||
                      option->type.toLower() == "intslider" ||
                      option->type.toLower() == "text")) {
                    error = QString("Json for option: %1 contains an invalid type: %2, valid types are "
                                    "Checkbox, Double, DoubleSlider, File, Int, IntSlider, Dropdown and Text").arg(i+1).arg(option->type); // +1 for user readability
                    valid = false;
                    return valid;
                }
            }
            for (const Option* optionid : options) {
                if (optionid->id == option->id) {
                    error = QString("Json for option: %1 contain a duplicate id: %2").arg(i+1).arg(optionid->id);
                    valid = false;
                    return valid;
                }
            }
            if (option->minimum.isNull()) {
                option->minimum = 0;
            }
            if (option->maximum.isNull()) {
                option->maximum = 100;
            }
            if (option->switchvalue.isNull()) {
                option->switchvalue = false;
            }
            options.append(option);
        }
    }
    if (json.contains("tasks") && json["tasks"].isArray()) {
        QJsonArray tasksArray = json["tasks"].toArray();
        for (int i = 0; i < tasksArray.size(); ++i) {
            QJsonObject jsontask = tasksArray[i].toObject();
            Task* task = new Task(preset.data());
            if (jsontask.contains("id") && jsontask["id"].isString()) task->id = jsontask["id"].toString();
            if (jsontask.contains("name") && jsontask["name"].isString()) task->name = jsontask["name"].toString();
            if (jsontask.contains("command") && jsontask["command"].isString()) task->command = jsontask["command"].toString();
            if (jsontask.contains("extension") && jsontask["extension"].isString()) task->extension = jsontask["extension"].toString();
            if (jsontask.contains("output") && jsontask["output"].isString()) task->output = jsontask["output"].toString();
            if (jsontask.contains("arguments") && jsontask["arguments"].isString()) task->arguments = jsontask["arguments"].toString();
            if (jsontask.contains("startin") && jsontask["startin"].isString()) task->startin = jsontask["startin"].toString();
            if (jsontask.contains("dependson") && jsontask["dependson"].isString()) task->dependson = jsontask["dependson"].toString();
            if (jsontask.contains("documentation") && jsontask["documentation"].isArray()) {
                QJsonArray docarray = jsontask["documentation"].toArray();
                for (int i = 0; i < docarray.size(); ++i) {
                    task->documentation.append(docarray[i].toString());
                }
            }
            if (!task->id.isEmpty() && !task->name.isEmpty() && !task->command.isEmpty() && !task->extension.isEmpty() && !task->arguments.isEmpty()) {
                if (task->dependson.length() > 0) {
                    bool found = false;
                    for (int j = 0; j < tasks.size(); ++j) {
                        if (tasks[j]->id == task->dependson) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        error = QString("Json for task: \"%1\" contains a dependson id that can not be found").arg(task->name);
                        valid = false;
                        return valid;
                    }
                }
                tasks.append(task);
            } else {
                if (task->name.length() > 0) {
                    error = QString("Json for task: \"%1\" does not contain all required attributes").arg(task->name);
                } else {
                    error = QString("Json for task: %1 does not contain all required attributes").arg(i);
                }
                if (task->id.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("id");
                }
                if (task->name.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("name");
                }
                if (task->command.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("command");
                }
                if (task->extension.isEmpty()) {
                    error += QString("\nMissing attribute: %1").arg("extension");
                }
                if (task->arguments.isEmpty()) {
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
    p->preset = this;
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

QList<Option*>
Preset::options() const
{
    return p->options;
}

QList<Task*>
Preset::tasks() const
{
    return p->tasks;
}

QUuid
Preset::uuid() const
{
    return p->uuid;
}
