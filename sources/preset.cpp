// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "preset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

Option::Option() {}

Option::~Option() {}

Task::Task() {}

Task::~Task() {}

class PresetPrivate {
public:
    PresetPrivate();
    void init();
    bool read();

public:
    QString id;
    QString error;
    QString filename;
    QString name;
    QString type;
    QList<QString> description;
    QList<QSharedPointer<Option>> options;
    QList<QSharedPointer<Task>> tasks;
    QUuid uuid;
    bool valid;
    QPointer<Preset> preset;
};

PresetPrivate::PresetPrivate()
    : valid(false)
    , uuid(QUuid::createUuid())
{}

void
PresetPrivate::init()
{}

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
    if (jsonData.size() == 0) {
        error = QString("Parse error:\n"
                        "File is empty");
        valid = false;
        return valid;
    }
    file.close();
    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(jsonData, &jsonError);
    if (document.isNull()) {
        int line = 1;  // get line and column
        int column = 1;
        for (int i = 0; i < jsonError.offset && i < jsonData.size(); ++i) {
            if (jsonData[i] == '\n') {
                ++line;
                column = 1;
            }
            else {
                ++column;
            }
        }
        error = QString("Parse error:\n"
                        "%1 at line %2, column %3 (offset %4)")
                    .arg(jsonError.errorString())
                    .arg(line)
                    .arg(column)
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
    else {
        error = QString("Json contains no a name").arg(filename);
        valid = false;
        return valid;
    }
    if (json.contains("type") && json["type"].isString()) {
        type = json["type"].toString();
    }
    if (!type.length()) {
        type = "file";
    }
    if (json.contains("options") && json["options"].isArray()) {
        QJsonArray optionsArray = json["options"].toArray();
        for (int i = 0; i < optionsArray.size(); ++i) {
            QJsonObject jsonoption = optionsArray[i].toObject();
            QSharedPointer<Option> option(new Option());
            if (jsonoption.contains("id") && jsonoption["id"].isString())
                option->id = jsonoption["id"].toString();
            if (jsonoption.contains("name") && jsonoption["name"].isString())
                option->name = jsonoption["name"].toString();
            if (jsonoption.contains("description") && jsonoption["description"].isString())
                option->description = jsonoption["description"].toString();
            if (jsonoption.contains("flag") && jsonoption["flag"].isString())
                option->flag = jsonoption["flag"].toString();
            if (jsonoption.contains("type") && jsonoption["type"].isString())
                option->type = jsonoption["type"].toString();
            if (jsonoption.contains("toggle") && jsonoption["toggle"].isString())
                option->toggle = jsonoption["toggle"].toString();
            if (jsonoption.contains("flagonly"))
                option->flagonly = jsonoption["flagonly"].toVariant();
            if (jsonoption.contains("value"))
                option->value = jsonoption["value"].toVariant();
            if (jsonoption.contains("default"))
                option->defaultvalue = jsonoption["default"].toVariant();
            if (jsonoption.contains("minimum"))
                option->minimum = jsonoption["minimum"].toVariant();
            if (jsonoption.contains("maximum"))
                option->maximum = jsonoption["maximum"].toVariant();
            if (jsonoption.contains("options") && jsonoption["options"].isArray()) {
                QJsonArray optionsArray = jsonoption["options"].toArray();
                int i = 0;
                for (const QJsonValue& opt : optionsArray) {
                    QJsonObject optObj = opt.toObject();
                    QString label;
                    QVariant optValue;
                    if (optObj.contains("value")) {
                        optValue = optObj["value"].toVariant();
                    }
                    else {
                        error = QString("Json for option: \"%1\" in list contains no value").arg(i);
                        valid = false;
                        return valid;
                    }
                    if (optObj.contains("label") && optObj["label"].isString())
                        label = optObj["label"].toString();
                    else
                        label = optValue.toString();  // use value as label if not set
                    option->options.append(qMakePair(label, optValue));
                    i++;
                }
            }
            if (option->id.isEmpty() || option->name.isEmpty() || option->type.isEmpty()) {
                QList<QString> attributes;
                if (option->id.isEmpty()) {
                    attributes.append("id");
                }
                if (option->name.isEmpty()) {
                    attributes.append("name");
                }
                if (option->type.isEmpty()) {
                    attributes.append("type");
                }
                if (!option->name.isEmpty()) {
                    error = QString("Json for option: \"%1\" is missing required attributes: %2\n")
                                .arg(option->name)
                                .arg(attributes.join(", "));
                }
                else {
                    error = QString("Json for option is missing required attributes\n"
                                    "Attributes: %1")
                                .arg(attributes.join(", "));
                }
                valid = false;
                return valid;
            }
            else {
                if (!(option->type.toLower() == "checkbox" || option->type.toLower() == "double"
                      || option->type.toLower() == "doubleslider" || option->type.toLower() == "dropdown"
                      || option->type.toLower() == "file" || option->type.toLower() == "int"
                      || option->type.toLower() == "intslider" || option->type.toLower() == "label")
                    || option->type.toLower() == "text") {
                    error = QString("Json for option: %1 contains an invalid type: %2, valid types are "
                                    "Checkbox, Double, DoubleSlider, File, Int, IntSlider, Dropdown and Text")
                                .arg(i + 1)
                                .arg(option->type);  // +1 for user readability
                    valid = false;
                    return valid;
                }
                else {
                    // default and value required for non file and text
                    if (!(option->type.toLower() == "file" || option->type.toLower() == "text"
                          || option->type.toLower() == "label")) {
                        if (!option->defaultvalue.isValid() || !option->value.isValid()) {
                            QList<QString> attributes;
                            if (!option->defaultvalue.isValid()) {
                                attributes.append("defaultvalue");
                            }
                            if (!option->value.isValid()) {
                                attributes.append("value");
                            }
                            error
                                = QString(
                                      "Json for option: \"%1\" is missing required attributes for non field or text type\n"
                                      "Attributes: %2")
                                      .arg(option->name)
                                      .arg(attributes.join(", "));
                        }
                    }
                }
            }
            if (option->flagonly.isNull()) {
                option->flagonly = false;
            }
            else {
                option->flagonly = true;
            }
            if (option->minimum.isNull()) {
                option->minimum = 0;
            }
            if (option->maximum.isNull()) {
                option->maximum = 100;
            }
            if (option->defaultvalue.isNull()) {
                option->defaultvalue = option->value;
            }
            if (option->toggle.isEmpty()) {
                option->enabled = true;
            }
            else {
                option->enabled = false;
            }
            bool hasdefault = true, hasvalue = true;
            for (const QPair<QString, QVariant>& pair : option->options) {
                if (pair.second == option->defaultvalue) {
                    hasdefault = true;
                }
                if (pair.second == option->value) {
                    hasvalue = true;
                }
            }
            if (!hasdefault) {
                error
                    = QString(
                          "Invalid default value for option \"%1\": The specified default value is not listed in its options.")
                          .arg(option->name);
                valid = false;
                return valid;
            }
            if (!hasvalue) {
                error = QString("Invalid value for option \"%1\": The specified value is not listed in its options.")
                            .arg(option->name);
                valid = false;
                return valid;
            }
            for (QSharedPointer<Option> other : options) {
                if (other->id == option->id) {
                    error = QString("Json for option: %1 contain a duplicate id: %2").arg(i + 1).arg(other->id);
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
            QSharedPointer<Task> task(new Task());
            if (jsontask.contains("id") && jsontask["id"].isString())
                task->id = jsontask["id"].toString();
            if (jsontask.contains("name") && jsontask["name"].isString())
                task->name = jsontask["name"].toString();
            if (jsontask.contains("command") && jsontask["command"].isString())
                task->command = jsontask["command"].toString();
            if (jsontask.contains("extension") && jsontask["extension"].isString())
                task->extension = jsontask["extension"].toString();
            if (jsontask.contains("output") && jsontask["output"].isString())
                task->output = jsontask["output"].toString();
            if (jsontask.contains("arguments") && jsontask["arguments"].isString())
                task->arguments = jsontask["arguments"].toString();
            if (jsontask.contains("startin") && jsontask["startin"].isString())
                task->startin = jsontask["startin"].toString();
            if (jsontask.contains("dependson") && jsontask["dependson"].isString())
                task->dependson = jsontask["dependson"].toString();
            if (jsontask.contains("documentation") && jsontask["documentation"].isArray()) {
                QJsonArray docarray = jsontask["documentation"].toArray();
                for (int i = 0; i < docarray.size(); ++i) {
                    task->documentation.append(docarray[i].toString());
                }
            }
            if (!task->id.isEmpty() && !task->name.isEmpty() && !task->command.isEmpty() && !task->extension.isEmpty()
                && !task->arguments.isEmpty()) {
                if (task->dependson.length() > 0) {
                    bool found = false;
                    for (int j = 0; j < tasks.size(); ++j) {
                        if (tasks[j]->id == task->dependson) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        error = QString("Json for task: \"%1\" contains a dependson id that can not be found")
                                    .arg(task->name);
                        valid = false;
                        return valid;
                    }
                }
                tasks.append(task);
            }
            else {
                if (task->name.length() > 0) {
                    error = QString("Json for task: \"%1\" does not contain all required attributes").arg(task->name);
                }
                else {
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
    else {
        error = QString("Json contains no tasks");
        valid = false;
        return valid;
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

Preset::~Preset() {}

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

QString
Preset::type() const
{
    return p->type;
}

QList<QSharedPointer<Option>>
Preset::options() const
{
    return p->options;
}

QList<QSharedPointer<Task>>
Preset::tasks() const
{
    return p->tasks;
}

QUuid
Preset::uuid() const
{
    return p->uuid;
}
