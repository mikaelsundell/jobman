// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "question.h"

#include <QPointer>

// generated files
#include "ui_question.h"

class QuestionPrivate : public QObject {
    Q_OBJECT
public:
    QuestionPrivate();
    void init();

public:
    QPointer<Question> dialog;
    QScopedPointer<Ui_Question> ui;
};

QuestionPrivate::QuestionPrivate() {}

void
QuestionPrivate::init()
{
    // ui
    ui.reset(new Ui_Question());
    ui->setupUi(dialog);
    // connect
    connect(ui->yes, &QPushButton::clicked, this, [this]() { dialog->done(QDialog::Accepted); });
    connect(ui->no, &QPushButton::clicked, this, [this]() { dialog->done(QDialog::Rejected); });
}

#include "question.moc"

Question::Question(QWidget* parent)
    : QDialog(parent)
    , p(new QuestionPrivate())
{
    p->dialog = this;
    p->init();
}

Question::~Question() {}

void
Question::setQuestion(const QString& question)
{
    p->ui->question->setText(question);
}

bool
Question::askQuestion(QWidget* parent, const QString& question)
{
    Question dialog(parent);
    dialog.setQuestion(question);
    const int result = dialog.exec();
    return result == QDialog::Accepted;
}
