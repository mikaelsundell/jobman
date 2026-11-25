// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <QJsonArray>
#include <QJsonObject>

class QWidget;

namespace mcp {
QJsonArray
tools();
QJsonObject
result(const QJsonValue& value);
QJsonObject
error(const QString& message);
QJsonObject
qtTool(QWidget* window, const QString& name, const QJsonObject& arguments);
}  // namespace mcp
