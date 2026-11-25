// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <QCoreApplication>

// Force-included only by full-application regression targets. Qt's explicit
// organization/application constructor uses NativeFormat on macOS, regardless
// of setDefaultFormat/setPath. A per-process organization isolates every caller,
// including the production preference dialogs and processor.
#undef APP_IDENTIFIER
#define APP_IDENTIFIER                                        \
    (QStringLiteral("com.github.mikaelsundell.jobman.tests.") \
     + qEnvironmentVariable("JOBMAN_TEST_SETTINGS_ID", QString::number(QCoreApplication::applicationPid())))
