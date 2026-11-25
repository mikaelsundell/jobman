// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "preset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

Option::Option()
    : enabled(true)
{}

Option::~Option() {}

Task::Task() {}

Task::~Task() {}

namespace {
QString
jsonText(const QJsonValue& value)
{
    const QByteArray encoded = QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

class PresetValidation {
public:
    explicit PresetValidation(const QString& filename)
        : filename(filename)
    {}

    bool fail(const QString& context, const QString& message)
    {
        error = QString("Preset file: %1\n%2: %3").arg(filename, context, message);
        return false;
    }

    bool members(const QJsonObject& object, const QStringList& allowed, const QString& context)
    {
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!allowed.contains(it.key()))
                return fail(context + '.' + it.key(), "Unknown field.");
        }
        return true;
    }

    bool string(const QJsonObject& object, const QString& key, QString& result, const QString& context,
                bool required = false)
    {
        if (!object.contains(key))
            return !required || fail(context + '.' + key, "Required field is missing.");
        const auto value = object.value(key);
        if (!value.isString())
            return fail(context + '.' + key, "Expected a string; got " + jsonText(value) + '.');
        result = value.toString();
        if (required && result.trimmed().isEmpty())
            return fail(context + '.' + key, "Required string must not be empty or whitespace.");
        return true;
    }

    bool boolean(const QJsonValue& value, bool& result, const QString& context)
    {
        if (value.isBool()) {
            result = value.toBool();
            return true;
        }
        if (value.isString()) {
            const QString text = value.toString().toLower();
            if (text == "true" || text == "false") {
                result = text == "true";
                return true;
            }
        }
        return fail(context, "Expected true or false (a JSON boolean or string); got " + jsonText(value) + '.');
    }

    bool number(const QJsonValue& value, bool integer, double& result, const QString& context)
    {
        bool ok = value.isDouble();
        result = value.toDouble();
        if (value.isString()) {
            if (integer)
                result = static_cast<double>(value.toString().toInt(&ok));
            else
                result = value.toString().toDouble(&ok);
        }
        if (!ok || !std::isfinite(result)
            || (integer
                && (std::floor(result) != result || result < std::numeric_limits<int>::min()
                    || result > std::numeric_limits<int>::max()))) {
            return fail(context, QString("Expected a finite %1; got %2.")
                                     .arg(integer ? "32-bit integer" : "number", jsonText(value)));
        }
        return true;
    }

    bool scalar(const QJsonValue& value, const QString& context)
    {
        return value.isString() || value.isBool() || (value.isDouble() && std::isfinite(value.toDouble()))
               || fail(context, "Expected a string, number, or boolean; got " + jsonText(value) + '.');
    }

    bool variables(const QString& source, const QString& field, const QString& context,
                   const QList<QSharedPointer<Option>>& options, bool dependent)
    {
        const QStringList paths { "inputdir",  "inputfile",  "inputext",  "inputbase",
                                  "outputdir", "outputfile", "outputext", "outputbase" };
        const QRegularExpression reserved("%(?:[A-Za-z_][A-Za-z0-9_]*:|options|task|input|output)");
        auto matches = reserved.globalMatch(source);
        QSet<QString> references;
        while (matches.hasNext()) {
            const auto match = matches.next();
            const qsizetype end = source.indexOf('%', match.capturedStart() + 1);
            if (end < 0)
                return fail(context, "Unterminated placeholder beginning at " + match.captured() + '.');
            const QString token = source.mid(match.capturedStart() + 1, end - match.capturedStart() - 1);
            if (token.startsWith("options:")) {
                const QString id = token.mid(8);
                const auto found = std::find_if(options.cbegin(), options.cend(),
                                                [&](const auto& option) { return option->id == id; });
                if (found == options.cend())
                    return fail(context, QString("Placeholder %%%1% references an undefined option.").arg(token));
                if (field == "extension")
                    return fail(context, "Option placeholders are not supported in extension.");
                references.insert(id);
                if ((*found)->flagonly.toBool() && source != '%' + token + '%')
                    return fail(context, "A flagonly option must occupy its own argument or field.");
            }
            else if (token.startsWith("task:")) {
                if (field != "arguments" || (token != "task:input" && token != "task:output"))
                    return fail(context,
                                "Supported task placeholders are %task:input% and %task:output%, in arguments only.");
                if (token == "task:input" && !dependent)
                    return fail(context, "%task:input% requires dependson; use %inputfile% for the original input.");
            }
            else if (!paths.contains(token) || (field == "extension" && token.startsWith("output"))) {
                return fail(context, QString("Unsupported placeholder %%%1% in %2.").arg(token, field));
            }
        }
        if (references.size() > 1)
            return fail(context, "Only one distinct option placeholder is supported per argument token or field.");
        return true;
    }

