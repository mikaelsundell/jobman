// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "listwidget.h"
#include "urlfilter.h"

#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>

class PairItemPrivate : public QObject {
    Q_OBJECT
public:
    PairItemPrivate();
    void init();

public:
    void enabled(bool enabled)
    {
        nameedit->setEnabled(enabled);
        valueedit->setEnabled(enabled);
    }
    QString name;
    QString value;
    QCheckBox* checkbox;
    QLineEdit* nameedit;
    QLineEdit* valueedit;
    QScopedPointer<Urlfilter> urlfilter;
    QPointer<PairItem> item;
};

PairItemPrivate::PairItemPrivate() {}

void
PairItemPrivate::init()
{
    // ui
    checkbox = new QCheckBox(item.data());
    checkbox->setChecked(true);
    nameedit = new QLineEdit(name, item.data());
    valueedit = new QLineEdit(value, item.data());
    nameedit->setFixedWidth(100);
    QHBoxLayout* layout = new QHBoxLayout(item.data());
    layout->addWidget(checkbox);
    layout->addWidget(nameedit);
    layout->addWidget(valueedit);
    layout->setContentsMargins(0, 0, 0, 0);
    item->setLayout(layout);
    // url filter
    urlfilter.reset(new Urlfilter);
    valueedit->installEventFilter(urlfilter.data());
    // connect
    connect(checkbox, &QCheckBox::toggled, this, [this](bool checked) { this->enabled(checked); });
}

PairItem::PairItem(QWidget* parent)
    : QWidget(parent)
    , p(new PairItemPrivate())
{
    p->item = this;
    p->init();
}

PairItem::PairItem(const QString& name, const QString& value, QWidget* parent)
    : QWidget(parent)
    , p(new PairItemPrivate())
{
    p->name = name;
    p->value = value;
    p->item = this;
    p->init();
}

PairItem::~PairItem() {}

QString
PairItem::name() const
{
    return p->nameedit->text();
}

QString
PairItem::value() const
{
    return p->valueedit->text();
}

bool
PairItem::isChecked() const
{
    return p->checkbox->isChecked();
}

void
PairItem::setName(const QString& name)
{
    p->nameedit->setText(name);
}

void
PairItem::setValue(const QString& value)
{
    p->valueedit->setText(value);
}

void
PairItem::setChecked(bool checked)
{
    p->checkbox->setChecked(checked);
}

class ListWidgetPrivate : public QObject {
    Q_OBJECT
public:
    ListWidgetPrivate();
    void init();

public:
    QPointer<ListWidget> widget;
};

ListWidgetPrivate::ListWidgetPrivate() {}

void
ListWidgetPrivate::init()
{}

#include "listwidget.moc"

ListWidget::ListWidget(QWidget* parent)
    : QListWidget(parent)
    , p(new ListWidgetPrivate())
{
    p->widget = this;
    p->init();
}

ListWidget::~ListWidget() {}

void
ListWidget::mousePressEvent(QMouseEvent* event)
{
    QListWidget::mousePressEvent(event);
    if (itemAt(event->pos()) == nullptr) {
        clearSelection();
        emit itemSelectionChanged();
    }
}
