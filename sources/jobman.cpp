// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "jobman.h"
#include "clickfilter.h"
#include "mcpserver.h"
#include "mcptools.h"
#include "message.h"
#include "monitor.h"
#include "optionsdialog.h"
#include "optionswidget.h"
#include "platform.h"
#include "preferences.h"
#include "preset.h"
#include "process.h"
#include "processor.h"
#include "question.h"
#include "queue.h"
#include "ui.h"
#include "urlfilter.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMessageBox>
#include <QMetaEnum>
#include <QPointer>
#include <QProcess>
#include <QPromise>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSharedPointer>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QWindow>

#include <QtConcurrent>

// generated files
#include "ui_about.h"
#include "ui_jobman.h"

namespace {
class MaxJobsDelegate : public QStyledItemDelegate {
public:
    explicit MaxJobsDelegate(QComboBox* combo)
        : QStyledItemDelegate(combo)
        , combo(combo)
    {}

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const int size = ::ui()->iconSize(UI::Small);
        const qreal ratio = combo->devicePixelRatioF();
        QPixmap mark = ::ui()->icon(UI::Checked, UI::Small, UI::Normal, ratio);
        if (index.row() != combo->currentIndex())
            mark.fill(Qt::transparent);
        option->icon = QIcon(mark);
        option->decorationSize = QSize(size, size);
        option->features |= QStyleOptionViewItem::HasDecoration;
    }

private:
    QComboBox* combo;
};
}  // namespace

class JobmanPrivate : public QObject {
    Q_OBJECT
public:
    enum Type { File = 0, Command = 1, Progress = 2 };

public:
    JobmanPrivate();
    void init();
    void stylesheet();
    void activate();
    void enable(bool enable);
    bool eventFilter(QObject* object, QEvent* event);
    void verifySaveTo();
    void loadSettings();
    void initMcp();
    QJsonObject callMcpTool(const QString& name, const QJsonObject& args);
    void saveSettings();

public Q_SLOTS:
    void loadPresets();
    void clearPresets();
    void defaultsPreset();
    void togglePreset();
    void toggleType();
    void openPresetsFolder();
    void openSaveToFolder();
    void openMonitor();
    void openOptions();
    void processFiles(const QList<QString>& files);
    void processCommand();
    void processUuids(const QList<QUuid>& uuids);
    void jobsProcessed(const QList<QUuid>& uuids);
    void fileSubmitted(const QString& file);
    void submitFiles();
    void openPreferences();
    void clearPreferences();
    void savePreferences();
    void importPreferences();
    void exportPreferences();
    void refreshOptions();
    void refreshPresets();
    void openPreset();
    void selectPresetsfrom();
    void importPresetsUrl(const QUrl& url);
    void selectSaveto();
    void showSaveto();
    void setSaveto(const QString& text);
    void saveToUrl(const QUrl& url);
    void copyOriginalChanged(int state);
    void createFolderChanged(int state);
    void overwriteChanged(int state);
    void presetsChanged(int index);
    void threadsChanged(int index);
    void openAbout();
    void openMcpServer();
    void openGithubReadme();
    void openGithubIssues();

Q_SIGNALS:
    void readOnly(bool readOnly);

public:
    class About : public QDialog {
    public:
        About(QWidget* parent = nullptr)
            : QDialog(parent)
        {
            QScopedPointer<Ui_About> about;
            about.reset(new Ui_About());
            about->setupUi(this);
            ::ui()->setPixmap(about->label, UI::AppIcon, UI::ExtraLarge);
            about->name->setText(APP_NAME);
            about->version->setText(APP_VERSION_STRING);
            about->copyright->setText(APP_COPYRIGHT);
            QString url = GITHUBURL;
            about->github->setText(QString("Github project: <a href='%1'>%1</a>").arg(url));
            about->github->setTextFormat(Qt::RichText);
            about->github->setTextInteractionFlags(Qt::TextBrowserInteraction);
            about->github->setOpenExternalLinks(true);
            QFile file(":/files/resources/Copyright.txt");
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "Could not open license file:" << file.errorString();
                about->licenses->setText(tr("Unable to load license information."));
                return;
            }
            QTextStream in(&file);
            about->licenses->setText(in.readAll());
        }
    };
    struct FileDrop {
        QList<QString> submitfiles;
        QStringList rejectedfiles;
        bool hasDir = false;
    };
    Paths paths();
    bool applicationPath(const QString& path) const;
    QString elidedtext(const QString& text) const;
    QString documents;
    QString presets;
    QString presetsselected;
    QString presetsfrom;
    QString saveto;
    QString filesfrom;
    QString preferencesfrom;
    bool copyoriginal;
    bool createfolders;
    bool overwrite;
    int threads;
    int submitcount;
    qsizetype submittotal;
    QSize size;
    QFuture<FileDrop> filedropfuture;
    QFuture<QList<QUuid>> submitfuture;
    QList<QUuid> waitinguuids;
    QList<QUuid> processeduuids;
    QPointer<Queue> queue;
    QPointer<Jobman> window;
    QScopedPointer<About> about;
    QScopedPointer<Monitor> monitor;
    QScopedPointer<OptionsDialog> optionsdialog;
    QScopedPointer<Preferences> preferences;
    QScopedPointer<Processor> processor;
    QScopedPointer<Clickfilter> presetfilter;
    QScopedPointer<Clickfilter> filedropfilter;
    QScopedPointer<Urlfilter> preseturlfilter;
    QScopedPointer<Urlfilter> savetourlfilter;
    QScopedPointer<McpServer> mcpserver;
    quint16 mcpport = McpServer::defaultPort;
    QMap<QUuid, QSharedPointer<Job>> mcpjobs;
    QScopedPointer<Ui_Jobman> ui;
};

JobmanPrivate::JobmanPrivate() { qRegisterMetaType<QSharedPointer<Preset>>("QSharedPointer<Preset>"); }

