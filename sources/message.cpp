// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "message.h"

#include <QPointer>

// generated files
#include "ui_message.h"

class MessagePrivate : public QObject {
    Q_OBJECT
public:
    MessagePrivate();
    void init();

public:
    QPointer<Message> dialog;
    QScopedPointer<Ui_Message> ui;
};

MessagePrivate::MessagePrivate() {}

void
MessagePrivate::init()
{
    // ui
    ui.reset(new Ui_Message());
    ui->setupUi(dialog);
    // connect
    connect(ui->close, &QPushButton::clicked, this, [this]() { dialog->done(QDialog::Accepted); });
}

#include "message.moc"

Message::Message(QWidget* parent)
    : QDialog(parent)
    , p(new MessagePrivate())
{
    p->dialog = this;
    p->init();
}

Message::~Message() {}

void
Message::setTitle(const QString& title)
{
    p->ui->title->setText(title);
}

void
Message::setMessage(const QString& message)
{
    p->ui->message->setPlainText(message);
}

bool
Message::showMessage(QWidget* parent, const QString& title, const QString& message)
{
    Message dialog(parent);
    dialog.setTitle(title);
    dialog.setMessage(message);
    dialog.adjustSize();
    const int result = dialog.exec();
    return result == QDialog::Accepted;
}
