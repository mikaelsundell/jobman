// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "mcptools.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProgressBar>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>

namespace {
QJsonObject
stringProperty(const QString& description)
{
    return { { "type", "string" }, { "description", description } };
}

QJsonObject
integerProperty(int minimum, int maximum)
{
    return { { "type", "integer" }, { "minimum", minimum }, { "maximum", maximum } };
}

QJsonObject
definition(const QString& name, const QString& description, const QJsonObject& properties = {},
           const QJsonArray& required = {}, bool readOnly = true, bool destructive = false)
{
    return { { "name", name },
             { "description", description },
             { "inputSchema", QJsonObject { { "type", "object" },
                                            { "properties", properties },
                                            { "required", required },
                                            { "additionalProperties", false } } },
             { "annotations", QJsonObject { { "readOnlyHint", readOnly },
                                            { "destructiveHint", destructive },
                                            { "openWorldHint", false } } } };
}

QString
objectPath(QObject* object, QObject* root)
{
    QStringList parts;
    for (QObject* current = object; current; current = current->parent()) {
        parts.prepend(current->objectName());
        if (current == root)
            return parts.join('/');
    }
    return {};
}

QObject*
findObject(QWidget* window, const QString& path)
{
    const QStringList parts = path.split('/');
    if (parts.isEmpty() || parts.first() != window->objectName())
        return nullptr;
    QObject* current = window;
    for (qsizetype i = 1; i < parts.size(); ++i) {
        if (parts[i].isEmpty())
            return nullptr;
        const auto children = current->findChildren<QObject*>(parts[i], Qt::FindDirectChildrenOnly);
        if (children.size() != 1)
            return nullptr;
        current = children.first();
    }
    return current;
}

QJsonObject
describe(QObject* object, QObject* root, int depth, int& remaining)
{
    --remaining;
    QJsonObject state { { "objectName", object->objectName() },
                        { "class", object->metaObject()->className() },
                        { "path", objectPath(object, root) } };
    if (auto* widget = qobject_cast<QWidget*>(object)) {
        state.insert("visible", widget->isVisible());
        state.insert("enabled", widget->isEnabled());
        state.insert("windowTitle", widget->windowTitle());
        const QRect geometry = widget->geometry();
        state.insert("geometry", QJsonArray { geometry.x(), geometry.y(), geometry.width(), geometry.height() });
    }
    if (auto* label = qobject_cast<QLabel*>(object))
        state.insert("text", label->text().left(4096));
    if (auto* button = qobject_cast<QAbstractButton*>(object)) {
        state.insert("text", button->text());
        state.insert("checked", button->isChecked());
    }
    if (auto* action = qobject_cast<QAction*>(object)) {
        state.insert("text", action->text());
        state.insert("enabled", action->isEnabled());
        state.insert("checked", action->isChecked());
    }
    if (auto* line = qobject_cast<QLineEdit*>(object))
        state.insert("text", line->echoMode() == QLineEdit::Normal ? line->text().left(4096) : "<hidden>");
    if (auto* text = qobject_cast<QTextEdit*>(object))
        state.insert("text", text->toPlainText().left(4096));
    if (auto* combo = qobject_cast<QComboBox*>(object)) {
        state.insert("currentIndex", combo->currentIndex());
        state.insert("currentText", combo->currentText());
        QJsonArray items;
        for (int i = 0; i < qMin(combo->count(), 100); ++i)
            items.append(combo->itemText(i));
        state.insert("items", items);
    }
    if (auto* stack = qobject_cast<QStackedWidget*>(object))
        state.insert("currentIndex", stack->currentIndex());
    if (auto* progress = qobject_cast<QProgressBar*>(object)) {
        state.insert("value", progress->value());
        state.insert("maximum", progress->maximum());
    }
    if (auto* tree = qobject_cast<QTreeWidget*>(object)) {
        QJsonArray rows;
        for (int i = 0; i < qMin(tree->topLevelItemCount(), 100); ++i) {
            QTreeWidgetItem* item = tree->topLevelItem(i);
            QJsonArray columns;
            for (int column = 0; column < tree->columnCount(); ++column)
                columns.append(item->text(column));
            rows.append(QJsonObject {
                { "columns", columns }, { "selected", item->isSelected() }, { "childCount", item->childCount() } });
        }
        state.insert("rows", rows);
        state.insert("rowCount", tree->topLevelItemCount());
    }
    if (depth > 0) {
        QJsonArray children;
        for (QObject* child : object->children()) {
            if (remaining <= 0)
                break;
            // Unnamed layout/internal objects cannot be addressed by the debug tools.
            if (!child->objectName().isEmpty())
                children.append(describe(child, root, depth - 1, remaining));
        }
        state.insert("children", children);
    }
    return state;
}
}  // namespace

