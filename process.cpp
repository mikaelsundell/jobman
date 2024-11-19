
// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "process.h"

#include <unistd.h>
#include <spawn.h>
#include <signal.h>
#include <crt_externs.h>
#include <sys/stat.h>

#include <QDir>
#include <QProcess>
#include <QThread>
#include <QDebug>

class ProcessPrivate : public QObject
{
    Q_OBJECT
    public:
        ProcessPrivate();
        ~ProcessPrivate();
        void init();
        void run(const QString& command, const QStringList& arguments, const QString& startin, const QList<QPair<QString, QString>>& environmentvars);
        bool wait();
        void kill();
        void kill(int pid);
    public:
        QString mapCommand(const QString& command);
        char** mapEnvironment(QList<QPair<QString, QString>> environment);
        pid_t pid;
        int exitcode;
        QString outputBuffer;
        QString errorBuffer;
        bool running;
        int outputpipe[2];
        int errorpipe[2];
};

ProcessPrivate::ProcessPrivate()
: pid(-1)
, exitcode(-1)
, running(false)
{
}

ProcessPrivate::~ProcessPrivate()
{
    if (outputpipe[0] != -1) {
        close(outputpipe[0]);
    }
    if (errorpipe[0] != -1) {
        close(errorpipe[0]);
    }
}

void
ProcessPrivate::init()
{
}

void
ProcessPrivate::run(const QString& command, const QStringList& arguments, const QString& startin, const QList<QPair<QString, QString>>& environment)
{
    running = false;
    outputBuffer.clear();
    errorBuffer.clear();
    QString absolutepath = mapCommand(command);
    QList<char *> argv;
    QByteArray commandbytes = absolutepath.toLocal8Bit();
    argv.push_back(commandbytes.data());
    std::vector<QByteArray> argbytes;
    for (const QString &arg : arguments) {
        argbytes.push_back(arg.toLocal8Bit());
        argv.push_back(argbytes.back().data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (pipe(outputpipe) == -1 || pipe(errorpipe) == -1) {
        return; // error creating pipes
    }
    posix_spawn_file_actions_adddup2(&actions, outputpipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errorpipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, outputpipe[0]);
    posix_spawn_file_actions_addclose(&actions, errorpipe[0]);
    if (!startin.isEmpty()) {
        chdir(startin.toLocal8Bit().data());
    }
    char** environ = mapEnvironment(environment);
    int status = posix_spawn(&pid, commandbytes.data(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    for (int i = 0; environ[i] != nullptr; ++i) {
        free(environ[i]); // take care of environ
    }
    close(outputpipe[1]);
    close(errorpipe[1]);
    if (status == 0) {
        running = true;
    }
    else {
        exitcode = -1; // process failed to start
    }
}

bool
ProcessPrivate::wait()
{
    if (running) {
        int status;
        waitpid(pid, &status, 0);
        running = false;
        char buffer[1024];
        ssize_t bytesread;
        if (outputpipe[0] != -1) {
            while ((bytesread = read(outputpipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytesread] = '\0';
                outputBuffer.append(buffer);
            }
            close(outputpipe[0]);
            outputpipe[0] = -1;
        }
        if (errorpipe[0] != -1) {
            while ((bytesread = read(errorpipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytesread] = '\0';
                errorBuffer.append(buffer);
            }
            close(errorpipe[0]);
            errorpipe[0] = -1;
        }
        if (WIFEXITED(status)) {
            exitcode = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status)) {
            exitcode = -WTERMSIG(status);
        }
        else {
            exitcode = -1;
        }
        return exitcode == 0;
    }
    return false;
}

void
ProcessPrivate::kill()
{
    if (running) {
        ::kill(pid, SIGKILL);
        wait();
    }
}

QString
ProcessPrivate::mapCommand(const QString& command)
{
    QString pathenv = QString::fromLocal8Bit(getenv("PATH"));
    QStringList paths = pathenv.split(':');
    for (const QString& path : paths) {
        QString fullPath = path + "/" + command;
        struct stat buffer;
        if (stat(fullPath.toLocal8Bit().data(), &buffer) == 0 && (buffer.st_mode & S_IXUSR)) {
            return fullPath;
        }
    }
    return command;
}

char**
ProcessPrivate::mapEnvironment(QList<QPair<QString, QString>> environment)
{
    char **environ = *_NSGetEnviron();
    QVector<QByteArray> envlist;
    for (int i = 0; environ[i] != nullptr; ++i) {
        envlist.append(QByteArray(environ[i]));
    }
    for (const QPair<QString, QString>& pair : environment) {
        QByteArray envvar = pair.first.toUtf8() + "=" + pair.second.toUtf8();
        envlist.append(envvar);
    }
    char **envp = new char*[envlist.size() + 1];
    for (int i = 0; i < envlist.size(); ++i) {
        envp[i] = strdup(envlist[i].constData());
    }
    envp[envlist.size()] = nullptr;
    return envp;
}

#include "process.moc"

Process::Process()
: p(new ProcessPrivate())
{
}

Process::~Process()
{
}

void
Process::run(const QString& command, const QStringList& arguments, const QString& startin, const QList<QPair<QString, QString>>& environmentvars)
{
    p->run(command, arguments, startin, environmentvars);
}

bool
Process::wait()
{
    return p->wait();
}

bool
Process::exists(const QString& command)
{
    Process process;
    process.run("which", QStringList() << command, "");
    return process.wait();
}

void
Process::kill()
{
    p->kill();
}

int
Process::pid() const
{
    return p->pid;
}

QString
Process::standardOutput() const
{
    return p->outputBuffer;
}

QString
Process::standardError() const
{
    return p->errorBuffer;
}

int
Process::exitCode() const
{
    return p->exitcode;
}

Process::Status
Process::exitStatus() const
{
    return (p->exitcode == 0) ? Process::Normal : Process::Crash;
}

void
Process::kill(int pid)
{
    ::kill(pid, SIGKILL);
}
