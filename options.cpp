// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "options.h"

#include <QFileDialog>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>

// generated files
#include "ui_options.h"

class OptionsPrivate : public QObject
{
    Q_OBJECT
    public:
    OptionsPrivate();
        void init();
        bool eventFilter(QObject* object, QEvent* event);
        void loadSettings();
        void saveSettings();

    public Q_SLOTS:
        void selectionChanged();
        //void add();
        //void remove();
        void close();
        void save();

    public:
        QString searchpathfrom;
        QStringList searchpaths;
        QPointer<Options> dialog;
        QScopedPointer<Ui_Options> ui;
};

OptionsPrivate::OptionsPrivate()
{
}

void
OptionsPrivate::init()
{
    // ui
    ui.reset(new Ui_Options());
    ui->setupUi(dialog);
    // settings
    loadSettings();
    // event filter
    dialog->installEventFilter(this);
    // layout
    for(QString searchpath : searchpaths) {
        //ui->searchpaths->addItem(searchpath);
    }
    // connect
    //connect(ui->searchpaths, &QListWidget::itemSelectionChanged, this, &PreferencesPrivate::selectionChanged);
    //connect(ui->add, &QPushButton::pressed, this, &PreferencesPrivate::add);
    //connect(ui->remove, &QPushButton::pressed, this, &PreferencesPrivate::remove);
    //connect(ui->close, &QPushButton::pressed, this, &PreferencesPrivate::close);
}

bool
OptionsPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::Hide) {
        saveSettings();
    }
    return false;
}

void
OptionsPrivate::loadSettings()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QSettings settings(MACOSX_BUNDLE_GUI_IDENTIFIER, "Jobman");
    //searchpathfrom = settings.value("searchpathFrom", documents).toString();
    //searchpaths = settings.value("searchpaths", documents).toStringList();
}

void
OptionsPrivate::saveSettings()
{
    QSettings settings(MACOSX_BUNDLE_GUI_IDENTIFIER, "Jobman");
    //settings.setValue("searchpathFrom", searchpathfrom);
    //searchpaths.clear();
    //for (int i = 0; i < ui->searchpaths->count(); ++i) {
    //    QListWidgetItem* item = ui->searchpaths->item(i);
    //    if (item) {
    //        searchpaths.append(item->text());
    //    }
    //}
    //settings.setValue("searchpaths", searchpaths);
}

void
OptionsPrivate::selectionChanged()
{
    //QList<QListWidgetItem*> selectedItems = ui->searchpaths->selectedItems();
    //if (!selectedItems.isEmpty()) {
    //    ui->remove->setEnabled(true);
    //} else {
    //    ui->remove->setEnabled(false);
    //}
}

void
OptionsPrivate::close()
{
    dialog->close();
}

void
OptionsPrivate::save()
{
    dialog->close();
}


#include "options.moc"

Options::Options(QWidget* parent)
: QDialog(parent)
, p(new OptionsPrivate())
{
    p->dialog = this;
    p->init();
}

Options::~Options()
{
}
