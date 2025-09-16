// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "optionsdialog.h"
#include "message.h"
#include "optionswidget.h"
#include "question.h"
#include "urlfilter.h"

#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>

// generated files
#include "ui_optionsdialog.h"

class OptionsDialogPrivate : public QObject {
    Q_OBJECT
public:
    OptionsDialogPrivate();
    void init();
    void update();
    bool eventFilter(QObject* obj, QEvent* event);
    void loadState();
    void loadStateFile(const QString& filename);
    void saveState();
    void loadSettings();
    void saveSettings();

public Q_SLOTS:
    void close();
    void defaults();
    void loadStateUrl(const QUrl& url);


public:
    QString documents;
    QString statesfrom;
    QScopedPointer<Urlfilter> urlfilter;
    QSharedPointer<Preset> preset;
    QPointer<OptionsDialog> dialog;
    QPointer<OptionsWidget> widget;
    QScopedPointer<Ui_OptionsDialog> ui;
};

OptionsDialogPrivate::OptionsDialogPrivate() {}

void
OptionsDialogPrivate::init()
{
    preset.reset(new Preset());
    // ui
    ui.reset(new Ui_OptionsDialog());
    ui->setupUi(dialog);
    // settings
    loadSettings();
    // event filter
    dialog->installEventFilter(this);
    // url filter
    urlfilter.reset(new Urlfilter);
    dialog->installEventFilter(urlfilter.data());
    // connect
    connect(ui->load, &QPushButton::pressed, this, &OptionsDialogPrivate::loadState);
    connect(ui->save, &QPushButton::pressed, this, &OptionsDialogPrivate::saveState);
    connect(ui->close, &QPushButton::pressed, this, &OptionsDialogPrivate::close);
    connect(ui->defaults, &QPushButton::pressed, this, &OptionsDialogPrivate::defaults);
    connect(urlfilter.data(), &Urlfilter::urlRequested, this, &OptionsDialogPrivate::loadStateUrl);
}

void
OptionsDialogPrivate::update()
{
    ui->name->setText(preset->name());
    QString objectname = ui->optionsWidget->objectName();
    {
        delete ui->optionsWidget;  // needed for dynamic layout
        ui->optionsWidget = nullptr;
    }
    ui->optionsWidget = new OptionsWidget();
    ui->optionsWidget->setObjectName(objectname);
    ui->optionsWidget->update(preset);
    ui->scrollarea->setWidget(ui->optionsWidget);
    ui->scrollarea->setWidgetResizable(true);
}

bool
OptionsDialogPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::Hide) {
        saveSettings();
    }
    return QObject::eventFilter(object, event);
}

void
OptionsDialogPrivate::loadStateUrl(const QUrl& url)
{
    QString path = url.toLocalFile();
    QFileInfo fileinfo(path);
    if (fileinfo.isFile() && fileinfo.suffix().toLower() == "json") {
        loadStateFile(fileinfo.filePath());
    }
}

void
OptionsDialogPrivate::loadState()
{
    QString filename = QFileDialog::getOpenFileName(widget.data(), tr("Load state ..."),
                                                    statesfrom.size() ? statesfrom : documents,
                                                    tr("JSON Files (*.json)"));
    if (!filename.isEmpty()) {
        loadStateFile(filename);
    }
}

void
OptionsDialogPrivate::loadStateFile(const QString& filename)
{
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray jsonData = file.readAll();
        file.close();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
        if (!jsonDoc.isObject()) {
            Message::showMessage(widget.data(), "Invalid state file",
                                 "The selected file is not a valid json state file.");
            return;
        }
        QJsonObject jsonobj = jsonDoc.object();
        for (auto it = jsonobj.constBegin(); it != jsonobj.constEnd(); ++it) {
            const QString& key = it.key();
            QStringList keyparts = key.split('/');
            if (keyparts.size() == 3 && keyparts[0] == "option") {
                const QString& id = keyparts[1];
                const QString& field = keyparts[2];
                if (preset->hasOption(id) && field == "value") {
                    const QJsonValue& value = it.value();
                    qDebug() << id << " == " << value.toBool();
                    if (value.isString()) {
                        preset->option(id)->value = value.toString();
                    }
                    else if (value.isDouble()) {
                        preset->option(id)->value = value.toDouble();
                    }
                    else if (value.isBool()) {
                        preset->option(id)->value = value.toBool();
                    }
                }
                if (preset->hasOption(id) && field == "enabled") {
                    const QJsonValue& value = it.value();
                    if (value.isBool()) {
                        qDebug() << id << " == " << value.toBool();
                        preset->option(id)->enabled = value.toBool();
                    }
                }
            }
        }
    }
    update();
}

void
OptionsDialogPrivate::saveState()
{
    QString filename = QFileDialog::getSaveFileName(widget.data(), tr("Save state ..."),
                                                    statesfrom.size() ? statesfrom : documents,
                                                    tr("JSON Files (*.json)"));
    if (!filename.isEmpty()) {
        if (!filename.toLower().endsWith(".json")) {
            filename += ".json";
        }
        QJsonObject jsonobj;
        for (QSharedPointer<Option> option : preset->options()) {
            QString key = QString("option/%1/value").arg(option->id);
            QVariant value = option->value;
            switch (value.typeId()) {
            case QMetaType::QString: jsonobj[key] = value.toString(); break;
            case QMetaType::Int: jsonobj[key] = value.toInt(); break;
            case QMetaType::Double: jsonobj[key] = value.toDouble(); break;
            case QMetaType::Bool: jsonobj[key] = value.toBool(); break;
            case QMetaType::LongLong: jsonobj[key] = value.toLongLong(); break;
            }
            jsonobj[QString("option/%1/enabled").arg(option->id)] = option->enabled;
        }
        QJsonDocument jsonDoc(jsonobj);
        QFile file(filename);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(jsonDoc.toJson(QJsonDocument::Indented));
            file.close();

            QFileInfo fileInfo(file.fileName());
            statesfrom = fileInfo.absolutePath();
        }
        else {
            Message::showMessage(widget.data(), "Could open file for writing",
                                 QString("Error message: %1").arg(file.errorString()));
        }
    }
}

void
OptionsDialogPrivate::loadSettings()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    statesfrom = settings.value("statesfrom", documents).toString();
}

void
OptionsDialogPrivate::saveSettings()
{
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    settings.setValue("statesfrom", statesfrom);
}

void
OptionsDialogPrivate::close()
{
    dialog->close();
}

void
OptionsDialogPrivate::defaults()
{
    if (Question::askQuestion(dialog.data(), "All values will be reset to their default settings.\n"
                                             "Do you want to continue?")) {
        for (QSharedPointer<Option> option : preset->options()) {
            option->value = option->defaultvalue;
            if (option->toggle.isEmpty()) {
                option->enabled = true;
            }
            else {
                option->enabled = false;
            }
        }
        update();
    }
}

#include "optionsdialog.moc"

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent)
    , p(new OptionsDialogPrivate())
{
    p->dialog = this;
    p->init();
}

OptionsDialog::~OptionsDialog() {}

QSharedPointer<Preset>
OptionsDialog::preset() const
{
    return p->preset;
}

void
OptionsDialog::update(QSharedPointer<Preset> preset)
{
    if (p->preset->uuid() != preset->uuid()) {
        p->preset = preset;
        p->update();
    }
}