void
JobmanPrivate::init()
{
    ::ui();
    // documents
    documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // presets
    presets = platform::getApplicationPath() + "/Presets";
    // queue
    queue = Queue::instance();
    // ui
    ui.reset(new Ui_Jobman());
    ui->setupUi(window);
    ::ui()->setIcon(ui->togglePreset, UI::Collapse);
    ::ui()->setIcon(ui->toggleType, UI::Collapse);
    ::ui()->setIcon(ui->showSaveto, UI::Link);
    ::ui()->setIcon(ui->selectSaveto, UI::Folder);
    ::ui()->setIcon(ui->openPreset, UI::File);
    ::ui()->setIcon(ui->refreshPresets, UI::Refresh);
    ::ui()->setIcon(ui->selectPresetsfrom, UI::Folder);
    ::ui()->setIcon(ui->openOptions, UI::Options);
    ::ui()->setIcon(ui->openMonitor, UI::Monitor);

    // preferences
    ui->editOpenPreferences->setMenuRole(QAction::PreferencesRole);
    // about
    about.reset(new About(window.data()));
    // monitor
    monitor.reset(new Monitor(window.data()));
    // options
    optionsdialog.reset(new OptionsDialog(window.data()));
    // preferences
    preferences.reset(new Preferences(window.data()));
    // processor
    processor.reset(new Processor(window.data()));
    // settings
    loadSettings();
    // layout
    // needed to keep .ui fixed size from setupUi
    window->setFixedSize(window->size());
    // event filter
    window->installEventFilter(this);
    // display filter
    presetfilter.reset(new Clickfilter);
    ui->presetBar->installEventFilter(presetfilter.data());
    // color filter
    filedropfilter.reset(new Clickfilter);
    ui->typeBar->installEventFilter(filedropfilter.data());
    // url filter
    preseturlfilter.reset(new Urlfilter);
    ui->presets->installEventFilter(preseturlfilter.data());
    // url filter
    savetourlfilter.reset(new Urlfilter);
    ui->saveTo->installEventFilter(savetourlfilter.data());
    // connect
    connect(ui->togglePreset, &QPushButton::pressed, this, &JobmanPrivate::togglePreset);
    connect(ui->toggleType, &QPushButton::pressed, this, &JobmanPrivate::toggleType);
    connect(presetfilter.data(), &Clickfilter::pressed, ui->togglePreset, &QPushButton::click);
    connect(filedropfilter.data(), &Clickfilter::pressed, ui->toggleType, &QPushButton::click);
    connect(ui->editSubmitFiles, &QAction::triggered, this, &JobmanPrivate::submitFiles);
    connect(ui->editOpenPreferences, &QAction::triggered, this, &JobmanPrivate::openPreferences);
    connect(ui->editClearPreferences, &QAction::triggered, this, &JobmanPrivate::clearPreferences);
    connect(ui->editSavePreferences, &QAction::triggered, this, &JobmanPrivate::savePreferences);
    connect(ui->editImportPreferences, &QAction::triggered, this, &JobmanPrivate::importPreferences);
    connect(ui->editExportPreferences, &QAction::triggered, this, &JobmanPrivate::exportPreferences);
    connect(ui->editRefreshOptions, &QAction::triggered, this, &JobmanPrivate::refreshOptions);
    connect(ui->refreshPresets, &QPushButton::clicked, this, &JobmanPrivate::refreshPresets);
    connect(ui->openPreset, &QPushButton::clicked, this, &JobmanPrivate::openPreset);
    connect(ui->selectPresetsfrom, &QPushButton::clicked, this, &JobmanPrivate::selectPresetsfrom);
    connect(preseturlfilter.data(), &Urlfilter::urlRequested, this, &JobmanPrivate::importPresetsUrl);
    connect(ui->selectSaveto, &QPushButton::clicked, this, &JobmanPrivate::selectSaveto);
    connect(ui->showSaveto, &QPushButton::clicked, this, &JobmanPrivate::showSaveto);
    connect(savetourlfilter.data(), &Urlfilter::urlChanged, this, &JobmanPrivate::saveToUrl);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    connect(ui->copyOriginal, &QCheckBox::checkStateChanged, this, &JobmanPrivate::copyOriginalChanged);
    connect(ui->createFolders, &QCheckBox::checkStateChanged, this, &JobmanPrivate::createFolderChanged);
    connect(ui->overwrite, &QCheckBox::checkStateChanged, this, &JobmanPrivate::overwriteChanged);
#else
    connect(ui->copyOriginal, &QCheckBox::stateChanged, this, &JobmanPrivate::copyOriginalChanged);
    connect(ui->createFolders, &QCheckBox::stateChanged, this, &JobmanPrivate::createFolderChanged);
    connect(ui->overwrite, &QCheckBox::stateChanged, this, &JobmanPrivate::overwriteChanged);
#endif
    connect(ui->filedrop, &Filedrop::filesDropped, this, &JobmanPrivate::processFiles, Qt::QueuedConnection);
    connect(ui->presets, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &JobmanPrivate::presetsChanged);
    connect(ui->threads, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &JobmanPrivate::threadsChanged);
    connect(ui->submit, &QPushButton::pressed, this, &JobmanPrivate::processCommand);
    connect(ui->defaults, &QPushButton::pressed, this, &JobmanPrivate::defaultsPreset);
    connect(ui->helpAbout, &QAction::triggered, this, &JobmanPrivate::openAbout);
    connect(ui->editOpenPresetsFolder, &QAction::triggered, this, &JobmanPrivate::openPresetsFolder);
    connect(ui->editOpenSaveToFolder, &QAction::triggered, this, &JobmanPrivate::openSaveToFolder);
    connect(ui->viewOpenMonitor, &QAction::triggered, this, &JobmanPrivate::openMonitor);
    connect(ui->openMonitor, &QPushButton::clicked, this, &JobmanPrivate::openMonitor);
    connect(ui->editOpenOptions, &QAction::triggered, this, &JobmanPrivate::openOptions);
    connect(ui->openOptions, &QPushButton::clicked, this, &JobmanPrivate::openOptions);
    connect(ui->helpOpenGithubReadme, &QAction::triggered, this, &JobmanPrivate::openGithubReadme);
    connect(ui->helpOpenGithubIssues, &QAction::triggered, this, &JobmanPrivate::openGithubIssues);
    connect(queue.data(), &Queue::jobsProcessed, this, &JobmanPrivate::jobsProcessed);
    connect(processor.data(), &Processor::fileSubmitted, this, &JobmanPrivate::fileSubmitted);
    size = window->size();
    // threads
    ui->threads->setItemDelegate(new MaxJobsDelegate(ui->threads));
    int threadcount = QThread::idealThreadCount();
    for (int i = 1; i <= threadcount; ++i) {
        ui->threads->addItem(QString::number(i), i);
    }
    ui->threads->setCurrentIndex(threads);
    // cpu
    QTimer* timer = new QTimer(window.data());
    QObject::connect(timer, &QTimer::timeout, [&]() {
        if (ui->fileprogress->maximum()) {
            ui->cpu->setText(QString("CPU: %1%").arg(platform::getCpuUsage(), 0, 'f', 0));
        }
        else {
            ui->cpu->setText(QString(""));
        }
    });
    timer->start(1000);
    // stylesheet
    stylesheet();
// debug
#ifdef QT_DEBUG
    QMenu* menu = ui->menubar->addMenu("Debug");
    {
        QAction* action = new QAction("Reload stylesheet...", window.data());
        action->setObjectName("debugReloadStylesheet");
        action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
        menu->addAction(action);
        connect(action, &QAction::triggered, this, &JobmanPrivate::stylesheet);
    }
#endif
    initMcp();
    // presets
    QTimer::singleShot(0, [this]() {
        this->loadPresets();
        this->verifySaveTo();
        this->activate();
    });
}

namespace {
QJsonObject
mcpJobInfo(const QSharedPointer<Job>& job)
{
    const QMetaEnum statuses = QMetaEnum::fromType<Job::Status>();
    QJsonArray arguments;
    for (const QString& argument : job->arguments())
        arguments.append(argument);
    return { { "uuid", job->uuid().toString() },
             { "name", job->name() },
             { "filename", job->filename() },
             { "status", statuses.valueToKey(job->status()) },
             { "command", job->command() },
             { "arguments", arguments },
             { "output", job->output() },
             { "priority", job->priority() },
             { "pid", job->pid() },
             { "dependson", job->dependson().toString() } };
}

bool
ownsWidget(QWidget* root, QWidget* widget)
{
    // QDialog is a separate native window; QWidget::isAncestorOf stops at that boundary.
    for (QObject* object = widget; object; object = object->parent()) {
        if (object == root)
            return true;
    }
    return false;
}
}  // namespace

