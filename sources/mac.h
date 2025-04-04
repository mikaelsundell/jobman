// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once
#include <QProcess>
#include <QWidget>

namespace mac {
struct IccProfile {
    int screenNumber;
    QString displayProfileUrl;
};
void
setDarkAppearance();
IccProfile
grabIccProfile(WId wid);
QString
grabIccProfileUrl(WId wid);
QString
resolveBookmark(const QString& bookmark);
QString
saveBookmark(const QString& bookmark);
void
console(const QString& log);
}  // namespace mac
