// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "jobman.h"
#include <QApplication>

int
main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    Jobman jobman;
    jobman.show();
    return app.exec();
}
