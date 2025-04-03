// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "utils.h"

#include <QRegularExpression>

namespace utils {
QString
formatDouble(double value, int precision)
{
    QString result = QString::number(value, 'f', precision);
    result.remove(QRegularExpression("0+$"));  // trailing zeros and dots
    result.remove(QRegularExpression("\\.$"));
    return result;
}
}  // namespace utils
