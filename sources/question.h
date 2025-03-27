// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#pragma once
#include <QDialog>

class QuestionPrivate;
class Question : public QDialog {
    Q_OBJECT
public:
    Question(QWidget* parent = nullptr);
    virtual ~Question();
    void setQuestion(const QString& question);

    static bool askQuestion(QWidget* parent, const QString& question);

private:
    QScopedPointer<QuestionPrivate> p;
};
