// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "preferences.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QMouseEvent>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>

// generated files
#include "ui_preferences.h"

class PreferencesPrivate : public QObject
{
    Q_OBJECT
    public:
        PreferencesPrivate();
        void init();
        bool eventFilter(QObject* object, QEvent* event);
        void loadSettings();
        void saveSettings();

    public Q_SLOTS:
        void searchpathChanged();
        void environmentvarsChanged();
        void addSearchpath();
        void removeSearchpath();
        void addEnvironmentvar();
        void removeEnvironmentvar();
        void close();

    public:
        QString open(const QString& path);
        void removeSelection(QListWidget* widget);
        QString searchpathfrom;
        QStringList searchpaths;
        QVariantList environmentvars;
        QPointer<Preferences> dialog;
        QScopedPointer<Ui_Preferences> ui;
};

PreferencesPrivate::PreferencesPrivate()
{
}

void
PreferencesPrivate::init()
{
    // ui
    ui.reset(new Ui_Preferences());
    ui->setupUi(dialog);
    // settings
    loadSettings();
    // event filter
    dialog->installEventFilter(this);
    // layout
    for(QString searchpath : searchpaths) {
        ui->searchpaths->addItem(searchpath);
    }
    for (const QVariant& environmentvar : environmentvars) {
        QVariantMap pairmap = environmentvar.toMap();
        PairItem* pairitem = new PairItem(pairmap["name"].toString(), pairmap["value"].toString());
        pairitem->setChecked(pairmap["checked"].toBool());
        QListWidgetItem* item = new QListWidgetItem(ui->environmentvars);
        item->setSizeHint(pairitem->sizeHint());
        ui->environmentvars->addItem(item);
        ui->environmentvars->setItemWidget(item, pairitem);
    }
    // connect
    connect(ui->searchpaths, &QListWidget::itemSelectionChanged, this, &PreferencesPrivate::searchpathChanged);
    connect(ui->environmentvars, &QListWidget::itemSelectionChanged, this, &PreferencesPrivate::environmentvarsChanged);
    connect(ui->addSearchpath, &QPushButton::pressed, this, &PreferencesPrivate::addSearchpath);
    connect(ui->removeSearchpath, &QPushButton::pressed, this, &PreferencesPrivate::removeSearchpath);
    connect(ui->addEnvironmentvar, &QPushButton::pressed, this, &PreferencesPrivate::addEnvironmentvar);
    connect(ui->removeEnvironmentvar, &QPushButton::pressed, this, &PreferencesPrivate::removeEnvironmentvar);
    connect(ui->close, &QPushButton::pressed, this, &PreferencesPrivate::close);
}

bool
PreferencesPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::Hide) {
        saveSettings();
    }
    return QObject::eventFilter(object, event); // Pass other events to base class
}

void
PreferencesPrivate::loadSettings()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QSettings settings(MACOSX_BUNDLE_GUI_IDENTIFIER, MACOSX_BUNDLE_BUNDLE_NAME);
    searchpathfrom = settings.value("searchpathFrom", documents).toString();
    searchpaths = settings.value("searchpaths").toStringList();
    environmentvars = settings.value("environmentvars").toList();
}

void
PreferencesPrivate::saveSettings()
{
    QSettings settings(MACOSX_BUNDLE_GUI_IDENTIFIER, MACOSX_BUNDLE_BUNDLE_NAME);
    searchpaths.clear();
    for (int i = 0; i < ui->searchpaths->count(); ++i) {
        QListWidgetItem* item = ui->searchpaths->item(i);
        searchpaths.append(item->text());
    }
    settings.setValue("searchpaths", searchpaths);
    environmentvars.clear();
    for (int i = 0; i < ui->environmentvars->count(); ++i) {
        QListWidgetItem* item = ui->environmentvars->item(i);
        PairItem* pairitem = qobject_cast<PairItem*>(ui->environmentvars->itemWidget(item));
        QVariantMap pairmap;
        pairmap["checked"] = pairitem->isChecked();
        pairmap["name"] = pairitem->name();
        pairmap["value"] = pairitem->value();
        environmentvars.append(pairmap);
    }
    settings.setValue("environmentvars", environmentvars);
}

void
PreferencesPrivate::searchpathChanged()
{
    QList<QListWidgetItem*> selectedItems = ui->searchpaths->selectedItems();
    if (!selectedItems.isEmpty()) {
        ui->removeSearchpath->setEnabled(true);
    } else {
        ui->removeSearchpath->setEnabled(false);
    }
}

void
PreferencesPrivate::environmentvarsChanged()
{
    QList<QListWidgetItem*> selectedItems = ui->environmentvars->selectedItems();
    if (!selectedItems.isEmpty()) {
        ui->removeEnvironmentvar->setEnabled(true);
    } else {
        ui->removeEnvironmentvar->setEnabled(false);
    }
}

void
PreferencesPrivate::addSearchpath()
{
    QString dir = QFileDialog::getExistingDirectory(
                        dialog.data(),
                        tr("Add folder"),
                        searchpathfrom,
                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );
        if (!dir.isEmpty()) {
            bool found = false;
            for (int i = 0; i < ui->searchpaths->count(); ++i) {
                QListWidgetItem *item = ui->searchpaths->item(i);
                if (item->text() == dir) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ui->searchpaths->addItem(dir);
                searchpathfrom = dir;
            }
        }
}

void
PreferencesPrivate::removeSearchpath()
{
    removeSelection(ui->searchpaths);
}

void
PreferencesPrivate::addEnvironmentvar()
{
    PairItem* pairitem = new PairItem();
    QListWidgetItem* item = new QListWidgetItem(ui->environmentvars);
    item->setSizeHint(pairitem->sizeHint());
    ui->environmentvars->addItem(item);
    ui->environmentvars->setItemWidget(item, pairitem);
    
}

void
PreferencesPrivate::removeEnvironmentvar()
{
    removeSelection(ui->environmentvars);
}

void
PreferencesPrivate::close()
{
    dialog->close();
}

void
PreferencesPrivate::removeSelection(QListWidget* widget)
{
    QList<QListWidgetItem*> selectedItems = widget->selectedItems();
    for (QListWidgetItem* item : selectedItems) {
        delete widget->takeItem(widget->row(item));
    }
}

QString
PreferencesPrivate::open(const QString& path)
{
    return QFileDialog::getExistingDirectory(
      dialog.data(),
      tr("Add folder"),
      path,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
}

#include "preferences.moc"

Preferences::Preferences(QWidget* parent)
: QDialog(parent)
, p(new PreferencesPrivate())
{
    p->dialog = this;
    p->init();
}

Preferences::~Preferences()
{
}
