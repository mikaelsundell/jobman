
// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "process.h"
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

#ifdef __APPLE__
    #include <spawn.h>
    #include <crt_externs.h>
#endif

#ifdef _WIN32
    #include <windows.h>
       #include <tchar.h>
#endif

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
        bool running;
        int exitcode;
        QString outputBuffer;
        QString errorBuffer;

#ifdef __APPLE__
        pid_t pid;
        int outputpipe[2];
        int errorpipe[2];
#elif defined(_WIN32)
        PROCESS_INFORMATION processInfo;
        HANDLE outputRead;
        HANDLE outputWrite;
        HANDLE errorRead;
        HANDLE errorWrite;
#endif
};

ProcessPrivate::ProcessPrivate()
: exitcode(-1)
, running(false)
{
#ifdef __APPLE__
    pid = -1;
#elif defined(_WIN32)
    ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
#endif
}

ProcessPrivate::~ProcessPrivate()
{
#ifdef __APPLE__
    if (outputpipe[0] != -1) {
        close(outputpipe[0]);
    }
    if (errorpipe[0] != -1) {
        close(errorpipe[0]);
    }
#endif
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

#ifdef __APPLE__
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

#elif defined(_WIN32)
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&outputRead, &outputWrite, &sa, 0) ||
        !CreatePipe(&errorRead, &errorWrite, &sa, 0)) {
        exitcode = -1;
        return;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errorRead, HANDLE_FLAG_INHERIT, 0);

    QString commandpath = absolutepath + " " + arguments.join(" ");
    std::wstring commandlinew = commandpath.toStdWString();
    LPWSTR commandline = const_cast<LPWSTR>(commandlinew.c_str());

    QList<QPair<QString, QString>> systemenv;
    LPWCH envstrings = GetEnvironmentStringsW();
    if (envstrings != nullptr) {
        for (LPWCH var = envstrings; *var != L'\0'; var += wcslen(var) + 1) {
            QString envVar = QString::fromWCharArray(var);
            if (!envVar.isEmpty()) {
                QStringList parts = envVar.split('=');
                if (parts.size() == 2) {
                    systemenv.append(qMakePair(parts[0], parts[1]));
                }
            }
        }
        FreeEnvironmentStringsW(envstrings);
    }
    QStringList processenv;
    for (const QPair<QString, QString>& pair : environment) {
        QString key = pair.first;
        QString value = QDir::toNativeSeparators(pair.second);
        bool found = false;
        for (const QPair<QString, QString>& syspair : systemenv) {
            qDebug() << "check: " << syspair.first;
            if (QString::compare(syspair.first, key, Qt::CaseInsensitive) == 0) {
                found = true;
                QString systemvalue = syspair.second;
                value.replace("%" + key + "%", systemvalue);
                break;
            }
        }
        processenv.append(key + "=" + value);
    }
    for (const QPair<QString, QString>& syspair : systemenv) {
        bool exists = false;
        for (const QPair<QString, QString>& pair : environment) {
            if (QString::compare(pair.first, syspair.first, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            processenv.append(syspair.first + "=" + syspair.second);
        }
    }
    std::wstring envblocks;
    for (const QString& envVar : processenv) {
        envblocks.append(envVar.toStdWString());
        envblocks.push_back(L'\0');
    }
    envblocks.push_back(L'\0');
    LPVOID lpenvironment = const_cast<wchar_t*>(envblocks.c_str());

    STARTUPINFO startupInfo;
    ZeroMemory(&startupInfo, sizeof(STARTUPINFO));
    startupInfo.cb = sizeof(STARTUPINFO);
    startupInfo.hStdOutput = outputWrite;
    startupInfo.hStdError = errorWrite;
    startupInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    if (CreateProcessW(
            nullptr, 
            commandline,
            nullptr,
            nullptr, 
            TRUE, 
            CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, 
            lpenvironment,
            nullptr, 
            &startupInfo, 
            &processInfo)) {
        running = true;
    }
    else {
        exitcode = -1;
    }
    CloseHandle(outputWrite);
    CloseHandle(errorWrite);
#endif
}

bool
ProcessPrivate::wait()
{
    if (running) {
#ifdef __APPLE__
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

#elif defined(_WIN32)
        if (running) {
            char buffer[1024];
            DWORD bytesRead;
            BOOL success;
            DWORD exitCode;
            while (true) {
                DWORD status = WaitForSingleObject(processInfo.hProcess, 50);
                if (status == WAIT_OBJECT_0) {
                    GetExitCodeProcess(processInfo.hProcess, &exitCode);
                    exitcode = exitCode;
                    running = false;
                }
                while (true) {
                    DWORD bytesAvailable = 0;
                    if (!PeekNamedPipe(outputRead, nullptr, 0, nullptr, &bytesAvailable, nullptr) || bytesAvailable == 0) {
                        break;
                    }
                    success = ReadFile(outputRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
                    if (!success || bytesRead == 0) {
                        break;
                    }
                    buffer[bytesRead] = '\0';
                    outputBuffer.append(QString::fromLocal8Bit(buffer));
                }
                while (true) {
                    DWORD bytesAvailable = 0;
                    if (!PeekNamedPipe(errorRead, nullptr, 0, nullptr, &bytesAvailable, nullptr) || bytesAvailable == 0) {
                        break;
                    }
                    success = ReadFile(errorRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
                    if (!success || bytesRead == 0) {
                        break;
                    }
                    buffer[bytesRead] = '\0';
                    errorBuffer.append(QString::fromLocal8Bit(buffer));
                }
                if (!running) {
                    break;
                }
                QThread::msleep(10);
            }
            CloseHandle(outputRead);
            CloseHandle(errorRead);
            CloseHandle(processInfo.hProcess);
            CloseHandle(processInfo.hThread);
            return exitcode == 0;
        }
#endif
    }
    return false;
}

void
ProcessPrivate::kill()
{
#ifdef __APPLE__
    if (running) {
        ::kill(pid, SIGKILL);
        wait();
    }
#elif defined(_WIN32)
    TerminateProcess(processInfo.hProcess, 1);
    CloseHandle(processInfo.hProcess);
#endif
}

QString
ProcessPrivate::mapCommand(const QString& command)
{
    QString pathenv = QString::fromLocal8Bit(getenv("PATH"));
#ifdef __APPLE__
    QStringList paths = pathenv.split(':');
    for (const QString& path : paths) {
        QString fullPath = path + "/" + command;
        struct stat buffer;
        if (stat(fullPath.toLocal8Bit().data(), &buffer) == 0 && (buffer.st_mode & S_IXUSR)) {
            return fullPath;
        }
    }
#elif defined(_WIN32)
    QStringList paths = pathenv.split(';');
    for (const QString& path : paths) {
        QString fullPath = QDir::toNativeSeparators(path + "/" + command);
        if (QFileInfo::exists(fullPath) && QFileInfo(fullPath).isFile()) {
            return fullPath;
        }
    }
#endif
    return command;
}

char**
ProcessPrivate::mapEnvironment(QList<QPair<QString, QString>> environment)
{
    QVector<QByteArray> envlist;
#ifdef __APPLE__
    char **environ = *_NSGetEnviron();
    for (int i = 0; environ[i] != nullptr; ++i) {
        envlist.append(QByteArray(environ[i]));
    }
#elif defined(_WIN32)
    LPWCH envstrings = GetEnvironmentStringsW();
    if (envstrings != nullptr) {
        for (LPWCH var = envstrings; *var != L'\0'; var += wcslen(var) + 1) {
            // Convert wide string to UTF-8 QByteArray
            QByteArray utf8Var = QString::fromWCharArray(var).toUtf8();
            envlist.append(utf8Var);
        }
        FreeEnvironmentStringsW(envstrings);
    }
#endif
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
    QFileInfo fileInfo(command);
    if (fileInfo.isAbsolute()) {
        return fileInfo.exists() && fileInfo.isExecutable();
    }
    Process process;
#ifdef __APPLE__
    process.run("which", QStringList() << command, "");
#endif

#ifdef _WIN32
    process.run("where", QStringList() << command, "");
#endif
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
#ifdef __APPLE__
    return p->pid;
#elif defined(_WIN32)
    return static_cast<int>(p->processInfo.dwProcessId);
#endif
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
#ifdef __APPLE__
    ::kill(pid, SIGKILL);
#elif defined(_WIN32)
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (hProcess != nullptr) {
        TerminateProcess(hProcess, 1);  // Exit code 1 for forced termination
        CloseHandle(hProcess);
    }
#endif
}