void
JobmanPrivate::initMcp()
{
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    const bool restore = settings.value("mcpEnabled", false).toBool();
    bool validPort = false;
    const uint savedPort = settings.value("mcpPort", McpServer::defaultPort).toUInt(&validPort);
    if (validPort && savedPort <= 65535)
        mcpport = static_cast<quint16>(savedPort);
    mcpserver.reset(new McpServer(this));
    mcpserver->setTools(mcp::tools(), [this](const QString& name, const QJsonObject& arguments) {
        return callMcpTool(name, arguments);
    });
    const auto rememberJobs = [this](const QList<QSharedPointer<Job>>& jobs) {
        for (const auto& job : jobs) {
            if (job)
                mcpjobs.insert(job->uuid(), job);
        }
    };
    connect(queue.data(), &Queue::jobsSubmitted, this, rememberJobs);
    connect(queue.data(), &Queue::batchSubmitted, this, rememberJobs);
    connect(queue.data(), &Queue::jobsRemoved, this, [this](const QList<QUuid>& uuids) {
        for (const QUuid& uuid : uuids)
            mcpjobs.remove(uuid);
    });
    connect(ui->helpMcpServer, &QAction::triggered, this, &JobmanPrivate::openMcpServer);
    connect(ui->mcpEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled) {
            mcpserver->stop();
        }
        else if (!mcpserver->start(mcpport)) {
            const QSignalBlocker blocker(ui->mcpEnabled);
            ui->mcpEnabled->setChecked(false);
            QSettings(APP_IDENTIFIER, APP_NAME).setValue("mcpEnabled", false);
            Message::showMessage(window.data(), tr("MCP server could not start"), mcpserver->errorString());
        }
    });
    connect(mcpserver.data(), &McpServer::runningChanged, this, [this](bool running) {
        QSettings settings(APP_IDENTIFIER, APP_NAME);
        settings.setValue("mcpEnabled", running);
        settings.setValue("mcpPort", mcpport);
        const QSignalBlocker blocker(ui->mcpEnabled);
        ui->mcpEnabled->setChecked(running);
        ui->mcpEnabled->setToolTip(running ? tr("MCP server listening at %1").arg(mcpserver->url().toString())
                                           : tr("Enable the local MCP server for job control and Qt debugging"));
        if (running)
            qInfo().noquote() << "Jobman MCP:" << mcpserver->url().toString();
    });
    // Let explicit command-line startup choose its port before restoring the saved switch.
    QTimer::singleShot(0, this, [this, restore]() {
        if (restore && !mcpserver->isRunning() && !mcpserver->start(mcpport)) {
            QSettings(APP_IDENTIFIER, APP_NAME).setValue("mcpEnabled", false);
            Message::showMessage(window.data(), tr("MCP server could not start"), mcpserver->errorString());
        }
    });
}

void
JobmanPrivate::openMcpServer()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral(GITHUBURL "/blob/master/docs/MCP.md")));
}

