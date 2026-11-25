// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <cstdio>
#include <thread>

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

namespace {
bool
writeBytes(FILE* stream, const QByteArray& bytes)
{
    return fwrite(bytes.constData(), 1, bytes.size(), stream) == size_t(bytes.size()) && fflush(stream) == 0;
}

bool
flood(FILE* stream, char character)
{
    const QByteArray block(4096, character);
    for (int i = 0; i < 512; ++i) {
        if (!writeBytes(stream, block))
            return false;
    }
    return true;
}
}  // namespace

int
main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const QString mode = application.arguments().value(1);
    if (mode == "job") {
        const QStringList arguments = application.arguments();
        const QString output = arguments.value(2);
        const QByteArray tag = arguments.value(3).toUtf8();
        const QString prerequisite = arguments.value(4);
        if (prerequisite != "none" && !QFileInfo::exists(prerequisite))
            return 9;
        QThread::msleep(arguments.value(6).toULong());
        if (!writeBytes(stdout, "stdout:" + tag + '\n') || !writeBytes(stderr, "stderr:" + tag + '\n'))
            return 1;
        const int exitCode = arguments.value(5).toInt();
        if (exitCode != 0)
            return exitCode;
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly) || file.write("artifact:" + tag + '\n') < 0)
            return 1;
        return 0;
    }
    if (mode == "stdout")
        return flood(stdout, 'o') ? 0 : 1;
    if (mode == "stderr")
        return flood(stderr, 'e') ? 0 : 1;
    if (mode == "both") {
        bool output = false;
        bool error = false;
        std::thread outputThread([&]() { output = flood(stdout, 'o'); });
        std::thread errorThread([&]() { error = flood(stderr, 'e'); });
        outputThread.join();
        errorThread.join();
        return output && error ? 0 : 1;
    }
    if (mode == "utf8") {
        const QByteArray bytes = QByteArray::fromHex(application.arguments().value(2).toLatin1());
        // Force multibyte characters to arrive in separate writes.
        for (char byte : bytes) {
            if (!writeBytes(stdout, QByteArray(1, byte)) || !writeBytes(stderr, QByteArray(1, byte)))
                return 1;
            QThread::msleep(1);
        }
        return 0;
    }
    if (mode == "cwd")
        return writeBytes(stdout, QDir::currentPath().toUtf8()) ? 0 : 1;
    if (mode == "exit")
        return 7;
    if (mode == "sleep") {
        QThread::sleep(10);
        return 0;
    }
    return 2;
}
