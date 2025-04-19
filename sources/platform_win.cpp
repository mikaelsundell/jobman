// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "platform.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QScreen>
#include <windows.h>

namespace platform {
namespace utils {
    QPointF toNativeCursor(int x, int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        QPoint globalPos = QCursor::pos();
        return QPointF(globalPos.x(), globalPos.y());
    }
    struct IccProfileData {
        QString profilePath;
        IccProfileData()
            : profilePath()
        {}
    };
    QMap<uint32_t, IccProfileData> iccCache;

    IccProfileData grabIccProfileData()
    {
        IccProfileData iccProfile;
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            DWORD pathSize = MAX_PATH;
            WCHAR iccPath[MAX_PATH];

            BOOL result = GetICMProfileW(hdc, &pathSize, iccPath);
            if (result) {
                iccProfile.profilePath = QString::fromWCharArray(iccPath);
            }
            ReleaseDC(nullptr, hdc);
        }
        return iccProfile;
    }

    IccProfileData grabDisplayProfile()
    {
        uint32_t displayId = 0;
        if (iccCache.contains(displayId)) {
            return iccCache.value(displayId);
        }
        IccProfileData iccProfile = grabIccProfileData();
        iccCache.insert(displayId, iccProfile);
        return iccProfile;
    }
}  // namespace utils

void
setDarkTheme()
{}

IccProfile
grabIccProfile(WId wid)
{
    Q_UNUSED(wid);
    utils::IccProfileData iccData = utils::grabDisplayProfile();
    return IccProfile { 0,  // Screen number (always 0 for primary display)
                        iccData.profilePath };
}

QString
getFileBrowser()
{
    return "Explorer";
}

QString
getIccProfileUrl(WId wid)
{
    return grabIccProfile(wid).displayProfileUrl;
}

QString
getApplicationPath()
{
    return QApplication::applicationDirPath();
}

QString
getExecutablePath()
{
    return getApplicationPath();
}

QString
resolveBookmark(const QString& bookmark)
{
    return bookmark;  // ignore on win32
}

QString
saveBookmark(const QString& bookmark)
{
    return bookmark;  // ignore on win32
}

void
openPath(const QString& path)
{
    QFileInfo info(path);
    if (info.exists()) {
        QString explorer = "explorer.exe";
        QStringList args;

        if (info.isDir()) {
            args << QDir::toNativeSeparators(info.absoluteFilePath());
        }
        else {
            args << "/select," << QDir::toNativeSeparators(info.absoluteFilePath());
        }

        QProcess::startDetached(explorer, args);
    }
}

double
getCpuUsage()
{
    static FILETIME prevIdleTime, prevKernelTime, prevUserTime;
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime) == 0) {
        return -1.0;
    }
    ULARGE_INTEGER idle, kernel, user;
    idle.LowPart = idleTime.dwLowDateTime;
    idle.HighPart = idleTime.dwHighDateTime;

    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;

    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    ULARGE_INTEGER prevIdle, prevKernel, prevUser;
    prevIdle.LowPart = prevIdleTime.dwLowDateTime;
    prevIdle.HighPart = prevIdleTime.dwHighDateTime;

    prevKernel.LowPart = prevKernelTime.dwLowDateTime;
    prevKernel.HighPart = prevKernelTime.dwHighDateTime;

    prevUser.LowPart = prevUserTime.dwLowDateTime;
    prevUser.HighPart = prevUserTime.dwHighDateTime;

    ULONGLONG idleDiff = idle.QuadPart - prevIdle.QuadPart;
    ULONGLONG kernelDiff = kernel.QuadPart - prevKernel.QuadPart;
    ULONGLONG userDiff = user.QuadPart - prevUser.QuadPart;
    ULONGLONG totalDiff = kernelDiff + userDiff;

    prevIdleTime = idleTime;
    prevKernelTime = kernelTime;
    prevUserTime = userTime;

    if (totalDiff == 0)
        return 0.0;
    double cpuUsage = (totalDiff - idleDiff) * 100.0 / totalDiff;
    return cpuUsage;
}
}  // namespace platform