QJsonObject
JobmanPrivate::callMcpTool(const QString& name, const QJsonObject& args)
{
    Q_ASSERT(QThread::currentThread() == window->thread());
    const bool scanning = filedropfuture.isRunning();
    const bool submitting = submitfuture.isRunning();
    const bool busy = scanning || submitting || ui->presettype->currentIndex() == static_cast<int>(Type::Progress);
    QWidget* modal = QApplication::activeModalWidget();
    if (name == "jobman_get_status") {
        const auto preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        return mcp::result(QJsonObject { { "version", APP_VERSION_STRING },
                                         { "scanning", scanning },
                                         { "submitting", submitting },
                                         { "readyForFiles", !busy && ui->filedrop->isEnabled() && !modal },
                                         { "preset", preset ? preset->id() : QString() },
                                         { "outputDirectory", saveto },
                                         { "processing", queue->isProcessing() },
                                         { "maxJobs", queue->threads() },
                                         { "jobCount", qint64(mcpjobs.size()) },
                                         { "filedropText", ui->filedropLabel->text() },
                                         { "filedropPage", ui->presettype->currentIndex() },
                                         { "endpoint", mcpserver->url().toString() } });
    }
    if (name == "jobman_list_presets") {
        QJsonArray presets;
        for (int i = 0; i < ui->presets->count(); ++i) {
            const auto preset = ui->presets->itemData(i).value<QSharedPointer<Preset>>();
            if (preset)
                presets.append(QJsonObject { { "id", preset->id() },
                                             { "name", preset->name() },
                                             { "type", preset->type() },
                                             { "filter", preset->filter() },
                                             { "selected", i == ui->presets->currentIndex() } });
        }
        return mcp::result(presets);
    }
    if (name == "jobman_list_jobs") {
        QJsonArray jobs;
        const qint64 limit = args.contains("limit") ? args.value("limit").toInteger() : 100;
        if (limit < 1)
            return mcp::error("Limit must be a positive integer representable as a signed 64-bit value.");
        auto position = mcpjobs.cbegin();
        const QString cursor = args.value("cursor").toString();
        if (!cursor.isEmpty()) {
            const QUuid uuid(cursor);
            if (uuid.isNull())
                return mcp::error("Cursor must be a job UUID returned by jobman_list_jobs.");
            position = mcpjobs.upperBound(uuid);
        }
        QString last;
        for (; position != mcpjobs.cend(); ++position) {
            if (jobs.size() >= limit)
                break;
            jobs.append(mcpJobInfo(position.value()));
            last = position.key().toString();
        }
        const bool more = position != mcpjobs.cend();
        return mcp::result(QJsonObject { { "jobs", jobs },
                                         { "total", qint64(mcpjobs.size()) },
                                         { "truncated", more },
                                         { "nextCursor", more ? last : QString() } });
    }
    if (name == "jobman_get_job") {
        const QUuid uuid(args.value("uuid").toString());
        const auto job = mcpjobs.value(uuid);
        if (!job)
            return mcp::error("Job UUID was not found.");
        QJsonObject info = mcpJobInfo(job);
        const QString log = job->log();
        const int limit = args.value("logLimit").toInt(65536);
        info.insert("log", limit == 0 ? QString() : log.right(limit));
        info.insert("logTruncated", log.size() > limit);
        return mcp::result(info);
    }
    if (name == "jobman_get_dialog") {
        if (!ownsWidget(window.data(), modal))
            return mcp::result(QJsonObject { { "active", false } });
        QStringList parts;
        for (QObject* current = modal; current; current = current->parent()) {
            parts.prepend(current->objectName());
            if (current == window.data())
                break;
        }
        QJsonArray buttons;
        for (auto* button : modal->findChildren<QAbstractButton*>())
            buttons.append(QJsonObject { { "objectName", button->objectName() },
                                         { "text", button->text() },
                                         { "enabled", button->isEnabled() } });
        QString text;
        if (auto* message = modal->findChild<QTextBrowser*>("message"))
            text = message->toPlainText().left(65536);
        else if (auto* question = modal->findChild<QLabel*>("question"))
            text = question->text();
        QString title = modal->windowTitle();
        if (auto* label = modal->findChild<QLabel*>("title"))
            title = label->text();
        return mcp::result(QJsonObject { { "active", true },
                                         { "class", modal->metaObject()->className() },
                                         { "title", title },
                                         { "text", text },
                                         { "path", parts.join('/') },
                                         { "buttons", buttons } });
    }
    if (name == "jobman_respond_dialog") {
        if (!ownsWidget(window.data(), modal))
            return mcp::error("No application-owned modal dialog is active.");
        const QString response = args.value("response").toString();
        if (auto* message = qobject_cast<Message*>(modal); message && response == "close")
            message->accept();
        else if (auto* question = qobject_cast<Question*>(modal); question && (response == "yes" || response == "no"))
            question->done(response == "yes" ? QDialog::Accepted : QDialog::Rejected);
        else
            return mcp::error(
                "Response must be close for a message, or yes/no for a confirmation. Other dialogs can be operated with Qt actions.");
        return mcp::result(QJsonObject { { "responded", true } });
    }
    if (name == "jobman_get_qt_state" || name == "jobman_qt_action" || name == "jobman_capture_widget")
        return mcp::qtTool(window.data(), name, args);
    if (modal)
        return mcp::error("Respond to the active modal dialog first.");
    if (name == "jobman_select_preset") {
        if (busy)
            return mcp::error("Wait for the current file submission to finish.");
        const QString id = args.value("id").toString();
        for (int i = 0; i < ui->presets->count(); ++i) {
            const auto preset = ui->presets->itemData(i).value<QSharedPointer<Preset>>();
            if (preset && preset->id() == id) {
                ui->presets->setCurrentIndex(i);
                return mcp::result(QJsonObject { { "selected", id } });
            }
        }
        return mcp::error("Preset id was not found.");
    }
    if (name == "jobman_set_output_directory") {
        if (busy)
            return mcp::error("Wait for the current file submission to finish.");
        const QFileInfo info(args.value("path").toString());
        if (!info.isAbsolute() || !info.isDir())
            return mcp::error("Output path must be an absolute path to an existing directory.");
        saveto = info.absoluteFilePath();
        setSaveto(saveto);
        activate();
        return mcp::result(QJsonObject { { "outputDirectory", saveto } });
    }
    if (name == "jobman_submit_files") {
        if (busy || !ui->filedrop->isEnabled())
            return mcp::error("Jobman is not ready for another file drop.");
        const auto preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        if (!preset || preset->type() != "file")
            return mcp::error("Select a file preset first.");
        const QJsonArray paths = args.value("paths").toArray();
        if (paths.isEmpty())
            return mcp::error("Submit at least one file/directory path.");
        QList<QString> files;
        for (const QJsonValue& value : paths) {
            if (!value.isString())
                return mcp::error("Each path must be a string.");
            const QFileInfo info(value.toString());
            if (!info.isAbsolute() || (!info.isFile() && !info.isDir()))
                return mcp::error("Each path must be an absolute path to an existing local file or directory.");
            files.append(info.absoluteFilePath());
        }
        processFiles(files);
        return mcp::result(QJsonObject { { "acceptedForScanning", true } });
    }
    if (name == "jobman_control_job") {
        const auto job = mcpjobs.value(QUuid(args.value("uuid").toString()));
        if (!job)
            return mcp::error("Job UUID was not found.");
        const QString action = args.value("action").toString();
        const auto status = job->status();
        if (action == "start" && status == Job::Stopped)
            queue->start(job->uuid());
        else if (action == "stop" && status == Job::Running && job->pid() > 0)
            queue->stop(job->uuid());
        else if (action == "restart" && status != Job::Running && status != Job::Waiting) {
            for (const auto& dependent : mcpjobs) {
                QUuid parent = dependent->dependson();
                QSet<QUuid> visited;
                while (!parent.isNull() && !visited.contains(parent)) {
                    visited.insert(parent);
                    if (parent == job->uuid()
                        && (dependent->status() == Job::Running || dependent->status() == Job::Waiting))
                        return mcp::error("Wait for dependent jobs to finish before restarting this job.");
                    const auto ancestor = mcpjobs.value(parent);
                    parent = ancestor ? ancestor->dependson() : QUuid();
                }
            }
            queue->restart(job->uuid());
        }
        else if (action == "remove")
            queue->remove(job->uuid());
        else
            return mcp::error("Unsupported action, or the job is not in a valid state for that action.");
        return mcp::result(QJsonObject { { "uuid", job->uuid().toString() }, { "action", action } });
    }
    if (name == "jobman_save_preferences") {
        savePreferences();
        return mcp::result(QJsonObject { { "saved", true } });
    }
    return mcp::error("Unknown Jobman tool.");
}


void
JobmanPrivate::stylesheet()
{
    ::ui()->loadStyleSheet();
}

Paths
JobmanPrivate::paths()
{
    Paths paths;
    paths.overwrite = overwrite;
    paths.copyoriginal = copyoriginal;
    paths.createpaths = createfolders;
    paths.searchpaths = documents;
    paths.outputpath = saveto;
    return paths;
}

bool
JobmanPrivate::applicationPath(const QString& path) const
{
    QDir appdir(platform::getApplicationPath());
    QDir dir(path);
    QString canonicalAppDir = appdir.canonicalPath();
    QString canonicalDir = dir.canonicalPath();
    if (canonicalAppDir.isEmpty() || canonicalDir.isEmpty()) {
        return false;
    }
    return canonicalDir == canonicalAppDir || canonicalDir.startsWith(canonicalAppDir + QDir::separator());
}

void
JobmanPrivate::activate()
{
    if (!saveto.isEmpty() && (ui->presets->count() > 0 && ui->presets->itemText(0) != "No presets found")) {
        enable(true);
    }
    else {
        enable(false);
    }
}

void
JobmanPrivate::submitFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(window.data(), tr("Select files"), filesfrom,
                                                      tr("All files (*)"));
    if (!files.isEmpty()) {
        QFileInfo fileInfo(files.first());
        filesfrom = fileInfo.absolutePath();

        QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        processUuids(processor->submit(files, preset, paths()));
    }
}

void
JobmanPrivate::openPreferences()
{
    preferences->exec();
}

void
JobmanPrivate::clearPreferences()
{
    if (Question::askQuestion(
            window.data(),
            "All values including search paths, environent variables and options will be reset to their default settings.\n"
            "Do you want to continue?")) {
        QSettings settings(APP_IDENTIFIER, APP_NAME);
        settings.clear();
        // update
        optionsdialog.reset(new OptionsDialog(window.data()));
        preferences.reset(new Preferences(window.data()));
        loadSettings();
        loadPresets();
        saveSettings();
        verifySaveTo();
        activate();
    }
}

void
JobmanPrivate::savePreferences()
{
    saveSettings();
}