QJsonArray
mcp::tools()
{
    const QJsonObject uuid { { "uuid", stringProperty("Job UUID returned by list_jobs or submission notifications.") } };
    QJsonArray tools {
        definition("jobman_get_status", "Read the active preset, output directory, file-drop state, and queue status."),
        definition("jobman_list_presets", "List presets loaded by the desktop application."),
        definition("jobman_select_preset", "Select an existing preset through the application's Qt UI.",
                   { { "id", stringProperty("Preset id from jobman_list_presets.") } }, { "id" }, false),
        definition("jobman_set_output_directory", "Select an existing output directory for subsequent jobs.",
                   { { "path", stringProperty("Absolute path to an existing directory.") } }, { "path" }, false),
        definition(
            "jobman_submit_files",
            "Drop local files/directories into the selected preset. Returns before scanning/submission completes. Check status/jobs and respond to any confirmation dialog.",
            { { "paths", QJsonObject { { "type", "array" },
                                       { "items", stringProperty("Absolute local path.") },
                                       { "minItems", 1 } } } },
            { "paths" }, false),
        definition(
            "jobman_list_jobs",
            "Read current jobs without full logs. Follow nextCursor to enumerate the whole queue. No job-count cap; limit defaults to 100. Does not change scheduling.",
            { { "limit", QJsonObject { { "type", "integer" }, { "minimum", 1 } } },
              { "cursor", stringProperty("Job UUID from the previous page's nextCursor; omit for the first page.") } }),
        definition("jobman_get_job", "Read one job's command, state, output, and bounded log tail.",
                   { { "uuid", uuid.value("uuid") }, { "logLimit", integerProperty(0, 262144) } }, { "uuid" }),
        definition(
            "jobman_control_job",
            "Start a stopped job, stop a running job, restart a finished job, or remove a job and its dependents using the existing queue API.",
            { { "uuid", uuid.value("uuid") }, { "action", stringProperty("start, stop, restart, or remove.") } },
            { "uuid", "action" }, false, true),
        definition("jobman_get_dialog", "Read the active application-owned modal dialog and its buttons."),
        definition("jobman_respond_dialog", "Respond to an active Jobman message/confirmation dialog.",
                   { { "response", stringProperty("close, yes, or no.") } }, { "response" }, false, true),
        definition("jobman_save_preferences",
                   "Persist the application's current preferences through its existing Save action.", {}, {}, false),
        definition(
            "jobman_get_qt_state",
            "Inspect real Qt widgets/actions by object path, including enabled/visible state, labels, selections, and progress. All inspection runs on the application thread.",
            { { "path", stringProperty("Qt object path; defaults to Jobman. Use returned paths for later actions.") },
              { "depth", integerProperty(0, 12) },
              { "limit", integerProperty(1, 1000) } }),
        definition(
            "jobman_qt_action",
            "Operate an application-owned Qt widget or action. Prefer named job/preset tools. Modal dialogs restrict actions to their own controls.",
            { { "path", stringProperty("Exact Qt object path from get_qt_state.") },
              { "action",
                stringProperty("click, trigger, set_text, set_checked, select_index, select_row, show, or hide.") },
              { "value",
                QJsonObject {
                    { "description", "Text, boolean, index, or tree row-index array as required by the action." } } } },
            { "path", "action" }, false, true),
        definition("jobman_capture_widget", "Return a PNG screenshot of an actual Qt widget as MCP image content.",
                   { { "path", stringProperty("Qt object path; defaults to Jobman.") } })
    };
    return tools;
}

QJsonObject
mcp::result(const QJsonValue& value)
{
    const QString text = value.isArray()
                             ? QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented))
                         : value.isObject()
                             ? QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented))
                             : value.toString();
    return { { "content", QJsonArray { QJsonObject { { "type", "text" }, { "text", text } } } } };
}

