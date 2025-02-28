// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "jobman.h"
#include "clickfilter.h"
#include "icctransform.h"
#include "message.h"
#include "monitor.h"
#include "options.h"
#include "platform.h"
#include "preferences.h"
#include "preset.h"
#include "process.h"
#include "question.h"
#include "queue.h"
#include "urlfilter.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QWindow>
#include <QDebug>

// generated files
#include "ui_about.h"
#include "ui_jobman.h"

class JobmanPrivate : public QObject
{
    Q_OBJECT
    public:
        JobmanPrivate();
        void init();
        void stylesheet();
        void profile();
        void activate();
        void deactivate();
        void enable(bool enable);
        bool eventFilter(QObject* object, QEvent* event);
        void verifySettings();
        void loadSettings();
        void saveSettings();
    
    public Q_SLOTS:
        void loadPresets();
        void clearPresets();
        void togglePreset();
        void toggleFiledrop();
        void openPresetsFolder();
        void openSaveToFolder();
        void openMonitor();
        void openOptions();
        void run(const QList<QString>& files);
        void jobProcessed(const QUuid& uuid);
        void addFiles();
        void openPreferences();
        void clearPreferences();
        void savePreferences();
        void importPreferences();
        void exportPreferences();
        void refreshPresets();
        void openPreset();
        void selectPresetsfrom();
        void presetsUrl(const QUrl& url);
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
        void openGithubReadme();
        void openGithubIssues();
    
    Q_SIGNALS:
        void readOnly(bool readOnly);

    public:
        class About : public QDialog
        {
            public: About(QWidget *parent = nullptr)
            : QDialog(parent)
            {
                QScopedPointer<Ui_About> about;
                about.reset(new Ui_About());
                about->setupUi(this);
                about->name->setText(APP_NAME);
                about->version->setText(APP_VERSION_STRING);
                about->copyright->setText(APP_COPYRIGHT);
                QString url = GITHUBURL;
                about->github->setText(QString("Github project: <a href='%1'>%1</a>").arg(url));
                about->github->setTextFormat(Qt::RichText);
                about->github->setTextInteractionFlags(Qt::TextBrowserInteraction);
                about->github->setOpenExternalLinks(true);
                QFile file(":/files/resources/Copyright.txt");
                file.open(QIODevice::ReadOnly | QIODevice::Text);
                QTextStream in(&file);
                QString text = in.readAll();
                file.close();
                about->licenses->setText(text);
            }
        };
        QString replacePaths(const QString& input, const QString& pattern, const QFileInfo& inputinfo);
        QString replaceFiles(const QString& input, const QFileInfo& inputinfo, const QFileInfo& outputinfo);
        QString replaceTask(const QString& input, const QString& inputinfo, const QString& outputinfo);
        QStringList replaceOptions(QList<Option*> options, const QString& input);
        int width;
        int height;
        QSize size;
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
        QMap<QString, QList<QUuid>> processedfiles;
        QPointer<Queue> queue;
        QPointer<Jobman> window;
        QScopedPointer<About> about;
        QScopedPointer<Monitor> monitor;
        QScopedPointer<Options> options;
        QScopedPointer<Preferences> preferences;
        QScopedPointer<Clickfilter> presetfilter;
        QScopedPointer<Clickfilter> filedropfilter;
        QScopedPointer<Urlfilter> preseturlfilter;
        QScopedPointer<Urlfilter> savetourlfilter;
        QScopedPointer<Ui_Jobman> ui;
};

JobmanPrivate::JobmanPrivate()
: width(128)
, height(128)
{
    qRegisterMetaType<QSharedPointer<Preset>>("QSharedPointer<Preset>");
}