void
JobmanPrivate::importPreferences()
{
    QString filename = QFileDialog::getOpenFileName(window.data(), tr("Import preferences file ..."), preferencesfrom,
                                                    tr("JSON Files (*.json)"));
    if (!filename.isEmpty()) {
        QFile file(filename);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray jsonData = file.readAll();
            file.close();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
            if (!jsonDoc.isObject()) {
                Message::showMessage(window.data(), "Invalid preferences file",
                                     "The selected file is not a valid json preferences file.");
                return;
            }
            QJsonObject jsonobj = jsonDoc.object();
            QSettings settings(APP_IDENTIFIER, APP_NAME);
            for (auto it = jsonobj.constBegin(); it != jsonobj.constEnd(); ++it) {
                const QString& key = it.key();
                const QJsonValue& value = it.value();
                if (value.isString()) {
                    settings.setValue(key, value.toString());
                }
                else if (value.isDouble()) {
                    settings.setValue(key, value.toDouble());
                }
                else if (value.isBool()) {
                    settings.setValue(key, value.toBool());
                }
                else if (value.isArray()) {
                    QJsonArray jsonArray = value.toArray();
                    QVariantList variantList;
                    for (const QJsonValue& arrayValue : jsonArray) {
                        variantList.append(arrayValue.toVariant());
                    }
                    settings.setValue(key, variantList);
                }
            }
            // update
            optionsdialog.reset(new OptionsDialog(window.data()));
            preferences.reset(new Preferences(window.data()));
            loadSettings();
            loadPresets();

            QFileInfo fileInfo(file.fileName());
            preferencesfrom = fileInfo.absolutePath();
            saveSettings();
        }
        else {
            Message::showMessage(window.data(), "Could not open file for reading",
                                 QString("Error message: %1").arg(file.errorString()));
        }
    }
}

void
JobmanPrivate::exportPreferences()
{
    QString filename = QFileDialog::getSaveFileName(window.data(), tr("Export preferences file ..."), preferencesfrom,
                                                    tr("JSON Files (*.json)"));
    if (!filename.isEmpty()) {
        if (!filename.toLower().endsWith(".json")) {
            filename += ".json";
        }
        QSettings settings(APP_IDENTIFIER, APP_NAME);
        QJsonObject jsonobj;
        QStringList internalprefix = {
            "Apple", "com/apple", "Nav", "AK", "PK", "NS", "Country", "MultipleSessionEnabled"  // safe to ignore
        };
        for (const QString& key : settings.allKeys()) {
            bool isinternal = false;
            for (const QString& prefix : internalprefix) {
                if (key.startsWith(prefix)) {
                    isinternal = true;
                    break;
                }
            }
            if (!isinternal) {
                QVariant value = settings.value(key);
                switch (value.typeId()) {
                case QMetaType::QString: jsonobj[key] = value.toString(); break;
                case QMetaType::Int: jsonobj[key] = value.toInt(); break;
                case QMetaType::Double: jsonobj[key] = value.toDouble(); break;
                case QMetaType::Bool: jsonobj[key] = value.toBool(); break;
                case QMetaType::LongLong: jsonobj[key] = value.toLongLong(); break;
                case QMetaType::QStringList: jsonobj[key] = QJsonArray::fromStringList(value.toStringList()); break;
                case QMetaType::QVariantList: {
                    QJsonArray jsonarray;
                    for (const QVariant& item : value.toList()) {
                        jsonarray.append(QJsonValue::fromVariant(item));
                    }
                    jsonobj[key] = jsonarray;
                } break;
                }
            }
        }
        QJsonDocument jsonDoc(jsonobj);
        QFile file(filename);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(jsonDoc.toJson(QJsonDocument::Indented));
            file.close();

            QFileInfo fileInfo(file.fileName());
            preferencesfrom = fileInfo.absolutePath();
        }
        else {
            Message::showMessage(window.data(), "Could open file for writing",
                                 QString("Error message: %1").arg(file.errorString()));
        }
    }
}

void
JobmanPrivate::enable(bool enable)
{
    ui->filedrop->setEnabled(enable);
    ui->fileprogress->setEnabled(enable);
    ui->options->setEnabled(enable);
    ui->openOptions->setEnabled(enable);
}

bool
JobmanPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::ScreenChangeInternal) {
        stylesheet();
    }
    if (event->type() == QEvent::Close) {
        if (filedropfuture.isRunning() || submitfuture.isRunning()) {
            if (Question::askQuestion(window.data(), "File drop is still in progress.\n"
                                                     "Do you want to cancel them and quit?")) {
                filedropfuture.cancel();
                filedropfuture.waitForFinished();
                submitfuture.cancel();
                submitfuture.waitForFinished();
                return true;
            }
            else {
                event->ignore();
                return true;
            }
        }
        if (queue->isProcessing()) {
            if (Question::askQuestion(window.data(), "Jobs are in progress, are you sure you want to quit?")) {
                saveSettings();
                return true;
            }
            else {
                event->ignore();
                return true;
            }
        }
        saveSettings();
        clearPresets();
        return true;
    }
    return QObject::eventFilter(object, event);
}

