// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "process.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <future>
#include <stdexcept>

namespace {
void
require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QString
captureDirectory(const QString& child, const QString& directory)
{
    Process process;
    process.run(child, { "cwd" }, directory);
    require(process.wait(), "Working-directory child failed");
    return process.standardOutput();
}

void
runCase(const QString& name, const QString& child)
{
    if (name == "process_stdout" || name == "process_stderr" || name == "process_both_streams") {
        const bool output = name != "process_stderr";
        const bool error = name != "process_stdout";
        Process process;
        process.run(child, { output && error ? "both" : output ? "stdout" : "stderr" });
        require(process.wait(), "Large-output child failed");
        require(process.standardOutput() == (output ? QString(2 * 1024 * 1024, 'o') : QString()),
                "stdout was lost, truncated, or mixed with stderr");
        require(process.standardError() == (error ? QString(2 * 1024 * 1024, 'e') : QString()),
                "stderr was lost, truncated, or mixed with stdout");
    }
    else if (name == "process_utf8") {
        Process process;
        const QString expected = QString::fromUtf8(QByteArray::fromHex("c3a5c3a4c3b6204dc3a47373696e6720f09f988000"));
        process.run(child, { "utf8", QString::fromLatin1(expected.toUtf8().toHex()) });
        require(process.wait(), "UTF-8 child failed");
        require(process.standardOutput() == expected && process.standardError() == expected,
                "Split UTF-8 or embedded NUL was corrupted");
        process.run(child, { "cwd" });
        require(process.wait(), "Second capture failed");
        require(process.standardOutput() == QDir::currentPath() && process.standardError().isEmpty(),
                "Output buffers were not reset between runs");
    }
    else if (name == "process_working_directory" || name == "process_concurrent_directories") {
        const QString parentDirectory = QDir::currentPath();
        QTemporaryDir temporary;
        require(temporary.isValid(), "Could not create directory fixture");
        QDir root(temporary.path());
        require(root.mkdir(QString::fromUtf8("first space \xc3\xa5")) && root.mkdir("second space"),
                "Could not create child directories");
        const QString first = QFileInfo(root.filePath(QString::fromUtf8("first space \xc3\xa5"))).canonicalFilePath();
        const QString second = QFileInfo(root.filePath("second space")).canonicalFilePath();
        if (name == "process_concurrent_directories") {
            auto capture = [&](const QString& directory) {
                for (int i = 0; i < 20; ++i)
                    require(captureDirectory(child, directory) == directory, "Concurrent child used wrong directory");
            };
            auto firstTask = std::async(std::launch::async, capture, first);
            auto secondTask = std::async(std::launch::async, capture, second);
            firstTask.get();
            secondTask.get();
        }
        else {
            require(captureDirectory(child, first) == first, "Child did not use requested directory");
            require(captureDirectory(child, QString()) == parentDirectory,
                    "Empty startin did not inherit parent directory");
        }
        require(QDir::currentPath() == parentDirectory, "Child launch changed Jobman's working directory");
    }
    else if (name == "process_launch_failure") {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Could not create failure fixture");
        Process process;
        process.run(child, { "cwd" });
        require(process.wait(), "Initial launch failed");
        process.run(QDir(temporary.path()).filePath("missing-program"), {});
        require(!process.wait() && process.exitCode() != 0, "Missing executable reported success");
        require(process.standardOutput().isEmpty() && process.standardError().isEmpty(),
                "Failed launch kept stale output");
        process.run(child, { "cwd" }, QDir(temporary.path()).filePath("missing-directory"));
        require(!process.wait() && process.exitCode() != 0, "Invalid working directory reported success");
        process.run(child, { "cwd" });
        require(process.wait(), "Could not recover after launch failure");
    }
    else if (name == "process_exit_code") {
        Process process;
        process.run(child, { "exit" });
        require(!process.wait() && process.exitCode() == 7, "Nonzero exit code was not preserved");
        require(process.exitStatus() == Process::Crash, "Process exit-status contract changed");
    }
    else if (name == "process_kill") {
        Process process;
        process.run(child, { "sleep" });
        require(process.pid() > 0, "Sleeping child did not start");
        Process::kill(process.pid());
        require(!process.wait() && process.exitCode() != 0, "Killed child reported success");
    }
    else {
        throw std::runtime_error("Unknown regression case");
    }
}
}  // namespace

int
main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const QString name = application.arguments().value(1);
    const QString child = QFileInfo(application.arguments().value(2)).absoluteFilePath();
    try {
        require(QFileInfo::exists(child), "Child fixture executable is missing");
        runCase(name, child);
        qInfo().noquote() << "PASS" << name;
        return 0;
    } catch (const std::exception& error) {
        qCritical().noquote() << "FAIL" << name << error.what();
        return 1;
    }
}
