// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once
#include <QProcess>
#include <QWidget>

namespace platform
{
    struct IccProfile {
        int screenNumber;
        QString displayProfileUrl;
    };
    IccProfile getIccProfile(WId wid);
    void setDarkTheme();
    QString getIccProfileUrl(WId wid);
    QString getApplicationPath();
    QString resolveBookmark(const QString& bookmark);
    QString saveBookmark(const QString& bookmark);
    double getCpuUsage();
}
