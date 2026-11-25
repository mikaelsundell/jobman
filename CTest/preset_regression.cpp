// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "preset.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <stdexcept>

namespace {
void
require(bool condition, const QString& message)
{
    if (!condition)
        throw std::runtime_error(message.toStdString());
}

QJsonObject
task(const QString& id = "01")
{
    return { { "id", id },
             { "name", "Convert" },
             { "command", "fixture" },
             { "extension", "out" },
             { "output", "%outputdir%/%outputbase%.%outputext%" },
             { "arguments", "%inputfile% --out %task:output%" } };
}

QJsonObject
option(const QString& id = "quality")
{
    return { { "id", id },      { "name", "Quality" },   { "type", "integer" }, { "value", 80 },
             { "default", 70 }, { "flag", "--quality" }, { "minimum", 0 },      { "maximum", 100 } };
}

QJsonObject
root()
{
    return { { "id", "fixture" }, { "name", "Fixture" }, { "tasks", QJsonArray { task() } } };
}

struct Fixture {
    QTemporaryDir directory;
    Preset preset;
    int checks = 0;

    bool read(const QByteArray& bytes)
    {
        QFile file(directory.filePath("fixture.json"));
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Could not write fixture");
        file.close();
        return preset.read(file.fileName());
    }

    void check(const QJsonObject& object, bool valid, const QStringList& fragments = {})
    {
        ++checks;
        const bool accepted = read(QJsonDocument(object).toJson());
        require(accepted == valid && preset.valid() == valid,
                QString("Case %1: acceptance mismatch: %2").arg(checks).arg(preset.error()));
        if (valid) {
            require(preset.error().isEmpty(), "A valid preset retained an error: " + preset.error());
        }
        else {
            require(preset.error().contains(directory.filePath("fixture.json")), "Error omitted the preset path");
            for (const auto& fragment : fragments)
                require(preset.error().contains(fragment), "Error omitted " + fragment + ": " + preset.error());
            require(preset.options().isEmpty() && preset.tasks().isEmpty() && preset.id().isEmpty(),
                    "A failed read exposed a partial or stale definition");
        }
    }

    void withOption(const QJsonObject& value, bool valid, const QStringList& fragments = {})
    {
        auto document = root();
        document["options"] = QJsonArray { value };
        check(document, valid, fragments);
    }