    QString filename;
    QString error;
};
}  // namespace

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
    QString filter;
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
    // A failed read must not expose data or errors left over from a previous file.
    valid = false;
    error.clear();
    id.clear();
    name.clear();
    type.clear();
    filter.clear();
    description.clear();
    options.clear();
    tasks.clear();
    PresetValidation check(filename);
    const auto parse = [&]() -> bool {
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly))
            return check.fail("file", "Could not open for reading: " + file.errorString());
        const QByteArray bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            return check.fail("file", "Could not read: " + file.errorString());
        if (bytes.trimmed().isEmpty())
            return check.fail("JSON", "File is empty.");
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qsizetype line = 1, column = 1;
            for (qsizetype i = 0; i < parseError.offset && i < bytes.size(); ++i) {
                if (bytes[i] == '\n') {
                    ++line;
                    column = 1;
                }
                else {
                    ++column;
                }
            }
            return check.fail("JSON", QString("%1 at line %2, column %3 (byte offset %4).")
                                          .arg(parseError.errorString())
                                          .arg(line)
                                          .arg(column)
                                          .arg(parseError.offset));
        }
        if (!document.isObject())
            return check.fail("preset", "Expected a JSON object.");
        const QJsonObject root = document.object();
        if (!check.members(root, { "id", "name", "type", "filter", "description", "options", "tasks" }, "preset"))
            return false;
        QString parsedId, parsedName, parsedType, parsedFilter;
        if (!check.string(root, "id", parsedId, "preset", true)
            || !check.string(root, "name", parsedName, "preset", true)
            || !check.string(root, "type", parsedType, "preset")
            || !check.string(root, "filter", parsedFilter, "preset"))
            return false;
        parsedType = parsedType.isEmpty() ? "file" : parsedType.toLower();
        if (parsedType != "file" && parsedType != "command")
            return check.fail("preset.type", "Expected file or command; got " + jsonText(root.value("type")) + '.');
        if (parsedFilter.isEmpty())
            parsedFilter = "*.*";
        QStringList patterns = parsedFilter.split(';');
        for (QString& pattern : patterns) {
            pattern = pattern.trimmed();
            if (pattern.isEmpty())
                return check.fail("preset.filter", "Empty wildcard pattern in semicolon-separated filter.");
        }
        parsedFilter = patterns.join(';');
        QStringList parsedDescription;
        if (root.contains("description")) {
            const QJsonValue value = root.value("description");
            if (value.isString())
                parsedDescription.append(value.toString());
            else if (value.isArray()) {
                for (const auto& entry : value.toArray()) {
                    if (!entry.isString())
                        return check.fail("preset.description", "Expected a string or array of strings.");
                    parsedDescription.append(entry.toString());
                }
            }
            else {
                return check.fail("preset.description", "Expected a string or array of strings.");
            }
        }
        QList<QSharedPointer<Option>> parsedOptions;
        QSet<QString> optionIds;
        if (root.contains("options") && !root.value("options").isArray())
            return check.fail("preset.options", "Expected an array of option objects.");
        const auto optionArray = root.value("options").toArray();
        for (qsizetype i = 0; i < optionArray.size(); ++i) {
            QString context = QString("options[%1]").arg(i + 1);
            if (!optionArray[i].isObject())
                return check.fail(context, "Expected an option object.");
            const auto object = optionArray[i].toObject();
            if (!check.members(object,
                               { "id", "name", "description", "flag", "type", "toggle", "flagonly", "valueonly",
                                 "value", "default", "minimum", "maximum", "options" },
                               context))
                return false;
            QSharedPointer<Option> option(new Option());
            if (!check.string(object, "id", option->id, context, true))
                return false;
            context += QString(" (\"%1\")").arg(option->id);
            if (optionIds.contains(option->id))
                return check.fail(context + ".id", "Duplicate option id: " + option->id);
            optionIds.insert(option->id);
            if (!check.string(object, "name", option->name, context, true)
                || !check.string(object, "type", option->type, context, true)
                || !check.string(object, "description", option->description, context)
                || !check.string(object, "flag", option->flag, context)
                || !check.string(object, "toggle", option->toggle, context))
                return false;
            option->type = option->type.toLower();
            const QStringList types { "checkbox", "double",  "doubleslider",  "dropdown", "openfile",
                                      "savefile", "integer", "integerslider", "label",    "text" };
            if (!types.contains(option->type))
                return check.fail(context + ".type", "Unsupported type; expected one of: " + types.join(", ") + '.');
            bool flagOnly = false, valueOnly = false;
            if ((object.contains("flagonly")
                 && !check.boolean(object.value("flagonly"), flagOnly, context + ".flagonly"))
                || (object.contains("valueonly")
                    && !check.boolean(object.value("valueonly"), valueOnly, context + ".valueonly")))
                return false;
            if (flagOnly && valueOnly)
                return check.fail(context, "flagonly and valueonly cannot both be true.");
            if (flagOnly && option->flag.trimmed().isEmpty())
                return check.fail(context + ".flag", "flagonly requires a nonempty flag.");
            option->flagonly = flagOnly;
            option->valueonly = valueOnly;
            if (!object.contains("value"))
                return check.fail(context + ".value", "Required field is missing; default does not replace value.");
            if (!check.scalar(object.value("value"), context + ".value"))
                return false;
            option->value = object.value("value").toVariant();
            const QJsonValue reset = object.contains("default") ? object.value("default") : object.value("value");
            if (!check.scalar(reset, context + ".default"))
                return false;
            option->defaultvalue = reset.toVariant();
            const bool integer = option->type == "integer" || option->type == "integerslider";
            const bool numeric = integer || option->type == "double" || option->type == "doubleslider";
            if (numeric) {
                double minimum = 0, maximum = 100, value = 0, defaultValue = 0;
                if ((object.contains("minimum")
                     && !check.number(object.value("minimum"), integer, minimum, context + ".minimum"))
                    || (object.contains("maximum")
                        && !check.number(object.value("maximum"), integer, maximum, context + ".maximum"))
                    || !check.number(object.value("value"), integer, value, context + ".value")
                    || !check.number(reset, integer, defaultValue, context + ".default"))
                    return false;
                if (minimum > maximum)
                    return check.fail(context + ".minimum", "Minimum must not exceed maximum.");
                if (option->type == "doubleslider" && (minimum == maximum || !std::isfinite(maximum - minimum)))
                    return check.fail(context, "doubleslider requires a positive, finite range (maximum - minimum).");
                if (value < minimum || value > maximum)
                    return check.fail(context + ".value", QString("%1 is outside [%2, %3].")
                                                              .arg(jsonText(object.value("value")))
                                                              .arg(minimum)
                                                              .arg(maximum));
                if (defaultValue < minimum || defaultValue > maximum)
                    return check.fail(context + ".default",
                                      QString("%1 is outside [%2, %3].").arg(jsonText(reset)).arg(minimum).arg(maximum));
                option->minimum = integer ? QVariant(static_cast<int>(minimum)) : QVariant(minimum);
                option->maximum = integer ? QVariant(static_cast<int>(maximum)) : QVariant(maximum);
            }
            else {
                if (object.contains("minimum") || object.contains("maximum"))
                    return check.fail(context, "minimum and maximum are only supported for numeric option types.");
                option->minimum = 0;
                option->maximum = 100;
            }
            if (option->type == "checkbox") {
                bool value, defaultValue;
                if (!check.boolean(object.value("value"), value, context + ".value")
                    || !check.boolean(reset, defaultValue, context + ".default"))
                    return false;
                option->value = value;
                option->defaultvalue = defaultValue;
            }
            if ((option->type == "text" || option->type == "label" || option->type == "openfile"
                 || option->type == "savefile")
                && (!object.value("value").isString() || !reset.isString()))
                return check.fail(context,
                                  "value and default must be strings for text, label, openfile, and savefile options.");
            if (option->type == "dropdown") {
                const auto choices = object.value("options");
                if (!choices.isArray() || choices.toArray().isEmpty())
                    return check.fail(context + ".options", "Dropdown requires a nonempty array of choices.");
                QStringList allowed;
                for (qsizetype j = 0; j < choices.toArray().size(); ++j) {
                    const QString choiceContext = context + QString(".options[%1]").arg(j + 1);
                    const auto entry = choices.toArray()[j];
                    if (!entry.isObject())
                        return check.fail(choiceContext, "Expected a choice object.");
                    const auto choice = entry.toObject();
                    if (!check.members(choice, { "label", "value" }, choiceContext))
                        return false;
                    if (!choice.contains("value"))
                        return check.fail(choiceContext + ".value", "Required field is missing.");
                    if (!check.scalar(choice.value("value"), choiceContext + ".value"))
                        return false;
                    QString label = choice.value("value").toVariant().toString();
                    if (!check.string(choice, "label", label, choiceContext))
                        return false;
                    const QVariant value = choice.value("value").toVariant();
                    for (const auto& previous : option->options) {
                        if (previous.second == value)
                            return check.fail(choiceContext + ".value",
                                              "Duplicate dropdown value: " + jsonText(choice.value("value")));
                    }
                    option->options.append(qMakePair(label, value));
                    allowed.append(jsonText(choice.value("value")));
                }
                const auto listed = [&](const QVariant& value) {
                    return std::any_of(option->options.cbegin(), option->options.cend(),
                                       [&](const auto& choice) { return choice.second == value; });
                };
                if (!listed(option->defaultvalue))
                    return check.fail(context + ".default",
                                      jsonText(reset) + " is not a listed choice. Allowed values: " + allowed.join(", ")
                                          + '.');
                if (!listed(option->value))
                    return check.fail(context + ".value",
                                      jsonText(object.value("value"))
                                          + " is not a listed choice. Allowed values: " + allowed.join(", ") + '.');
            }
            else if (object.contains("options")) {
                return check.fail(context + ".options", "Choice lists are only supported for dropdown options.");
            }
            option->enabled = option->toggle.isEmpty();
            parsedOptions.append(option);
        }
        if (!root.value("tasks").isArray() || root.value("tasks").toArray().isEmpty())
            return check.fail("preset.tasks", "Required nonempty array of task objects is missing or invalid.");
        struct TaskRecord {
            QSharedPointer<Task> task;
            QString context;
        };
        QList<TaskRecord> records;
        QSet<QString> taskIds;
        const auto taskArray = root.value("tasks").toArray();
        for (qsizetype i = 0; i < taskArray.size(); ++i) {
            QString context = QString("tasks[%1]").arg(i + 1);
            if (!taskArray[i].isObject())
                return check.fail(context, "Expected a task object.");
            const auto object = taskArray[i].toObject();
            if (!check.members(object,
                               { "id", "name", "command", "extension", "output", "arguments", "startin", "dependson",
                                 "documentation", "exclusive" },
                               context))
                return false;
            QSharedPointer<Task> task(new Task());
            if (!check.string(object, "id", task->id, context, true))
                return false;
            context += QString(" (\"%1\")").arg(task->id);
            if (taskIds.contains(task->id))
                return check.fail(context + ".id", "Duplicate task id: " + task->id);
            taskIds.insert(task->id);
            if (!check.string(object, "name", task->name, context, true)
                || !check.string(object, "command", task->command, context, true)
                || !check.string(object, "extension", task->extension, context, true)
                || !check.string(object, "output", task->output, context, true)
                || !check.string(object, "arguments", task->arguments, context, true)
                || !check.string(object, "startin", task->startin, context)
                || !check.string(object, "dependson", task->dependson, context))
                return false;
            if (object.contains("documentation")) {
                if (!object.value("documentation").isArray())
                    return check.fail(context + ".documentation", "Expected an array of strings.");
                for (const auto& entry : object.value("documentation").toArray()) {
                    if (!entry.isString())
                        return check.fail(context + ".documentation", "Every documentation entry must be a string.");
                    task->documentation.append(entry.toString());
                }
            }
            bool exclusive = false;
            if (object.contains("exclusive")
                && !check.boolean(object.value("exclusive"), exclusive, context + ".exclusive"))
                return false;
            task->exclusive = exclusive;
            records.append({ task, context });
        }
        QMap<QString, QString> parents;
        for (const auto& record : records) {
            const auto& dependency = record.task->dependson;
            if (!dependency.isEmpty() && !taskIds.contains(dependency))
                return check.fail(record.context + ".dependson", "Undefined task id: " + dependency);
            if (dependency == record.task->id)
                return check.fail(record.context + ".dependson", "A task cannot depend on itself.");
            parents.insert(record.task->id, dependency);
        }
        QSet<QString> visited;
        for (const auto& record : records) {
            QString current = record.task->id;
            QStringList chain;
            QSet<QString> path;
            while (!current.isEmpty() && !visited.contains(current)) {
                if (path.contains(current))
                    return check.fail(record.context + ".dependson",
                                      "Dependency cycle: " + (chain + QStringList { current }).join(" -> "));
                path.insert(current);
                chain.append(current);
                current = parents.value(current);
            }
            for (const auto& member : chain)
                visited.insert(member);
        }
        std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.task->id < b.task->id; });
        QList<QSharedPointer<Task>> parsedTasks;
        for (const auto& record : records) {
            const auto& task = record.task;
            if (!task->dependson.isEmpty() && task->dependson >= task->id)
                return check.fail(
                    record.context + ".dependson",
                    "Dependency must sort before this task by string id; use ordered ids such as 01, 02, 03.");
            for (const auto& field : { QString("command"), QString("extension"), QString("output"), QString("startin"),
                                       QString("arguments") }) {
                const QString source = field == "command"     ? task->command
                                       : field == "extension" ? task->extension
                                       : field == "output"    ? task->output
                                       : field == "startin"   ? task->startin
                                                              : task->arguments;
                const auto tokens = field == "arguments" ? source.split(' ', Qt::SkipEmptyParts)
                                                         : QStringList { source };
                for (const auto& token : tokens) {
                    if (!check.variables(token, field, record.context + '.' + field, parsedOptions,
                                         !task->dependson.isEmpty()))
                        return false;
                }
            }
            parsedTasks.append(task);
        }
        // Commit only a completely validated definition.
        id = parsedId;
        name = parsedName;
        type = parsedType;
        filter = parsedFilter;
        description = parsedDescription;
        options = parsedOptions;
        tasks = parsedTasks;
        return true;
    };
    valid = parse();
    error = check.error;
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

QString
Preset::filter() const
{
    return p->filter;
}

bool
Preset::hasOption(const QString& id) const
{
    for (const QSharedPointer<Option>& option : options()) {
        if (option->id == id) {
            return true;
        }
    }
    return false;
}

QSharedPointer<Option>
Preset::option(const QString& id) const
{
    for (const QSharedPointer<Option>& option : options()) {
        if (option->id == id) {
            return option;
        }
    }
    return QSharedPointer<Option>();
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