void
JobmanPrivate::verifySaveTo()
{
    if (saveto.isEmpty()) {
        if (Question::askQuestion(window.data(), "No save to folder selected for output files.\n"
                                                 "Would you like to choose one now?")) {
            QString dir
                = QFileDialog::getExistingDirectory(window.data(),
                                                    tr("No save to folder selected for output files ..."),
                                                    QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (!dir.isEmpty()) {
                saveto = dir;
                setSaveto(saveto);
            }
        }
    }
}

void
JobmanPrivate::loadSettings()
{
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    filesfrom = settings.value("filesfrom", documents).toString();
    preferencesfrom = settings.value("preferencesfrom", documents).toString();
    presetsselected = settings.value("presetsselected", "").toString();
    presetsfrom = settings.value("presetsfrom", presets).toString();
    {
        QString bookmark = settings.value("presetsfrombookmark").toString();
        if (!bookmark.isEmpty()) {
            QString resolvedPath = platform::resolveBookmark(bookmark);
            if (!resolvedPath.isEmpty()) {
                presetsfrom = resolvedPath;
            }
        }
    }
    saveto = settings.value("saveto", "").toString();
    {
        QString bookmark = settings.value("savetobookmark").toString();
        if (!bookmark.isEmpty()) {
            QString resolvedPath = platform::resolveBookmark(bookmark);
            if (!resolvedPath.isEmpty()) {
                saveto = resolvedPath;
            }
        }
    }
    copyoriginal = settings.value("copyoriginal", true).toBool();
    createfolders = settings.value("createfolders", true).toBool();
    overwrite = settings.value("overwrite", true).toBool();
    threads = settings.value("threads", 0).toInt();
    // ui
    setSaveto(saveto);
    ui->copyOriginal->setChecked(copyoriginal);
    ui->createFolders->setChecked(createfolders);
    ui->overwrite->setChecked(overwrite);
    ui->threads->setCurrentIndex(threads);
}

void
JobmanPrivate::saveSettings()
{
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    settings.setValue("filesfrom", filesfrom);
    settings.setValue("preferencesfrom", preferencesfrom);
    if (ui->presets->count()) {
        QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        if (!preset.isNull()) {  // skip "No presets found"
            settings.setValue("presetsselected", preset->filename());
        }
    }
    settings.setValue("presetsfrom", presetsfrom);
    {
        if (QDir(presetsfrom).exists()) {  // must exist
            QString bookmark = platform::saveBookmark(presetsfrom);
            if (!bookmark.isEmpty()) {
                settings.setValue("presetsfrombookmark", bookmark);
            }
        }
    }
    settings.setValue("saveto", saveto);
    {
        if (QDir(saveto).exists()) {  // must exist
            QString bookmark = platform::saveBookmark(saveto);
            if (!bookmark.isEmpty()) {
                settings.setValue("savetobookmark", bookmark);
            }
        }
    }
    settings.setValue("copyoriginal", copyoriginal);
    settings.setValue("createfolders", createfolders);
    settings.setValue("overwrite", overwrite);
    for (int i = 0; i < ui->presets->count(); ++i) {
        if (ui->presets->itemText(i) != "No presets found") {
            QVariant data = ui->presets->itemData(i);
            QSharedPointer<Preset> preset = data.value<QSharedPointer<Preset>>();
            settings.beginGroup(QString("preset/%1").arg(preset->id()));
            for (QSharedPointer<Option> option : preset->options()) {
                settings.setValue(QString("option/%1/value").arg(option->id), option->value);
                settings.setValue(QString("option/%1/enabled").arg(option->id), option->enabled);
            }
            settings.endGroup();
        }
    }
    settings.setValue("threads", ui->threads->currentIndex());
}

void
JobmanPrivate::loadPresets()
{
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    ui->presets->clear();
    QDir presets(presetsfrom);
    QFileInfoList presetfiles = presets.entryInfoList(QStringList("*.json"));
    QString error;
    if (presetfiles.count() > 0) {
        QMap<QString, QString> presetIds;
        for (QFileInfo presetfile : presetfiles) {
            QSharedPointer<Preset> preset(new Preset());
            QString filename = presetfile.absoluteFilePath();
            if (preset->read(filename)) {
                if (presetIds.contains(preset->id())) {
                    error += QString("%1\n\nDuplicate preset id \"%2\"; already used by %3.\n")
                                 .arg(filename, preset->id(), presetIds.value(preset->id()));
                    continue;
                }
                presetIds.insert(preset->id(), filename);
                settings.beginGroup(QString("preset/%1").arg(preset->id()));
                for (QSharedPointer<Option> option : preset->options()) {
                    QString valuekey = QString("option/%1/value").arg(option->id);
                    if (settings.contains(valuekey)) {
                        option->value = settings.value(valuekey);
                    }
                    QString enabledkey = QString("option/%1/enabled").arg(option->id);
                    if (settings.contains(enabledkey)) {
                        option->enabled = settings.value(enabledkey).toBool();
                    }
                }
                settings.endGroup();
                ui->presets->addItem(preset->name(), QVariant::fromValue(preset));
                if (filename == presetsselected) {
                    ui->presets->setCurrentIndex(ui->presets->count() - 1);
                }
            }
            else {
                if (error.length() > 0) {
                    error += "\n";
                }
                error += QString("Could not load preset from file:\n"
                                 "%1\n\n"
                                 "%2\n")
                             .arg(presetfile.absoluteFilePath())
                             .arg(preset->error());
            }
        }
        if (error.length() > 0) {
            Message::showMessage(window.data(), "Could not load all presets", error);
        }
    }
    if (ui->presets->count()) {
        ui->openPreset->setEnabled(true);
    }
    else {
        ui->presets->addItem("No presets found");
        ui->openPreset->setEnabled(false);
    }
}

void
JobmanPrivate::clearPresets()
{
    for (int i = 0; i < ui->presets->count(); ++i) {
        QVariant data = ui->presets->itemData(i);
        QSharedPointer<Preset> preset = data.value<QSharedPointer<Preset>>();
        preset.clear();
    }
}

void
JobmanPrivate::processFiles(const QList<QString>& files)
{
    if (filedropfuture.isRunning() || submitfuture.isRunning()
        || ui->presettype->currentIndex() == static_cast<int>(Type::Progress)) {
        return;
    }
    QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
    if (!preset)
        return;
    const QString filter = preset->filter().toLower();
    const QStringList filters = filter.split(';', Qt::SkipEmptyParts);
    const auto finishDrop = [this]() {
        ui->filedropLabel->clear();
        ui->filedropProgress->setValue(0);
        ui->filedropProgress->setVisible(false);
        ui->presettype->setCurrentIndex(static_cast<int>(Type::File));
        ui->progressWidget->setVisible(true);
    };
    ui->presettype->setCurrentIndex(static_cast<int>(Type::Progress));
    ui->filedropLabel->setText(tr("Checking files..."));
    ui->filedropProgress->setValue(0);
    ui->filedropProgress->setVisible(false);
    ui->progressWidget->setVisible(false);
    // Scan and filter without accessing widgets or the shared future from the worker.
    filedropfuture = QtConcurrent::run([files, filters](QPromise<FileDrop>& promise) {
        FileDrop result;
        QList<QString> allItems;
        for (const QString& path : files) {
            if (promise.isCanceled())
                return;
            QFileInfo info(path);
            if (info.isDir()) {
                result.hasDir = true;
                QDirIterator it(path, filters, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    if (promise.isCanceled())
                        return;
                    allItems.append(it.next());
                }
            }
            else if (info.isFile()) {
                allItems.append(path);
            }
        }
        for (const QString& path : allItems) {
            if (promise.isCanceled())
                return;
            QFileInfo info(path);
            if (filters.contains("*.*") || QDir::match(filters, info.fileName().toLower())) {
                result.submitfiles.append(info.absoluteFilePath());
            }
            else {
                result.rejectedfiles.append(info.fileName());
            }
        }
        promise.addResult(result);
    });
    QFutureWatcher<FileDrop>* filedropwatcher = new QFutureWatcher<FileDrop>(this);
    connect(
        filedropwatcher, &QFutureWatcher<FileDrop>::finished, this,
        [this, filedropwatcher, preset, filter, finishDrop]() {
            const QFuture<FileDrop> future = filedropwatcher->future();
            filedropwatcher->deleteLater();
            if (future.isCanceled()) {
                finishDrop();
                return;
            }
            const FileDrop result = future.result();
            if (!result.rejectedfiles.isEmpty()) {
                Message::showMessage(
                    window.data(), "Files Skipped",
                    QString(
                        "The following files were skipped because they do not match the preset's filter:\n%1\n\nFilter:\n%2")
                        .arg(result.rejectedfiles.join("\n"))
                        .arg(filter));
            }
            if (result.submitfiles.isEmpty()) {
                finishDrop();
                return;
            }
            if (result.submitfiles.size() > 10 && result.hasDir
                && !Question::askQuestion(
                    window.data(),
                    QString("You are about to submit %1 files for processing from one or more directories.\n"
                            "Do you want to continue?")
                        .arg(result.submitfiles.size()))) {
                finishDrop();
                return;
            }
            submittotal = static_cast<int>(result.submitfiles.size());
            submitcount = 0;
            ui->filedropProgress->setVisible(true);
            const Paths submissionPaths = paths();
            submitfuture = QtConcurrent::run([this, result, preset, submissionPaths]() {
                return processor->submit(result.submitfiles, preset, submissionPaths);
            });
            QFutureWatcher<QList<QUuid>>* submitwatcher = new QFutureWatcher<QList<QUuid>>(this);
            connect(submitwatcher, &QFutureWatcher<QList<QUuid>>::finished, this, [this, submitwatcher, finishDrop]() {
                const QFuture<QList<QUuid>> future = submitwatcher->future();
                submitwatcher->deleteLater();
                if (!future.isCanceled())
                    processUuids(future.result());
                finishDrop();
            });
            submitwatcher->setFuture(submitfuture);
        });
    filedropwatcher->setFuture(filedropfuture);
}

void
JobmanPrivate::processCommand()
{
    QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
    processUuids(QList<QUuid> { processor->submit(preset, paths()) });
}

void
JobmanPrivate::processUuids(const QList<QUuid>& uuids)
{
    int value = 0;
    for (const QUuid& uuid : uuids) {
        if (!processeduuids.contains(uuid)) {
            waitinguuids.append(uuid);
        }
        else {
            value++;
            processeduuids.removeAll(uuid);  // skip, already processed
        }
    }
    ui->fileprogress->setValue(ui->fileprogress->value() + value);
    ui->fileprogress->setMaximum(ui->fileprogress->maximum() + static_cast<int>(uuids.count()));
    if (!ui->progressWidget->currentIndex()) {
        ui->progressWidget->setCurrentIndex(1);
    }
}

void
JobmanPrivate::jobsProcessed(const QList<QUuid>& uuids)
{
    for (const QUuid& uuid : uuids) {
        if (waitinguuids.contains(uuid)) {
            const int progress = ui->fileprogress->value() + 1;

            waitinguuids.removeAll(uuid);

            if (progress >= ui->fileprogress->maximum()) {
                ui->fileprogress->setValue(0);
                ui->fileprogress->setMaximum(0);
                ui->fileprogress->setToolTip(QString());
                ui->progressWidget->setCurrentIndex(0);
            }
            else {
                ui->fileprogress->setValue(progress);

                const int maximum = ui->fileprogress->maximum();
                QString tooltip = QString("Completed jobs: %1/%2").arg(progress).arg(maximum);

                const int percentage = maximum > 0 ? (progress * 100) / maximum : 0;
                if (percentage > 0 && percentage < 100) {
                    tooltip.append(QString(" - %1%").arg(percentage));
                }

                ui->fileprogress->setToolTip(tooltip);
            }
        }
        else if (!processeduuids.contains(uuid)) {
            processeduuids.append(uuid);
        }
    }
}

void
JobmanPrivate::fileSubmitted(const QString& file)
{
    const int progress = (submittotal > 0) ? static_cast<int>((submitcount * 100) / submittotal) : 0;
    const int percentage = progress;

    const QString filename = QFileInfo(file).fileName();
    const QString prefix = "Submitted file: ";
    const QString suffix = QString(" - %1/%2 %3%").arg(submitcount).arg(submittotal).arg(percentage);

    const int width = ui->filedropLabel->width();
    QFontMetrics metrics(ui->filedropLabel->font());
    const int availableWidth = width - metrics.horizontalAdvance(prefix) - metrics.horizontalAdvance(suffix);

    const QString text = metrics.elidedText(filename, Qt::ElideMiddle, qMax(0, availableWidth));

    ui->filedropLabel->setText(prefix + text + suffix);
    ui->filedropProgress->setValue(progress);
    submitcount++;
}

void
JobmanPrivate::refreshOptions()
{
    saveSettings();
    refreshPresets();
}

void
JobmanPrivate::refreshPresets()
{
    loadPresets();
    activate();
}

void
JobmanPrivate::openPreset()
{
    if (ui->presets->count()) {
        QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        QDesktopServices::openUrl(QUrl::fromLocalFile(preset->filename()));
    }
}

void
JobmanPrivate::selectPresetsfrom()
{
    QString dir = QFileDialog::getExistingDirectory(window.data(), tr("Select preset folder ..."),
                                                    presetsfrom == presets
                                                        ? documents
                                                        : presetsfrom,  // open will always open from Documents
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        if (dir != presetsfrom) {  // skip if the same directory
            QDir selectdir(dir);
            selectdir.setNameFilters(QStringList() << "*.json");
            if (selectdir.entryList(QDir::Files).isEmpty()) {
                if (Question::askQuestion(window.data(), "The directory has no presets.\n"
                                                         "Copy the built-in preset as a template?")) {
                    QDir presetsdir(presets);
                    for (const QString& fileName : presetsdir.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
                        QString jsonfile = presetsdir.filePath(fileName);
                        QString copyfile = selectdir.filePath(QFileInfo(jsonfile).fileName());
                        if (QFile::copy(jsonfile, copyfile)) {
                            QFile file(copyfile);
                            if (file.open(QIODevice::ReadOnly)) {
                                QByteArray jsonData = file.readAll();
                                file.close();
                                QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
                                QJsonObject jsonobj = jsonDoc.object();
                                jsonobj["id"] = "template_" + jsonobj["id"].toString();
                                QString name = jsonobj["name"].toString();
                                jsonobj["name"] = name.replace("Internal:", "Template:");
                                jsonDoc.setObject(jsonobj);
                                if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                    file.write(jsonDoc.toJson(QJsonDocument::Indented));
                                    file.close();
                                }
                                else {
                                    Message::showMessage(window.data(), "Could open file for writing",
                                                         QString("Error message: %1").arg(file.errorString()));
                                    return;
                                }
                            }
                            else {
                                Message::showMessage(window.data(), "Could open file for reading",
                                                     QString("Error message: %1").arg(file.errorString()));
                                return;
                            }
                        }
                        else {
                            Message::showMessage(
                                window.data(), "Could not copy presets",
                                QString("Failed when trying to copy presets to selected directory: %1").arg(dir));
                            return;
                        }
                    }
                }
            }
            presetsfrom = dir;
            refreshPresets();
        }
    }
}

