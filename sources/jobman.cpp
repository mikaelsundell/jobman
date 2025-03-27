// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "jobman.h"
#include "clickfilter.h"
#include "icctransform.h"
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
#include "urlfilter.h"

#include <QAction>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QWindow>

// generated files
#include "ui_about.h"
#include "ui_jobman.h"

class JobmanPrivate : public QObject {
    Q_OBJECT
public:
    enum Type { File, Command };

public:
    JobmanPrivate();
    void init();
    void stylesheet();
    void profile();
    void activate();
    void enable(bool enable);
    bool eventFilter(QObject* object, QEvent* event);
    void verifySaveTo();
    void loadSettings();
    void saveSettings();

public Q_SLOTS:
    void loadPresets();
    void clearPresets();
    void togglePreset();
    void toggleType();
    void openPresetsFolder();
    void openSaveToFolder();
    void openMonitor();
    void openOptions();
    void processFiles(const QList<QString>& files);
    void processCommand();
    void processUuids(const QList<QUuid>& uuids);
    void jobProcessed(const QUuid& uuid);
    void submitFiles();
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
    class About : public QDialog {
    public:
        About(QWidget* parent = nullptr)
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
    Paths paths();
    void process(const QList<QUuid> uuids);
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
    QSize size;
    int offset;
    QList<QUuid> submitteduuids;
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
    QScopedPointer<Ui_Jobman> ui;
};

JobmanPrivate::JobmanPrivate()
    : offset(0)
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
    QString inputProfile = resources.filePath("sRGB2014.icc");  // built-in Qt input profile
    transform->setInputProfile(inputProfile);
    profile();
    // queue
    queue = Queue::instance();
    // ui
    ui.reset(new Ui_Jobman());
    ui->setupUi(window);
    // preferences
    ui->open->setMenuRole(QAction::PreferencesRole);
    // about
    about.reset(new About(window.data()));
    // monitor
    monitor.reset(new Monitor(window.data()));
    monitor->setModal(false);
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
    connect(ui->submitFiles, &QAction::triggered, this, &JobmanPrivate::submitFiles);
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
    connect(ui->filedrop, &Filedrop::filesDropped, this, &JobmanPrivate::processFiles, Qt::QueuedConnection);
    connect(ui->presets, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &JobmanPrivate::presetsChanged);
    connect(ui->threads, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &JobmanPrivate::threadsChanged);
    connect(ui->submit, &QPushButton::pressed, this, &JobmanPrivate::processCommand);
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
        QAction* action = new QAction("Reload stylesheet...", this);
        action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
        menu->addAction(action);
        connect(action, &QAction::triggered, [&]() { this->stylesheet(); });
    }
#endif
    // presets
    QTimer::singleShot(0, [this]() {
        this->loadPresets();
        this->verifySaveTo();
        this->activate();
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
            if (!match.captured(1).isEmpty() && !match.captured(2).isEmpty() && !match.captured(3).isEmpty()) {
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
    window->setStyleSheet(transformqss);
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
    QStringList filenames = QFileDialog::getOpenFileNames(window.data(), tr("Select files"), filesfrom,
                                                          tr("All files (*)"));
    if (!filenames.isEmpty()) {
        QFileInfo fileInfo(filenames.first());
        filesfrom = fileInfo.absolutePath();
        processFiles(filenames);
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
                                                    tr("JSON Files (*.json);"));
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
    ui->openPreset->setEnabled(enable);
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
    QDir applicationPath(QApplication::applicationDirPath());
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
        if (!preset.isNull()) {  // skip "No presets found"
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
            for (QSharedPointer<Option> option : preset->options()) {
                settings.setValue(QString("option/%1/value").arg(option->id), option->value);
                settings.setValue(QString("option/%1/enabled").arg(option->id), option->enabled);
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
        for (QFileInfo presetfile : presetfiles) {
            QSharedPointer<Preset> preset(new Preset());
            QString filename = presetfile.absoluteFilePath();
            if (preset->read(filename)) {
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
    if (!ui->presets->count()) {
        ui->presets->addItem("No presets found");
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
    QSharedPointer<Preset> preset = ui->presets->currentData().value<QSharedPointer<Preset>>();
    processUuids(processor->submit(files, preset, paths()));
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
    for (const QUuid& uuid : uuids) {
        submitteduuids.append(uuid);
    }
    ui->fileprogress->setMaximum(ui->fileprogress->maximum() + static_cast<int>(uuids.count()));
    if (!ui->progressWidget->currentIndex()) {
        ui->progressWidget->setCurrentIndex(1);
    }
}

void
JobmanPrivate::jobProcessed(const QUuid& uuid)
{
    if (submitteduuids.contains(uuid)) {
        int value = ui->fileprogress->value() + 1;
        if (value == ui->fileprogress->maximum()) {
            ui->fileprogress->setValue(0);
            ui->fileprogress->setMaximum(0);
            ui->progressWidget->setCurrentIndex(0);
        }
        else {
            ui->fileprogress->setValue(value);
            ui->fileprogress->setToolTip(
                QString("Completed %1 of %2").arg(ui->fileprogress->value()).arg(ui->fileprogress->maximum()));
        }
        submitteduuids.removeAll(uuid);
    }
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
JobmanPrivate::presetsUrl(const QUrl& url)
{
    if (presetsfrom == presets) {
        return;  // internal presets
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
            Message::showMessage(window.data(), "File ignored",
                                 QString("The file '%1' was ignored because it already exists.").arg(filename));
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
        if (preset->type() == "file") {
            ui->presettype->setCurrentIndex(0);
            ui->type->setText("Filedrop");
            ui->openOptions->setVisible(true);

            window->setFixedSize(window->width(), size.height() - offset);
            offset = 0;
            size = window->size();
        }
        else {
            ui->presettype->setCurrentIndex(1);
            ui->type->setText("Command");
            ui->optionsWidget->update(preset);
            ui->openOptions->setVisible(false);

            if (!offset) {
                offset = 200;
                window->setFixedSize(window->width(), size.height() + offset);
                size = window->size();
                enabled = false;
            }
        }
        ui->submitFiles->setEnabled(enabled);
        ui->options->setEnabled(enabled);
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
    }
    else {
        ui->togglePreset->setIcon(QIcon(":/icons/resources/Expand.png"));
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
        ui->toggleType->setIcon(QIcon(":/icons/resources/Collapse.png"));
        ui->typeWidget->show();
        window->setFixedSize(window->width(), size.height() + height);
    }
    else {
        ui->toggleType->setIcon(QIcon(":/icons/resources/Expand.png"));
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
    optionsdialog->exec();
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
    : QMainWindow(nullptr, Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint
                               | Qt::WindowMinimizeButtonHint)
    , p(new JobmanPrivate())
{
    p->window = this;
    p->init();
}

Jobman::~Jobman() {}
