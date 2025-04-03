// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "optionsdialog.h"
#include "optionswidget.h"
#include "question.h"

#include <QPointer>
#include <QTimer>
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

public Q_SLOTS:
    void close();
    void defaults();

public:
    QSharedPointer<Preset> preset;
    QPointer<OptionsDialog> dialog;
    QPointer<OptionsWidget> widget;
    QScopedPointer<Ui_OptionsDialog> ui;
};

OptionsDialogPrivate::OptionsDialogPrivate() {}

void
OptionsDialogPrivate::init()
{
    // preset
    preset.reset(new Preset());
    // ui
    ui.reset(new Ui_OptionsDialog());
    ui->setupUi(dialog);
    // event filter
    dialog->installEventFilter(this);
    // connect
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
            option->enabled = false;
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