void
JobmanPrivate::importPresetsUrl(const QUrl& url)
{
    if (presetsfrom == presets) {
        return;  // internal presets
    }
    QString path = url.toLocalFile();
    QFileInfo fileinfo(path);

    if (!applicationPath(presetsfrom)) {
        QDir presetsdir(presetsfrom);
        QStringList addedfiles;
        QStringList alreadyexistsfiles;
        QStringList invalidfiles;

        if (fileinfo.isDir()) {
            QDir dir(path);
            dir.setNameFilters(QStringList() << "*.json");
            QStringList jsonFiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
            if (jsonFiles.isEmpty()) {
                return;
            }
            for (const QString& filename : jsonFiles) {
                QString path = dir.filePath(filename);
                QString target = presetsdir.filePath(filename);
                if (QFile::exists(target)) {
                    alreadyexistsfiles.append(filename);
                    continue;
                }
                QSharedPointer<Preset> preset(new Preset());
                if (preset->read(path)) {
                    QFile::copy(path, target);
                    addedfiles.append(filename);
                }
                else {
                    invalidfiles.append(filename);
                }
            }
            QString message = QString("Preset import from directory:\n%1\n\n").arg(path);
            if (!addedfiles.isEmpty()) {
                message += "Added:\n" + addedfiles.join("\n") + "\n\n";
            }
            if (!alreadyexistsfiles.isEmpty()) {
                message += "Already exists:\n" + alreadyexistsfiles.join("\n") + "\n\n";
            }
            if (!invalidfiles.isEmpty()) {
                message += "Invalid preset files:\n" + invalidfiles.join("\n");
            }
            Message::showMessage(window.data(), "Preset import summary", message);
        }
        else if (fileinfo.isFile() && fileinfo.suffix().toLower() == "json") {
            QString filename = fileinfo.fileName();
            QString path = fileinfo.filePath();
            QString target = presetsdir.filePath(fileinfo.fileName());
            if (QFile::exists(target)) {
                Message::showMessage(window.data(), "File skipped",
                                     QString("The file '%1' was skipped because it already exists.").arg(filename));
            }
            else {
                QSharedPointer<Preset> preset(new Preset());
                if (preset->read(path)) {
                    QFile::copy(path, target);
                    Message::showMessage(window.data(), "File added",
                                         QString("The file '%1' was added successfully.").arg(filename));
                }
                else {
                    Message::showMessage(window.data(), "Invalid preset file",
                                         QString("The file '%1' was invalid because it already exists.\n"
                                                 "Error: %2")
                                             .arg(filename)
                                             .arg(preset->error()));
                }
            }
        }
        else {
            return;
        }
        refreshPresets();
    }
    else {
        Message::showMessage(
            window.data(), "Invalid preset directory",
            "The preset directory is invalid because it is inside the application path. Please select a different preset folder.");
    }
}

