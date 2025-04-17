// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "optionsdialog.h"
#include "message.h"
#include "optionswidget.h"
#include "question.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QStandardPaths>
#include <QUUid>

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
    void saveState();

public Q_SLOTS:
    void close();
    void defaults();

public:
    QString documents;
    QString statesfrom;
    QSharedPointer<Preset> preset;
    QPointer<OptionsDialog> dialog;
    QPointer<OptionsWidget> widget;
    QScopedPointer<Ui_OptionsDialog> ui;
};

OptionsDialogPrivate::OptionsDialogPrivate() {}

void
OptionsDialogPrivate::init()
{
    documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // preset
    preset.reset(new Preset());
    // ui
    ui.reset(new Ui_OptionsDialog());
    ui->setupUi(dialog);
    // event filter
    dialog->installEventFilter(this);
    // connect
    connect(ui->load, &QPushButton::pressed, this, &OptionsDialogPrivate::loadState);
    connect(ui->save, &QPushButton::pressed, this, &OptionsDialogPrivate::saveState);
    connect(ui->close, &QPushButton::pressed, this, &OptionsDialogPrivate::close);
    connect(ui->defaults, &QPushButton::pressed, this, &OptionsDialogPrivate::defaults);
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
OptionsDialogPrivate::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Show) {}
    return QObject::eventFilter(obj, event);
}

void
OptionsDialogPrivate::loadState()
{
    QString filename = QFileDialog::getOpenFileName(widget.data(), tr("Load state ..."), statesfrom,
                                                    tr("JSON Files (*.json)"));
    if (!filename.isEmpty()) {
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
}
   
void
OptionsDialogPrivate::saveState()
{
    QString filename = QFileDialog::getSaveFileName(widget.data(), tr("Save state ..."), statesfrom,
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

void
OptionsDialog::update(QSharedPointer<Preset> preset)
{
    if (p->preset->uuid() != preset->uuid()) {
        p->preset = preset;
        p->update();
    }
}
