
// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "process.h"

#ifdef __APPLE__
#    include <AvailabilityMacros.h>
#    include <cerrno>
#    include <crt_externs.h>
#    include <fcntl.h>
#    include <poll.h>
#    include <signal.h>
#    include <spawn.h>
#    include <sys/stat.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

#ifdef _WIN32
#    include <tchar.h>
#    include <windows.h>
#endif

#include <QByteArray>
#include <QDir>
#include <QProcess>
#include <QThread>

#include <vector>

class ProcessPrivate : public QObject {
    Q_OBJECT
public:
    ProcessPrivate();
    ~ProcessPrivate();
    void init();
    void run(const QString& command, const QStringList& arguments, const QString& startin,
             const QList<QPair<QString, QString>>& environmentvars);
    bool wait();
    void kill();
    void kill(int pid);

public:
    QString mapCommand(const QString& command);
    char** mapEnvironment(QList<QPair<QString, QString>> environment);
    bool running;
    int exitcode;
    // Keep raw bytes until capture is complete so reads cannot split UTF-8 characters.
    QByteArray outputBuffer;
    QByteArray errorBuffer;

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
    outputpipe[0] = outputpipe[1] = -1;
    errorpipe[0] = errorpipe[1] = -1;
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
{}

void
ProcessPrivate::run(const QString& command, const QStringList& arguments, const QString& startin,
                    const QList<QPair<QString, QString>>& environment)
{
    running = false;
    exitcode = -1;
    outputBuffer.clear();
    errorBuffer.clear();
    QString absolutepath = mapCommand(command);

#ifdef __APPLE__
    QList<char*> argv;
    QByteArray commandbytes = absolutepath.toLocal8Bit();
    argv.push_back(commandbytes.data());
    std::vector<QByteArray> argbytes;
    for (const QString& arg : arguments) {
        argbytes.push_back(arg.toLocal8Bit());
        argv.push_back(argbytes.back().data());
    }
    argv.push_back(nullptr);
    const auto closePipes = [&]() {
        for (int* pipefds : { outputpipe, errorpipe }) {
            for (int i = 0; i < 2; ++i) {
                if (pipefds[i] != -1)
                    close(pipefds[i]);
                pipefds[i] = -1;
            }
        }
    };
    if (pipe(outputpipe) == -1 || pipe(errorpipe) == -1) {
        closePipes();
        return;  // error creating pipes
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        closePipes();
        return;
    }
    int status = posix_spawn_file_actions_adddup2(&actions, outputpipe[1], STDOUT_FILENO)
                 || posix_spawn_file_actions_adddup2(&actions, errorpipe[1], STDERR_FILENO)
                 || posix_spawn_file_actions_addclose(&actions, outputpipe[0])
                 || posix_spawn_file_actions_addclose(&actions, errorpipe[0])
                 || posix_spawn_file_actions_addclose(&actions, outputpipe[1])
                 || posix_spawn_file_actions_addclose(&actions, errorpipe[1]);
    // Preserve the previous stdin behavior while closing unrelated inherited descriptors.
    if (status == 0 && fcntl(STDIN_FILENO, F_GETFD) != -1)
        status = posix_spawn_file_actions_addinherit_np(&actions, STDIN_FILENO);
    // Change only the child's directory; chdir() would affect every Jobman worker.
    if (status == 0 && !startin.isEmpty()) {
#    if defined(__MAC_26_0) && __MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_26_0
        if (__builtin_available(macOS 26.0, *)) {
            status = posix_spawn_file_actions_addchdir(&actions, startin.toLocal8Bit().constData());
        }
        else
#    endif
        {
            // Older macOS versions require the non-standard spelling.
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdeprecated-declarations"
            status = posix_spawn_file_actions_addchdir_np(&actions, startin.toLocal8Bit().constData());
#    pragma clang diagnostic pop
        }
    }
    posix_spawnattr_t attributes;
    const bool attributesInitialized = posix_spawnattr_init(&attributes) == 0;
    if (!attributesInitialized)
        status = -1;
    // Other concurrent launches must not inherit these pipes and keep them open.
    if (status == 0)
        status = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
    char** environ = mapEnvironment(environment);
    pid = -1;
    if (status == 0)
        status = posix_spawn(&pid, commandbytes.data(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (attributesInitialized)
        posix_spawnattr_destroy(&attributes);
    for (int i = 0; environ[i] != nullptr; ++i) {
        free(environ[i]);  // take care of environ
    }
    delete[] environ;
    close(outputpipe[1]);
    close(errorpipe[1]);
    outputpipe[1] = errorpipe[1] = -1;
    if (status == 0) {
        running = true;
    }
    else {
        closePipes();
        pid = -1;
    }

#elif defined(_WIN32)
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&outputRead, &outputWrite, &sa, 0) || !CreatePipe(&errorRead, &errorWrite, &sa, 0)) {
        exitcode = -1;
        return;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errorRead, HANDLE_FLAG_INHERIT, 0);

    QStringList quotedarguments;
    for (const QString& arg : arguments) {
        quotedarguments.append(QString("\"%1\"").arg(arg));  // quotes needed on windows
    }
    QString commandpath = absolutepath + " " + quotedarguments.join(" ");
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
            if (syspair.first.toLower() == key.toLower()) {
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
            if (pair.first.toLower() == syspair.first.toLower()) {
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

    STARTUPINFOW startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.hStdOutput = outputWrite;
    startupInfo.hStdError = errorWrite;
    startupInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    const std::wstring workingDirectory = QDir::toNativeSeparators(startin).toStdWString();
    if (CreateProcessW(nullptr, commandline, nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                       lpenvironment, startin.isEmpty() ? nullptr : workingDirectory.c_str(), &startupInfo,
                       &processInfo)) {
        running = true;
    }
    else {
        exitcode = -1;
        CloseHandle(outputRead);
        CloseHandle(errorRead);
        ZeroMemory(&processInfo, sizeof(processInfo));
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
        int status = 0;
        bool exited = false;
        bool captureFailed = false;
        // Drain both streams while the child runs, including any bytes remaining at exit.
        // Waiting first, or draining one pipe to EOF first, can deadlock a verbose child.
        while (!exited || outputpipe[0] != -1 || errorpipe[0] != -1) {
            if (!exited) {
                const pid_t result = waitpid(pid, &status, WNOHANG);
                if (result == pid)
                    exited = true;
                else if (result == -1 && errno != EINTR) {
                    captureFailed = true;
                    break;
                }
            }
            pollfd pipes[] = { { outputpipe[0], POLLIN, 0 }, { errorpipe[0], POLLIN, 0 } };
            if (poll(pipes, 2, 50) == -1) {
                if (errno == EINTR)
                    continue;
                captureFailed = true;
                break;
            }
            for (int i = 0; i < 2; ++i) {
                if (!(pipes[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)))
                    continue;
                char buffer[16384];
                const ssize_t bytesread = read(pipes[i].fd, buffer, sizeof(buffer));
                if (bytesread > 0) {
                    (i == 0 ? outputBuffer : errorBuffer).append(buffer, bytesread);
                }
                else if (bytesread == 0 || errno != EINTR) {
                    if (bytesread < 0)
                        captureFailed = true;
                    int& fd = i == 0 ? outputpipe[0] : errorpipe[0];
                    close(fd);
                    fd = -1;
                }
            }
        }
        if (!exited) {
            ::kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
        }
        for (int* pipefds : { outputpipe, errorpipe }) {
            if (pipefds[0] != -1)
                close(pipefds[0]);
            pipefds[0] = -1;
        }
        running = false;
        if (captureFailed) {
            exitcode = -1;
        }
        else if (WIFEXITED(status)) {
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
                    if (!PeekNamedPipe(outputRead, nullptr, 0, nullptr, &bytesAvailable, nullptr)
                        || bytesAvailable == 0) {
                        break;
                    }
                    success = ReadFile(outputRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
                    if (!success || bytesRead == 0) {
                        break;
                    }
                    outputBuffer.append(buffer, bytesRead);
                }
                while (true) {
                    DWORD bytesAvailable = 0;
                    if (!PeekNamedPipe(errorRead, nullptr, 0, nullptr, &bytesAvailable, nullptr)
                        || bytesAvailable == 0) {
                        break;
                    }
                    success = ReadFile(errorRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
                    if (!success || bytesRead == 0) {
                        break;
                    }
                    errorBuffer.append(buffer, bytesRead);
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
    char** environ = *_NSGetEnviron();
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
    char** envp = new char*[envlist.size() + 1];
    for (int i = 0; i < envlist.size(); ++i) {
        envp[i] = strdup(envlist[i].constData());
    }
    envp[envlist.size()] = nullptr;
    return envp;
}

#include "process.moc"

Process::Process()
    : p(new ProcessPrivate())
{}

Process::~Process() {}

void
Process::run(const QString& command, const QStringList& arguments, const QString& startin,
             const QList<QPair<QString, QString>>& environmentvars)
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
    return QString::fromUtf8(p->outputBuffer);
}

QString
Process::standardError() const
{
    return QString::fromUtf8(p->errorBuffer);
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