void
JobmanPrivate::selectSaveto()
{
    QString dir = QFileDialog::getExistingDirectory(window.data(), tr("Select save to folder ..."),
                                                    saveto.size() ? saveto : documents,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        saveto = dir;
        setSaveto(saveto);
        activate();
    }
}

void
JobmanPrivate::showSaveto()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(saveto));
}

void
JobmanPrivate::setSaveto(const QString& text)
{
    if (!text.isEmpty()) {
        QFontMetrics metrics(ui->saveTo->font());
        ui->saveTo->setText(metrics.elidedText(text, Qt::ElideRight, ui->saveTo->maximumSize().width()));
        ui->showSaveto->setVisible(true);
    }
    else {
        ui->saveTo->setText("No folder selected");
        ui->showSaveto->setVisible(false);
    }
}

void
JobmanPrivate::saveToUrl(const QUrl& url)
{
    QString localFile = url.toLocalFile();
    if (QDir(localFile).exists()) {
        saveto = url.toLocalFile();
        ui->showSaveto->setVisible(true);
        activate();
    }
}

void
JobmanPrivate::copyOriginalChanged(int state)
{
    copyoriginal = (state == Qt::Checked);
}

void
JobmanPrivate::createFolderChanged(int state)
{
    createfolders = (state == Qt::Checked);
}

void
JobmanPrivate::overwriteChanged(int state)
{
    overwrite = (state == Qt::Checked);
}

void
JobmanPrivate::presetsChanged(int index)
{
    if (ui->presets->currentData().isValid()) {
        QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        bool enabled = false;
        if (preset->options().size()) {
            enabled = true;
        }
        if (preset->type() == "file") {
            ui->presettype->setCurrentIndex(0);
            ui->type->setText("Filedrop");
            ui->openOptions->setVisible(true);
        }
        else {
            ui->presettype->setCurrentIndex(1);
            ui->type->setText("Command");
            QString objectname = ui->optionsWidget->objectName();
            {
                delete ui->optionsWidget;  // needed for dynamic layout
                ui->optionsWidget = nullptr;
            }
            ui->optionsWidget = new OptionsWidget();
            ui->optionsWidget->setObjectName(objectname);
            ui->optionsWidget->update(preset);
            ui->scrollarea->setWidget(ui->optionsWidget);
            ui->openOptions->setVisible(false);
        }
        ui->editSubmitFiles->setEnabled(enabled);
        ui->editOpenOptions->setEnabled(enabled);
        ui->openOptions->setEnabled(enabled);
    }
}

void
JobmanPrivate::threadsChanged(int index)
{
    queue->setThreads(ui->threads->itemText(index).toInt());
}

void
JobmanPrivate::defaultsPreset()
{
    if (Question::askQuestion(window.data(), "All values will be reset to their default settings.\n"
                                             "Do you want to continue?")) {
        QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        for (QSharedPointer<Option> option : preset->options()) {
            option->value = option->defaultvalue;
            if (option->toggle.isEmpty()) {
                option->enabled = true;
            }
            else {
                option->enabled = false;
            }
        }
        presetsChanged(ui->presets->currentIndex());
    }
}

void
JobmanPrivate::togglePreset()
{
    int height = ui->presetWidget->height();
    if (ui->togglePreset->isChecked()) {
        ::ui()->setIcon(ui->togglePreset, UI::Collapse);
        ui->presetWidget->show();
        window->setFixedSize(window->width(), size.height() + height);
    }
    else {
        ::ui()->setIcon(ui->togglePreset, UI::Expand);
        ui->presetWidget->hide();
        window->setFixedSize(window->width(), size.height() - height);
    }
    size = window->size();
}

void
JobmanPrivate::toggleType()
{
    int height = ui->typeWidget->height();
    if (ui->toggleType->isChecked()) {
        ::ui()->setIcon(ui->toggleType, UI::Collapse);
        ui->typeWidget->show();
        window->setFixedSize(window->width(), size.height() + height);
    }
    else {
        ::ui()->setIcon(ui->toggleType, UI::Expand);
        ui->typeWidget->hide();
        window->setFixedSize(window->width(), size.height() - height);
    }
    size = window->size();
}

void
JobmanPrivate::openAbout()
{
    about->exec();
}

void
JobmanPrivate::openPresetsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(presetsfrom));
}

void
JobmanPrivate::openSaveToFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(saveto));
}

void
JobmanPrivate::openMonitor()
{
    if (monitor->isVisible()) {
        monitor->raise();
    }
    else {
        monitor->show();
    }
}

void
JobmanPrivate::openOptions()
{
    QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
    optionsdialog->update(preset);
    if (optionsdialog->isVisible()) {
        optionsdialog->raise();
        optionsdialog->activateWindow();
    }
    else {
        optionsdialog->show();
    }
}

void
JobmanPrivate::openGithubReadme()
{
    QDesktopServices::openUrl(QUrl(QString(GITHUBURL) + "/blob/master/README.md"));
}

void
JobmanPrivate::openGithubIssues()
{
    QDesktopServices::openUrl(QUrl(QString(GITHUBURL) + "/issues"));
}

#include "jobman.moc"

Jobman::Jobman()
    : QMainWindow(nullptr, Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint
                               | Qt::WindowMinimizeButtonHint)
    , p(new JobmanPrivate())
{
    p->window = this;
    p->init();
}

Jobman::~Jobman() {}

bool
Jobman::startMcpServer(quint16 port)
{
    if (!p->mcpserver->isRunning())
        p->mcpport = port;
    return p->mcpserver->start(port);
}

void
Jobman::stopMcpServer()
{
    p->mcpserver->stop();
}

bool
Jobman::isMcpServerRunning() const
{
    return p->mcpserver->isRunning();
}

QUrl
Jobman::mcpServerUrl() const
{
    return p->mcpserver->url();
}

QString
Jobman::mcpServerError() const
{
    return p->mcpserver->errorString();
}