    void withTask(const QJsonObject& value, bool valid, const QStringList& fragments = {})
    {
        auto document = root();
        document["tasks"] = QJsonArray { value };
        check(document, valid, fragments);
    }
};

void
run(const QString& name, const QString& source, Fixture& f)
{
    if (name == "preset_json") {
        require(!f.preset.read(f.directory.filePath("missing.json")) && f.preset.error().contains("Could not open"),
                "Missing file did not report the filesystem error");
        for (const auto& bytes : { QByteArray(), QByteArray(" \n"), QByteArray("[]"), QByteArray("{\n\"id\":,\n}") }) {
            require(!f.read(bytes) && !f.preset.error().isEmpty(), "Malformed JSON was accepted");
            if (bytes.contains("\"id\""))
                require(f.preset.error().contains("line 2") && f.preset.error().contains("column")
                            && f.preset.error().contains("offset"),
                        "Parse error omitted its location");
        }
    }
    else if (name == "preset_structure") {
        f.check(root(), true);
        for (const auto& key : { "id", "name" }) {
            auto value = root();
            value.remove(key);
            f.check(value, false, { QString("preset.") + key, "missing" });
            value = root();
            value[key] = 3;
            f.check(value, false, { QString("preset.") + key, "string" });
            value[key] = "  ";
            f.check(value, false, { "empty" });
        }
        for (const auto& value : { QJsonValue("savefile"), QJsonValue(4), QJsonValue(QJsonValue::Null) }) {
            auto document = root();
            document["type"] = value;
            f.check(document, false, { "preset.type" });
        }
        for (const auto& key : { "options", "tasks" }) {
            auto value = root();
            value[key] = QJsonObject();
            f.check(value, false, { key, "array" });
            value[key] = QJsonArray { 4 };
            f.check(value, false, { key, "object" });
        }
        auto value = root();
        value["tasks"] = QJsonArray();
        f.check(value, false, { "tasks", "nonempty" });
        value = root();
        value["task"] = QJsonArray();
        f.check(value, false, { "preset.task", "Unknown" });
        value = root();
        value["filter"] = 4;
        f.check(value, false, { "preset.filter", "string" });
        value["filter"] = "*.png;;*.jpg";
        f.check(value, false, { "filter", "Empty" });
        value["filter"] = " *.png ; *.jpg ";
        f.check(value, true);
        require(f.preset.filter() == "*.png;*.jpg", "Filter whitespace was not normalized");
        value = root();
        value["description"] = QJsonArray { "A", "B" };
        f.check(value, true);
        value["description"] = QJsonArray { 4 };
        f.check(value, false, { "description", "strings" });
    }
    else if (name == "preset_options") {
        f.withOption(option(), true);
        for (const auto& key : { "id", "name", "type", "value" }) {
            auto value = option();
            value.remove(key);
            f.withOption(value, false, { key, "missing" });
        }
        auto value = option();
        value["type"] = "int";
        f.withOption(value, false, { "type", "integer" });
        value = option();
        value["flag"] = QJsonArray();
        f.withOption(value, false, { "flag", "string" });
        value = option();
        value["values"] = 2;
        f.withOption(value, false, { "values", "Unknown" });
        value = option();
        value["flagonly"] = true;
        value["valueonly"] = true;
        f.withOption(value, false, { "flagonly", "valueonly" });
        value = option();
        value["flagonly"] = true;
        value.remove("flag");
        f.withOption(value, false, { "flag" });
        value = option();
        value["flagonly"] = "false";
        value["toggle"] = "Enable";
        value.remove("default");
        f.withOption(value, true);
        require(!f.preset.options().first()->enabled && f.preset.options().first()->defaultvalue == 80,
                "Optional reset default or toggle behavior changed");
        value["flagonly"] = "no";
        f.withOption(value, false, { "flagonly", "true or false" });
        auto document = root();
        document["options"] = QJsonArray { option(), option() };
        f.check(document, false, { "options[2]", "quality", "Duplicate" });
        for (const auto& type : { "text", "label", "openfile", "savefile" }) {
            QJsonObject text { { "id", "path" }, { "name", "Path" }, { "type", type }, { "value", "" } };
            f.withOption(text, true);
            text["value"] = 4;
            f.withOption(text, false, { "strings" });
        }
    }
    else if (name == "preset_dropdown") {
        QJsonObject value { { "id", "width" },
                            { "name", "Width" },
                            { "type", "dropdown" },
                            { "value", "400" },
                            { "default", "200" },
                            { "options", QJsonArray { QJsonObject { { "value", 200 } },
                                                      QJsonObject { { "label", "Medium" }, { "value", 400 } } } } };
        f.withOption(value, true);
        auto invalid = value;
        invalid["default"] = 999;
        f.withOption(invalid, false, { "width", ".default", "999", "Allowed values: 200, 400" });
        invalid = value;
        invalid["value"] = 999;
        f.withOption(invalid, false, { ".value", "999", "Allowed values" });
        invalid = value;
        invalid.remove("value");
        f.withOption(invalid, false, { ".value", "default does not replace" });
        for (const auto& choices :
             { QJsonValue(QJsonArray()), QJsonValue(QJsonObject()), QJsonValue(QJsonValue::Null) }) {
            invalid = value;
            invalid["options"] = choices;
            f.withOption(invalid, false, { ".options", "nonempty" });
        }
        for (const auto& choice : { QJsonValue(4), QJsonValue(QJsonObject { { "label", "missing" } }),
                                    QJsonValue(QJsonObject { { "value", QJsonValue::Null } }),
                                    QJsonValue(QJsonObject { { "value", 200 }, { "label", false } }) }) {
            invalid = value;
            invalid["options"] = QJsonArray { choice };
            f.withOption(invalid, false, { ".options[1]" });
        }
        invalid = value;
        invalid["options"] = QJsonArray { QJsonObject { { "value", 200 } }, QJsonObject { { "value", "200" } } };
        f.withOption(invalid, false, { "Duplicate dropdown value" });
    }
    else if (name == "preset_values") {
        for (const auto& value :
             { QJsonValue(1.5), QJsonValue("bad"), QJsonValue("1.0"), QJsonValue(true), QJsonValue(2147483648.0) }) {
            auto invalid = option();
            invalid["value"] = value;
            f.withOption(invalid, false, { ".value", "integer" });
        }
        auto value = option();
        value["value"] = 101;
        f.withOption(value, false, { ".value", "outside" });
        value = option();
        value["default"] = -1;
        f.withOption(value, false, { ".default", "outside" });
        value = option();
        value["minimum"] = 200;
        f.withOption(value, false, { "Minimum", "maximum" });
        value = option();
        value["maximum"] = "bad";
        f.withOption(value, false, { ".maximum" });
        value = option();
        value["value"] = "80";
        f.withOption(value, true);
        for (const auto& type : { "double", "doubleslider" }) {
            value = option();
            value["type"] = type;
            value["value"] = "1.25";
            f.withOption(value, true);
            value["default"] = "nan";
            f.withOption(value, false, { ".default", "finite" });
        }
        value = option();
        value["type"] = "doubleslider";
        value["value"] = 0;
        value["default"] = 0;
        value["maximum"] = 0;
        f.withOption(value, false, { "positive, finite range" });
        value["minimum"] = -1e308;
        value["maximum"] = 1e308;
        f.withOption(value, false, { "positive, finite range" });
        QJsonObject checkbox { { "id", "enabled" },
                               { "name", "Enabled" },
                               { "type", "checkbox" },
                               { "value", "true" },
                               { "default", false } };
        f.withOption(checkbox, true);
        checkbox["value"] = "yes";
        f.withOption(checkbox, false, { "true or false" });
        checkbox["value"] = QJsonValue::Null;
        f.withOption(checkbox, false, { "null" });
    }
    else if (name == "preset_tasks") {
        for (const auto& key : { "id", "name", "command", "extension", "output", "arguments" }) {
            auto value = task();
            value.remove(key);
            f.withTask(value, false, { key, "missing" });
            value = task();
            value[key] = false;
            f.withTask(value, false, { key, "string" });
        }
        auto value = task();
        value["exclusive"] = "true";
        f.withTask(value, true);
        value["exclusive"] = 1;
        f.withTask(value, false, { "exclusive", "true or false" });
        value = task();
        value["documentation"] = "text";
        f.withTask(value, false, { "documentation", "array" });
        value["documentation"] = QJsonArray { false };
        f.withTask(value, false, { "documentation", "string" });
        value = task();
        value["dependson"] = "missing";
        f.withTask(value, false, { "Undefined task id", "missing" });
        value["dependson"] = "01";
        f.withTask(value, false, { "itself" });
        auto document = root();
        document["tasks"] = QJsonArray { task(), task() };
        f.check(document, false, { "Duplicate task id" });
        auto a = task("01"), b = task("02"), c = task("03");
        a["dependson"] = "02";
        b["dependson"] = "01";
        document["tasks"] = QJsonArray { a, b };
        f.check(document, false, { "Dependency cycle", "01", "02" });
        b.remove("dependson");
        document["tasks"] = QJsonArray { a, b };
        f.check(document, false, { "sort before", "01, 02, 03" });
        a.remove("dependson");
        b["dependson"] = "01";
        c["dependson"] = "02";
        document["tasks"] = QJsonArray { c, b, a };
        f.check(document, true);
        require(f.preset.tasks()[0]->id == "01" && f.preset.tasks()[2]->id == "03", "Task string-id ordering changed");
    }
    else if (name == "preset_variables") {
        for (const auto& arguments : { "%options:missing%", "%options:missing", "%option:missing%", "%task:bad%",
                                       "%task:input%", "%inputtypo%" }) {
            auto value = task();
            value["arguments"] = arguments;
            f.withTask(value, false, { ".arguments" });
        }
        auto value = task();
        value["output"] = "%task:output%";
        f.withTask(value, false, { ".output", "arguments only" });
        value = task();
        value["extension"] = "%outputext%";
        f.withTask(value, false, { ".extension" });
        auto document = root();
        document["options"] = QJsonArray { option("a"), option("b") };
        value = task();
        value["arguments"] = "%options:a%%options:b%";
        document["tasks"] = QJsonArray { value };
        f.check(document, false, { "one distinct option" });
        value["arguments"] = "%options:a%  %options:b% %inputfile% --out %task:output%";
        document["tasks"] = QJsonArray { value };
        f.check(document, true);
    }
    else if (name == "preset_reuse") {
        auto a = root();
        a["options"] = QJsonArray { option() };
        a["filter"] = "*.png";
        f.check(a, true);
        auto b = root();
        b["id"] = "second";
        b["type"] = "command";
        f.check(b, true);
        require(f.preset.options().isEmpty() && f.preset.tasks().size() == 1 && f.preset.filter() == "*.*"
                    && f.preset.id() == "second" && f.preset.type() == "command",
                "Successful reread retained old fields");
        b["options"] = QJsonArray { option(), QJsonObject() };
        f.check(b, false, { "options[2]" });
        f.check(root(), true);
        require(!f.preset.read(f.directory.filePath("missing.json")) && f.preset.tasks().isEmpty(),
                "Failed reread retained old tasks");
    }
    else if (name == "preset_examples") {
        QDir presets(source + "/presets");
        for (const auto& filename : presets.entryList({ "*.json" }, QDir::Files)) {
            require(f.preset.read(presets.filePath(filename)), "Built-in preset: " + f.preset.error());
        }
        QFile readme(source + "/README.md");
        require(readme.open(QIODevice::ReadOnly), "README could not be read");
        const QString markdown = QString::fromUtf8(readme.readAll());
        const QRegularExpression blocks("```json\\s*\\n([\\s\\S]*?)\\n```");
        auto matches = blocks.globalMatch(markdown);
        int examples = 0;
        while (matches.hasNext()) {
            const auto text = matches.next().captured(1).toUtf8();
            const auto value = QJsonDocument::fromJson(text);
            require(!value.isNull(), "README contains invalid JSON");
            if (value.object().contains("tasks")) {
                require(f.read(text), "README preset: " + f.preset.error());
                ++examples;
            }
        }
        require(examples > 0, "README had no complete preset example");
        QFile tools(source + "/sources/mcptools.cpp");
        require(tools.open(QIODevice::ReadOnly), "MCP tool definitions missing");
        const QRegularExpression names("definition\\(\\s*\"(jobman_[a-z_]+)\"");
        matches = names.globalMatch(QString::fromUtf8(tools.readAll()));
        int count = 0;
        while (matches.hasNext()) {
            const auto tool = matches.next().captured(1);
            require(markdown.contains(tool), "README omitted " + tool);
            ++count;
        }
        require(count >= 14, "MCP tool documentation check found no definitions");
    }
    else {
        throw std::runtime_error("Unknown preset regression case");
    }
}
}  // namespace

int
main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        Fixture fixture;
        run(application.arguments().value(1), application.arguments().value(2), fixture);
        qInfo() << "PASS" << application.arguments().value(1) << fixture.checks << "preset cases";
        return 0;
    } catch (const std::exception& error) {
        qCritical() << "FAIL preset" << error.what();
        return 1;
    }
}