void
JobmanPrivate::init()
{
    platform::setDarkTheme();
    documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir applicationPath(QApplication::applicationDirPath());
    presets = platform::getApplicationPath() + "/Presets";
    // icc profile
    ICCTransform* transform = ICCTransform::instance();
    QDir resources(platform::getApplicationPath() + "/Resources");
    QString inputProfile = resources.filePath("sRGB2014.icc"); // built-in Qt input profile
    transform->setInputProfile(inputProfile);
    profile();
    // queue
    queue = Queue::instance();
    // ui
    ui.reset(new Ui_Jobman());
    ui->setupUi(window);
    // about
    about.reset(new About(window.data()));
    // monitor
    monitor.reset(new Monitor(window.data()));
    monitor->setModal(false);
    // options
    options.reset(new Options(window.data()));
    // preferences
    preferences.reset(new Preferences(window.data()));
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
    ui->filedropBar->installEventFilter(filedropfilter.data());
    // url filter
    preseturlfilter.reset(new Urlfilter);
    ui->presets->installEventFilter(preseturlfilter.data());
    // url filter
    savetourlfilter.reset(new Urlfilter);
    ui->saveTo->installEventFilter(savetourlfilter.data());
    // progress
    ui->fileprogress->hide();
    // connect
    connect(ui->togglePreset, &QPushButton::pressed, this, &JobmanPrivate::togglePreset);
    connect(ui->toggleFiledrop, &QPushButton::pressed, this, &JobmanPrivate::toggleFiledrop);
    connect(presetfilter.data(), &Clickfilter::pressed, ui->togglePreset, &QPushButton::click);
    connect(filedropfilter.data(), &Clickfilter::pressed, ui->toggleFiledrop, &QPushButton::click);
    connect(ui->addFiles, &QAction::triggered, this, &JobmanPrivate::addFiles);
    connect(ui->open, &QAction::triggered, this, &JobmanPrivate::openPreferences);
    connect(ui->clear, &QAction::triggered, this, &JobmanPrivate::clearPreferences);
    connect(ui->save, &QAction::triggered, this, &JobmanPrivate::savePreferences);
    connect(ui->import_, &QAction::triggered, this, &JobmanPrivate::importPreferences);
    connect(ui->export_, &QAction::triggered, this, &JobmanPrivate::exportPreferences);
    connect(ui->refreshPresets, &QPushButton::clicked, this, &JobmanPrivate::refreshPresets);
    connect(ui->openPreset, &QPushButton::clicked, this, &JobmanPrivate::openPreset);
    connect(ui->selectPresetsfrom, &QPushButton::clicked, this, &JobmanPrivate::selectPresetsfrom);
    connect(preseturlfilter.data(), &Urlfilter::urlRequested, this, &JobmanPrivate::presetsUrl);
    connect(ui->selectSaveto, &QPushButton::clicked, this, &JobmanPrivate::selectSaveto);
    connect(ui->showSaveto, &QPushButton::clicked, this, &JobmanPrivate::showSaveto);
    connect(savetourlfilter.data(), &Urlfilter::urlChanged, this, &JobmanPrivate::saveToUrl);
    connect(ui->copyOriginal, &QCheckBox::stateChanged, this, &JobmanPrivate::copyOriginalChanged);
    connect(ui->createFolders, &QCheckBox::stateChanged, this, &JobmanPrivate::createFolderChanged);
    connect(ui->overwrite, &QCheckBox::stateChanged, this, &JobmanPrivate::overwriteChanged);
    connect(ui->filedrop, &Filedrop::filesDropped, this, &JobmanPrivate::run, Qt::QueuedConnection);
    connect(ui->presets, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &JobmanPrivate::presetsChanged);
    connect(ui->threads, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &JobmanPrivate::threadsChanged);
    connect(ui->about, &QAction::triggered, this, &JobmanPrivate::openAbout);
    connect(ui->openPresetsFolder, &QAction::triggered, this, &JobmanPrivate::openPresetsFolder);
    connect(ui->openSaveToFolder, &QAction::triggered, this, &JobmanPrivate::openSaveToFolder);
    connect(ui->monitor, &QAction::triggered, this, &JobmanPrivate::openMonitor);
    connect(ui->openMonitor, &QPushButton::clicked, this, &JobmanPrivate::openMonitor);
    connect(ui->options, &QAction::triggered, this, &JobmanPrivate::openOptions);
    connect(ui->openOptions, &QPushButton::clicked, this, &JobmanPrivate::openOptions);
    connect(ui->openGithubReadme, &QAction::triggered, this, &JobmanPrivate::openGithubReadme);
    connect(ui->openGithubIssues, &QAction::triggered, this, &JobmanPrivate::openGithubIssues);
    connect(queue.data(), &Queue::jobProcessed, this, &JobmanPrivate::jobProcessed);
    size = window->size();
    // threads
    int threads = QThread::idealThreadCount();
    for (int i = 1; i <= threads; ++i) {
        ui->threads->addItem(QString::number(i), i);
    }
    // cpu
    // todo: get a better function for windows!
    QTimer* timer = new QTimer(window.data());
    QObject::connect(timer, &QTimer::timeout, [&]() {
        if (ui->fileprogress->maximum()) {
            ui->cpu->setText(QString("CPU: %1%").arg(platform::getCpuUsage(), 0, 'f', 0));
        } else {
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
            QAction* action = new QAction("Reload stylesheet...", this);
            action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
            menu->addAction(action);
            connect(action, &QAction::triggered, [&]() {
                this->stylesheet();
            });
        }
    #endif
    // presets
    QTimer::singleShot(0, [this]() {
        this->loadPresets();
        this->verifySettings();
    });
}

void
JobmanPrivate::stylesheet()
{ 
    QFile stylesheet(platform::getApplicationPath() + "/Resources/App.css");
    stylesheet.open(QFile::ReadOnly);
    QString qss = stylesheet.readAll();
    QRegularExpression hslRegex("hsl\\(\\s*(\\d+)\\s*,\\s*(\\d+)%\\s*,\\s*(\\d+)%\\s*\\)");
    QString transformqss = qss;
    QRegularExpressionMatchIterator i = hslRegex.globalMatch(transformqss);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        if (match.hasMatch()) {
            if (!match.captured(1).isEmpty() &&
                !match.captured(2).isEmpty() &&
                !match.captured(3).isEmpty())
            {
                int h = match.captured(1).toInt();
                int s = match.captured(2).toInt();
                int l = match.captured(3).toInt();
                QColor color = QColor::fromHslF(h / 360.0f, s / 100.0f, l / 100.0f);
                // icc profile
                ICCTransform* transform = ICCTransform::instance();
                color = transform->map(color.rgb());
                QString hsl = QString("hsl(%1, %2%, %3%)")
                                .arg(color.hue() == -1 ? 0 : color.hue())
                                .arg(static_cast<int>(color.hslSaturationF() * 100))
                                .arg(static_cast<int>(color.lightnessF() * 100));
                
                transformqss.replace(match.captured(0), hsl);
            }
        }
    }
    qApp->setStyleSheet(transformqss);
}

void
JobmanPrivate::profile()
{
    QString outputProfile = platform::getIccProfileUrl(window->winId());
    // icc profile
    ICCTransform* transform = ICCTransform::instance();
    transform->setOutputProfile(outputProfile);
}

void
JobmanPrivate::activate()
{
    enable(true);
}

void
JobmanPrivate::deactivate()
{
    enable(false);
}

void
JobmanPrivate::addFiles()
{
    QStringList filenames = QFileDialog::getOpenFileNames(
                                window.data(),
                                tr("Select files"),
                                filesfrom,
                                tr("All files (*)")
    );
    if (!filenames.isEmpty()) {
        QFileInfo fileInfo(filenames.first());
        filesfrom = fileInfo.absolutePath();
        run(filenames);
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
    if (Question::askQuestion(window.data(),
        "All values including search paths, environent variables and options will be reset to their default settings.\n"
        "Do you want to continue?"
    )) {
        QSettings settings(APP_IDENTIFIER, APP_NAME);
        settings.clear();
        // update
        options.reset(new Options(window.data()));
        preferences.reset(new Preferences(window.data()));
        loadSettings();
        loadPresets();
        saveSettings();
        verifySettings();
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
    QString filename = QFileDialog::getOpenFileName(
            window.data(),
            tr("Import preferences file ..."),
            preferencesfrom,
            tr("JSON Files (*.json)")
    );
    if (!filename.isEmpty()) {
        QFile file(filename);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray jsonData = file.readAll();
            file.close();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
            if (!jsonDoc.isObject()) {
                Message::showMessage(window.data(),
                    "Invalid preferences file",
                    "The selected file is not a valid json preferences file."
                );
                return;
            }
            QJsonObject jsonobj = jsonDoc.object();
            QSettings settings(APP_IDENTIFIER, APP_NAME);
            for (auto it = jsonobj.constBegin(); it != jsonobj.constEnd(); ++it) {
                const QString &key = it.key();
                const QJsonValue &value = it.value();
                if (value.isString()) {
                    settings.setValue(key, value.toString());
                } else if (value.isDouble()) {
                    settings.setValue(key, value.toDouble());
                } else if (value.isBool()) {
                    settings.setValue(key, value.toBool());
                } else if (value.isArray()) {
                    QJsonArray jsonArray = value.toArray();
                    QVariantList variantList;
                    for (const QJsonValue &arrayValue : jsonArray) {
                        variantList.append(arrayValue.toVariant());
                    }
                    settings.setValue(key, variantList);
                }
            }
            // update
            options.reset(new Options(window.data()));
            preferences.reset(new Preferences(window.data()));
            loadSettings();
            loadPresets();
            
            QFileInfo fileInfo(file.fileName());
            preferencesfrom = fileInfo.absolutePath();
            saveSettings();
        } else {
            Message::showMessage(window.data(),
                "Could not open file for reading",
                QString("Error message: %1").arg(file.errorString())
            );
        }
    }
}

void
JobmanPrivate::exportPreferences()
{
    QString filename = QFileDialog::getSaveFileName(
        window.data(),
        tr("Export preferences file ..."),
        preferencesfrom,
        tr("JSON Files (*.json);")
    );
    if (!filename.isEmpty()) {
        if (!filename.toLower().endsWith(".json")) {
            filename += ".json";
        }
        QSettings settings(APP_IDENTIFIER, APP_NAME);
        QJsonObject jsonobj;
        QStringList internalprefix = {
            "Apple", "com/apple", "Nav", "AK", "PK", "NS", "Country", "MultipleSessionEnabled" // safe to ignore
        };
        for (const QString &key : settings.allKeys()) {
            bool isinternal = false;
            for (const QString &prefix : internalprefix) {
                if (key.startsWith(prefix)) {
                    isinternal = true;
                    break;
                }
            }
            if (!isinternal) {
                QVariant value = settings.value(key);
                switch (value.typeId()) {
                    case QMetaType::QString:
                        jsonobj[key] = value.toString();
                        break;
                    case QMetaType::Int:
                        jsonobj[key] = value.toInt();
                        break;
                    case QMetaType::Double:
                        jsonobj[key] = value.toDouble();
                        break;
                    case QMetaType::Bool:
                        jsonobj[key] = value.toBool();
                        break;
                    case QMetaType::LongLong:
                        jsonobj[key] = value.toLongLong();
                        break;
                    case QMetaType::QStringList:
                        jsonobj[key] = QJsonArray::fromStringList(value.toStringList());
                        break;
                    case QMetaType::QVariantList:
                    {
                        QJsonArray jsonarray;
                        for (const QVariant &item : value.toList()) {
                            jsonarray.append(QJsonValue::fromVariant(item));
                        }
                        jsonobj[key] = jsonarray;
                    }
                    break;
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
            Message::showMessage(window.data(),
                "Could open file for writing",
                QString("Error message: %1").arg(file.errorString())
            );
        }
    }
}

void
JobmanPrivate::enable(bool enable)
{
    ui->filedrop->setEnabled(enable);
    ui->fileprogress->setEnabled(enable);
    ui->options->setEnabled(enable);
}

bool
JobmanPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::ScreenChangeInternal) {
        profile();
        stylesheet();
    }
    if (event->type() == QEvent::Close) {
        if (queue->isProcessing()) {
            if (Question::askQuestion(window.data(), "Jobs are in progress, are you sure you want to quit?")) {
                saveSettings();
                return true;
            } else {
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
JobmanPrivate::verifySettings()
{
    if (saveto.isEmpty()) {
        if (Question::askQuestion(window.data(),
             "No save to folder selected for output files.\n"
             "Would you like to choose one now?"
        )) {
            QString dir = QFileDialog::getExistingDirectory(
                            window.data(),
                            tr("No save to folder selected for output files ..."),
                            QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
                            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
            );
            if (!dir.isEmpty()) {
                saveto = dir;
                setSaveto(saveto);
                activate();
                return;
            }
        }
        deactivate();
    }
}
    
void
JobmanPrivate::loadSettings()
{
    QDir applicationPath(QApplication::applicationDirPath());
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
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
    // ui
    setSaveto(saveto);
    ui->copyOriginal->setChecked(copyoriginal);
    ui->createFolders->setChecked(createfolders);
    ui->overwrite->setChecked(overwrite);
}

void
JobmanPrivate::saveSettings()
{
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    settings.setValue("filesfrom", filesfrom);
    settings.setValue("preferencesfrom", preferencesfrom);
    // presets
    if (ui->presets->count()) {
        QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
        if (!preset.isNull()) { // skip "No presets found"
            settings.setValue("presetsselected", preset->filename());
        }
    }
    settings.setValue("presetsfrom", presetsfrom);
    {
        QString bookmark = platform::saveBookmark(presetsfrom);
        if (!bookmark.isEmpty()) {
            settings.setValue("presetsfrombookmark", bookmark);
        }
    }
    settings.setValue("saveto", saveto);
    {
        QString bookmark = platform::saveBookmark(saveto);
        if (!bookmark.isEmpty()) {
            settings.setValue("savetobookmark", bookmark);
        }
    }
    settings.setValue("copyoriginal", copyoriginal);
    settings.setValue("createfolders", createfolders);
    settings.setValue("overwrite", createfolders);
    for (int i = 0; i < ui->presets->count(); ++i) {
        if (ui->presets->itemText(i) != "No presets found") {
            QVariant data = ui->presets->itemData(i);
            QSharedPointer<Preset> preset = data.value<QSharedPointer<Preset>>();
            settings.beginGroup(QString("preset/%1").arg(preset->id()));
            for(Option* option : preset->options()) {
                settings.setValue(QString("option/%1").arg(option->id), option->value);
            }
            settings.endGroup();
        }
    }
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
        for(QFileInfo presetfile : presetfiles) {
            QSharedPointer<Preset> preset(new Preset());
            QString filename = presetfile.absoluteFilePath();
            if (preset->read(filename)) {
                settings.beginGroup(QString("preset/%1").arg(preset->id()));
                for (Option* option : preset->options()) {
                    QString optionKey = QString("option/%1").arg(option->id);
                    if (settings.contains(optionKey)) {
                        option->value = settings.value(optionKey); // Restore value from settings
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
                                 "Error:\n"
                                 "%2\n")
                                 .arg(presetfile.absoluteFilePath())
                                 .arg(preset->error());
            }
        }
        if (error.length() > 0) {
            Message::showMessage(window.data(), "Could not load all presets", error);
        }
        if (ui->presets->count()) {
            activate();
        }
        else {
            ui->presets->addItem("No presets found");
            deactivate();
        }
    }
    else {
        ui->presets->addItem("No presets found");
        deactivate();
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

QString
JobmanPrivate::replacePaths(const QString& input, const QString& pattern, const QFileInfo& fileinfo)
{
    QString result = input;
    QList<QPair<QString, QString>> replacements = {
        {QString("%%1dir%").arg(pattern), fileinfo.absolutePath()},
        {QString("%%1file%").arg(pattern), fileinfo.absoluteFilePath()},
        {QString("%%1ext%").arg(pattern), fileinfo.suffix()},
        {QString("%%1base%").arg(pattern), fileinfo.baseName()}
    };
    for (const auto& replacement : replacements) {
        result.replace(replacement.first, replacement.second);
    }
    return result;
}

QString
JobmanPrivate::replaceFiles(const QString& input, const QFileInfo& inputinfo, const QFileInfo& outputinfo)
{
    return replacePaths(replacePaths(input, "input", inputinfo), "output", outputinfo);
}

QString
JobmanPrivate::replaceTask(const QString& input, const QString& inputinfo, const QString& outputinfo)
{
    QString result = inputinfo;
    result.replace(QString("%task:%1%").arg(input), outputinfo);
    return result;
}

QStringList
JobmanPrivate::replaceOptions(QList<Option*> options, const QString& input)
{
    QStringList result;
    for (Option* option : options) {
        QString pattern = QString("%options:%1%").arg(option->id);
        if (input.contains(pattern)) {
            QString replacement = option->flag;
            if (!option->switchvalue.toBool()) {
                if (replacement.length()) {
                    replacement += " ";
                }
                replacement += option->value.toString();
                result.append(QString(input).replace(pattern, replacement).split(" "));
            }
            else {
                result.append(replacement);
            }
        }
    }
    if (!result.count()) {
        result.append(input);
    }
    return result;
}

void
JobmanPrivate::run(const QList<QString>& files)
{
    QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
    QString outputDir = saveto;
    processedfiles.clear();
    int count = 0;
    for(const QString& file : files) {
        QMap<QString, QUuid> jobuuids;
        QMap<QString, QString> joboutputs;
        QList<QPair<QSharedPointer<Job>, QString>> dependentjobs;
        QFileInfo inputinfo(file);
        bool first = true;
        for(Task* task : preset->tasks()) {
            QString extension = replacePaths(task->extension, "input", inputinfo);
            QString outputdir;
            if (createfolders) {
                outputdir =
                    outputDir +
                    "/" +
                    inputinfo.fileName();
            } else {
                outputdir = outputDir;
            }
            QString outputfile =
                outputdir +
                "/" +
                inputinfo.baseName() +
                "." +
                extension;
            QFileInfo outputinfo(outputfile);
            QString command = replaceOptions(preset->options(), replaceFiles(task->command, inputinfo, outputinfo)).join(" ");
            QString output = replaceOptions(preset->options(), replaceFiles(task->output, inputinfo, outputinfo)).join(" ");
            QStringList argumentlist = task->arguments.split(" ");
            QStringList replacedlist;
            for(QString& argument : argumentlist) {
                replacedlist.append(replaceOptions(preset->options(), replaceTask("output", replaceFiles(argument, inputinfo, outputinfo), output)));
            }
            QString startin = replaceOptions(preset->options(), replaceFiles(task->startin, inputinfo, outputinfo)).join(" ");
            // job
            QSharedPointer<Job> job(new Job());
            {
                job->setId(task->id);
                job->setFilename(inputinfo.fileName());
                job->setDir(outputdir);
                job->setName(task->name);
                job->setCommand(command);
                job->setArguments(replacedlist);
                job->setOutput(output);
                job->setOverwrite(overwrite);
                job->setStartin(startin);
                job->setStatus(Job::Waiting);
            }
            QSettings settings(APP_IDENTIFIER, APP_NAME);
            job->os()->searchpaths = settings.value("searchpaths", documents).toStringList();
            QVariantList environmentvars = settings.value("environmentvars").toList();
            for (const QVariant& environmentvar : environmentvars) {
                QVariantMap environmentvarmap = environmentvar.toMap();
                if (environmentvarmap["checked"].toBool()) {
                    job->os()->environmentvars.append(
                        qMakePair(QString(environmentvarmap["name"].toString()), QString(environmentvarmap["value"].toString()))
                    );
                }
            }
            if (first) {
                if (copyoriginal) {
                    job->preprocess()->copyoriginal.filename = file;
                }
                first = false;
            }
            if (task->dependson.isEmpty()) {
                QUuid uuid = queue->submit(job);
                count++;
                if (!processedfiles.contains(file)) {
                    processedfiles.insert(file, QList<QUuid>());
                }
                processedfiles[file].append(uuid);
                jobuuids[task->id] = uuid;
                joboutputs[task->id] = job->output();
            } else {
                dependentjobs.append(qMakePair(job, task->dependson));
            }
    
        }
        for (QPair<QSharedPointer<Job>, QString> depedentjob : dependentjobs) {
            QSharedPointer<Job> job = depedentjob.first;
            QString dependentid = depedentjob.second;
            if (jobuuids.contains(dependentid)) {
                QStringList argumentlist = job->arguments();
                for(QString& argument : argumentlist) {
                    argument = replaceTask("input", argument, joboutputs[dependentid]);
                }
                job->setArguments(argumentlist);
                job->setDependson(jobuuids[dependentid]);
                QUuid uuid = queue->submit(job);
                if (!processedfiles.contains(file)) {
                    processedfiles.insert(file, QList<QUuid>());
                }
                processedfiles[file].append(uuid);
                jobuuids[job->id()] = uuid;
                count++;
            } else {
                QString status = QString("Status:\n"
                                         "Dependency not found for job: %1\n")
                                         .arg(job->name());
                job->setLog(status);
                job->setStatus(Job::Failed);
                return;
            }
        }
    }
    ui->fileprogress->setMaximum(ui->fileprogress->maximum() + count);
    if (!ui->fileprogress->isVisible()) {
        ui->fileprogress->show();
        ui->idleprogress->hide();
    }
}

void
JobmanPrivate::jobProcessed(const QUuid& uuid)
{
    bool found = false;
    QStringList files = processedfiles.keys();
    for (const QString& file : files) {
        if (processedfiles[file].contains(uuid)) {
            processedfiles[file].removeAll(uuid);
            if (processedfiles[file].isEmpty()) {
                processedfiles.remove(file);
            }
            found = true;
        }
    }
    if (found) { // test if dropped
        int value = ui->fileprogress->value() + 1;
        if (value == ui->fileprogress->maximum()) {
            ui->fileprogress->setValue(0);
            ui->fileprogress->setMaximum(0);
            ui->fileprogress->hide();
            ui->idleprogress->show();
        } else {
            ui->fileprogress->setValue(value);
            ui->fileprogress->setToolTip(QString("Completed %1 of %2").arg(ui->fileprogress->value()).arg(ui->fileprogress->maximum()));
        }
    }
}

void
JobmanPrivate::refreshPresets()
{
    loadPresets();
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
    QString dir = QFileDialog::getExistingDirectory(
                    window.data(),
                    tr("Select preset folder ..."),
                    presetsfrom == presets ? documents : presetsfrom, // open will always open from Documents
                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (!dir.isEmpty()) {
        if (dir != presetsfrom) { // skip if the same directory
            QDir selectdir(dir);
            selectdir.setNameFilters(QStringList() << "*.json");
            if (selectdir.entryList(QDir::Files).isEmpty()) {
                if (Question::askQuestion(window.data(),
                    "The directory has no presets.\n"
                    "Copy the built-in preset as a template?"
                )) {
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
                                    Message::showMessage(window.data(),
                                        "Could open file for writing",
                                        QString("Error message: %1").arg(file.errorString())
                                    );
                                    return;
                                }
                            }
                            else {
                                Message::showMessage(window.data(),
                                    "Could open file for reading",
                                    QString("Error message: %1").arg(file.errorString())
                                );
                                return;
                            }
                        }
                        else {
                            Message::showMessage(window.data(),
                                "Could not copy presets",
                                QString("Failed when trying to copy presets to selected directory: %1").arg(dir)
                            );
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
JobmanPrivate::presetsUrl(const QUrl& url)
{
    if (presetsfrom == presets) {
        return; // internal presets
    }
    QString path = url.toLocalFile();
    QFileInfo fileinfo(path);
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
        if (!alreadyexistsfiles.isEmpty()) {
            message += "Invalid preset files:\n" + invalidfiles.join("\n");
        }
        Message::showMessage(window.data(), "Preset import summary", message);
    }
    else if (fileinfo.isFile() && fileinfo.suffix().toLower() == "json") {
        QString filename = fileinfo.fileName();
        QString path = fileinfo.filePath();
        QString target = presetsdir.filePath(fileinfo.fileName());
        if (QFile::exists(target)) {
            Message::showMessage(window.data(),
                                 "File ignored",
                                 QString("The file '%1' was ignored because it already exists.")
                                 .arg(filename));
        }
        else {
            QSharedPointer<Preset> preset(new Preset());
            if (preset->read(path)) {
                QFile::copy(path, target);
                Message::showMessage(window.data(),
                                     "File added",
                                     QString("The file '%1' was added successfully.")
                                     .arg(filename));
                
            }
            else {
                Message::showMessage(window.data(),
                                     "Invalid preset file",
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

void
JobmanPrivate::selectSaveto()
{
    QString dir = QFileDialog::getExistingDirectory(
                    window.data(),
                    tr("Select save to folder ..."),
                    saveto,
                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (!dir.isEmpty()) {
        saveto = dir;
        setSaveto(saveto);
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
    saveto = url.toLocalFile();
    ui->showSaveto->setVisible(true);
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
        ui->openOptions->setEnabled(enabled);
        ui->options->setEnabled(enabled);
    }
}

void
JobmanPrivate::threadsChanged(int index)
{
    queue->setThreads(ui->threads->itemText(index).toInt());
}

void
JobmanPrivate::togglePreset()
{
    int height = ui->presetWidget->height(); 
    if (ui->togglePreset->isChecked()) {
        ui->togglePreset->setIcon(QIcon(":/icons/resources/Collapse.png"));
        ui->presetWidget->show();
        window->setFixedSize(window->width(), size.height() + height);
    } else {
        
        ui->togglePreset->setIcon(QIcon(":/icons/resources/Expand.png"));
        ui->presetWidget->hide();
        window->setFixedSize(window->width(), size.height() - height);
    }
    size = window->size();
}

void
JobmanPrivate::toggleFiledrop()
{
    int height = ui->filedropWidget->height();
    if (ui->toggleFiledrop->isChecked()) {
        ui->toggleFiledrop->setIcon(QIcon(":/icons/resources/Collapse.png"));
        ui->filedropWidget->show();
        window->setFixedSize(window->width(), size.height() + height);
    } else {
        
        ui->toggleFiledrop->setIcon(QIcon(":/icons/resources/Expand.png"));
        ui->filedropWidget->hide();
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
    } else {
        monitor->show();
    }
}

void
JobmanPrivate::openOptions()
{
    QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
    options->update(preset);
    options->exec();
}

void
JobmanPrivate::openGithubReadme()
{
    QDesktopServices::openUrl(QUrl("https://github.com/mikaelsundell/jobman/blob/master/README.md"));
}

void
JobmanPrivate::openGithubIssues()
{
    QDesktopServices::openUrl(QUrl("https://github.com/mikaelsundell/jobman/issues"));
}

#include "jobman.moc"

Jobman::Jobman()
: QMainWindow(nullptr,
  Qt::WindowTitleHint |
  Qt::CustomizeWindowHint |
  Qt::WindowCloseButtonHint |
  Qt::WindowMinimizeButtonHint)
, p(new JobmanPrivate())
{
    p->window = this;
    p->init();
}

Jobman::~Jobman()
{
}
