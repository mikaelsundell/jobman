// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "platform.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QScreen>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace platform {
    namespace {
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
        IccProfileData iccData = grabDisplayProfile();
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
    openPaths(const QList<QString>& paths)
    {
        QHash<QString, QList<QString>> grouped;
        for (const QString& path : paths) {
            QFileInfo info(path);
            if (info.exists()) {
                if (info.isDir()) {
                    QProcess::startDetached("explorer.exe", { QDir::toNativeSeparators(info.absoluteFilePath()) });
                }
                else {
                    QString parent = info.absolutePath();
                    grouped[parent].append(info.absoluteFilePath());
                }
            }
        }

        for (auto it = grouped.begin(); it != grouped.end(); ++it) {
            QString parentPath = QDir::toNativeSeparators(it.key());
            QList<QString> filePaths = it.value();

            PIDLIST_ABSOLUTE pidlFolder = nullptr;
            HRESULT hr = SHParseDisplayName((LPCWSTR)parentPath.utf16(), nullptr, &pidlFolder, 0, nullptr);
            if (FAILED(hr)) {
                continue;
            }
            std::vector<PCUITEMID_CHILD> childItems;
            std::vector<PIDLIST_ABSOLUTE> allocatedItems;
            for (const QString& filePath : filePaths) {
                QString nativeFile = QDir::toNativeSeparators(filePath);
                PIDLIST_ABSOLUTE pidlFile = nullptr;
                hr = SHParseDisplayName((LPCWSTR)nativeFile.utf16(), nullptr, &pidlFile, 0, nullptr);
                if (SUCCEEDED(hr)) {
                    childItems.push_back(ILFindLastID(pidlFile));
                    allocatedItems.push_back(pidlFile);
                }
            }
            if (!childItems.empty()) {
                SHOpenFolderAndSelectItems(pidlFolder, static_cast<UINT>(childItems.size()), childItems.data(), 0);
            }
            for (auto pidl : allocatedItems) {
                CoTaskMemFree(pidl);
            }
            CoTaskMemFree(pidlFolder);
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