QJsonObject
mcp::error(const QString& message)
{
    QJsonObject response = result(message);
    response.insert("isError", true);
    return response;
}

QJsonObject
mcp::qtTool(QWidget* window, const QString& name, const QJsonObject& arguments)
{
    const QString path = arguments.value("path").toString(window->objectName());
    QObject* object = findObject(window, path);
    if (!object)
        return error("Qt object path does not exist or is ambiguous: " + path);
    if (name == "jobman_get_qt_state") {
        int remaining = arguments.value("limit").toInt(500);
        const QJsonObject snapshot = describe(object, window, arguments.value("depth").toInt(4), remaining);
        return result(QJsonObject { { "root", snapshot }, { "truncated", remaining <= 0 } });
    }
    auto* widget = qobject_cast<QWidget*>(object);
    if (name == "jobman_capture_widget") {
        if (!widget)
            return error("Screenshot target must be a widget.");
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!widget->grab().save(&buffer, "PNG"))
            return error("Widget screenshot failed.");
        return { { "content", QJsonArray { QJsonObject { { "type", "image" },
                                                         { "mimeType", "image/png" },
                                                         { "data", QString::fromLatin1(bytes.toBase64()) } } } } };
    }
    if (name != "jobman_qt_action")
        return error("Unknown Qt debug tool.");
    QWidget* modal = QApplication::activeModalWidget();
    if (modal && (!widget || (widget != modal && !modal->isAncestorOf(widget))))
        return error("Respond to the active modal dialog before operating another widget.");
    const QString action = arguments.value("action").toString();
    const QJsonValue value = arguments.value("value");
    bool queued = false;
    if (action == "trigger") {
        auto* target = qobject_cast<QAction*>(object);
        if (!target || !target->isEnabled())
            return error("Target must be an enabled QAction.");
        const QPointer<QAction> actionTarget(target);
        QTimer::singleShot(0, window, [actionTarget]() {
            if (actionTarget)
                actionTarget->trigger();
        });
        queued = true;
    }
    else if (action == "show" || action == "hide") {
        if (!widget)
            return error("Target must be a widget.");
        widget->setVisible(action == "show");
    }
    else {
        if (!widget || !widget->isEnabled() || !widget->isVisible())
            return error("Target must be a visible, enabled widget.");
        if (action == "click") {
            auto* button = qobject_cast<QAbstractButton*>(widget);
            if (!button)
                return error("Target must be a button.");
            const QPointer<QAbstractButton> buttonTarget(button);
            QTimer::singleShot(0, window, [buttonTarget]() {
                if (buttonTarget)
                    buttonTarget->click();
            });
            queued = true;
        }
        else if (action == "set_checked") {
            auto* button = qobject_cast<QAbstractButton*>(widget);
            if (!button || !button->isCheckable() || !value.isBool())
                return error("Target must be checkable and value must be boolean.");
            button->setChecked(value.toBool());
        }
        else if (action == "set_text") {
            auto* line = qobject_cast<QLineEdit*>(widget);
            if (!line || line->isReadOnly() || !value.isString())
                return error("Target must be an editable QLineEdit and value must be text.");
            line->setText(value.toString());
        }
        else if (action == "select_index") {
            auto* combo = qobject_cast<QComboBox*>(widget);
            if (!combo || !value.isDouble() || value.toDouble() != value.toInt() || value.toInt() < 0
                || value.toInt() >= combo->count())
                return error("Target must be a combo box and value must be a valid item index.");
            combo->setCurrentIndex(value.toInt());
        }
        else if (action == "select_row") {
            auto* tree = qobject_cast<QTreeWidget*>(widget);
            const QJsonArray indexes = value.toArray();
            if (!tree || indexes.isEmpty())
                return error("Target must be a tree and value must be a nonempty row-index array.");
            QTreeWidgetItem* item = tree->invisibleRootItem();
            for (const QJsonValue& index : indexes) {
                if (!index.isDouble() || index.toDouble() != index.toInt() || index.toInt() < 0
                    || index.toInt() >= item->childCount())
                    return error("Invalid tree row index.");
                item = item->child(index.toInt());
            }
            tree->clearSelection();
            tree->setCurrentItem(item);
            item->setSelected(true);
        }
        else {
            return error("Unsupported Qt action: " + action);
        }
    }
    return result(QJsonObject { { "path", path }, { "queued", queued }, { "performed", !queued } });
}
