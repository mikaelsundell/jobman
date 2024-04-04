// Copyright 2022-present Contributors to the automator project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/automator

#include "log.h"

#include <QPointer>
#include <QSharedPointer>
#include <QTreeWidgetItem>
#include <QDebug>

// generated files
#include "ui_log.h"

class LogPrivate : public QObject
{
    Q_OBJECT
    public:
        LogPrivate();
        void init();
        void addJob(QSharedPointer<Job> job);
        void updateJob(const QUuid& uuid);
        bool eventFilter(QObject* object, QEvent* event);
    
    public Q_SLOTS:
        void jobChanged();
        void selectionChanged();
        void clear();
        void close();

    public:
        QTreeWidgetItem* findItemByUuid(const QUuid& uuid, QTreeWidgetItem* parent = nullptr);
        QSize size;
        QList<QSharedPointer<Job>> jobs; // prevent deletition
        QPointer<Log> dialog;
        QScopedPointer<Ui_Log> ui;
};

LogPrivate::LogPrivate()
{
    qRegisterMetaType<QSharedPointer<Job>>("QSharedPointer<Job>");
}

void
LogPrivate::init()
{
    // ui
    ui.reset(new Ui_Log());
    ui->setupUi(dialog);
    ui->items->setHeaderLabels(
        QStringList() << "Uuid"
                      << "Name"
                      << "Status"
    );
    ui->items->setColumnWidth(0, 280);
    ui->items->setColumnWidth(1, 250);
    ui->items->header()->setStretchLastSection(true);
    // event filter
    dialog->installEventFilter(this);
    // layout
    // connect
    connect(ui->items, &QTreeWidget::itemSelectionChanged, this, &LogPrivate::selectionChanged);
    connect(ui->close, &QPushButton::pressed, this, &LogPrivate::close);
    connect(ui->clear, &QPushButton::pressed, this, &LogPrivate::clear);
}

void
LogPrivate::addJob(QSharedPointer<Job> job)
{
    QTreeWidgetItem* parentItem = nullptr;
    QUuid dependsonUuid = job->dependson();
    if (!dependsonUuid.isNull()) {
       parentItem = findItemByUuid(dependsonUuid);
    }
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setData(0, Qt::UserRole, QVariant::fromValue(job));
    if (parentItem) {
        parentItem->addChild(item);
    } else {
        ui->items->addTopLevelItem(item);
    }
    updateJob(job->uuid());
    // connect
    connect(job.data(), SIGNAL(jobChanged()), this, SLOT(jobChanged()));
    jobs.append(job);
}

void
LogPrivate::updateJob(const QUuid& uuid)
{
    QTreeWidgetItem* item = findItemByUuid(uuid);
    QVariant data = item->data(0, Qt::UserRole);
    QSharedPointer<Job> itemjob = data.value<QSharedPointer<Job>>();
    
    if (itemjob->uuid() == uuid) {
        item->setText(0, itemjob->uuid().toString());
        item->setText(1, itemjob->name());
        switch(itemjob->status())
        {
            case Job::Pending: {
                item->setText(2, "Pending");
            }
            break;
            case Job::Running: {
                item->setText(2, "Running");
            }
            break;
            case Job::Completed: {
                item->setText(2, "Completed");
            }
            break;
            case Job::Failed: {
                item->setText(2, "Failed");
            }
            break;
            case Job::Cancelled: {
                item->setText(2, "Cancelled");
            }
            break;
        }
        ui->log->setText(itemjob->log());
    }
}

bool
LogPrivate::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::Show) {
        QList<int> sizes;
        int height = ui->splitter->height();
        int jobsHeight = height * 0.75;
        int logHeight = height - jobsHeight;
        sizes << jobsHeight << logHeight;
        ui->splitter->setSizes(sizes);
    }
    return false;
}

void
LogPrivate::jobChanged()
{
    updateJob(qobject_cast<Job*>(sender())->uuid());
}

void
LogPrivate::selectionChanged()
{
    QList<QTreeWidgetItem*> selectedItems = ui->items->selectedItems();
    if (!selectedItems.isEmpty()) {
        // Get the first selected item
        QTreeWidgetItem* item = selectedItems.first();
        QVariant data = item->data(0, Qt::UserRole);
        QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
        ui->log->setText(job->log());
    }
}

void
LogPrivate::clear()
{
    ui->items->clear();
    ui->log->clear();
}

void
LogPrivate::close()
{
    dialog->close();
}

QTreeWidgetItem*
LogPrivate::findItemByUuid(const QUuid& uuid, QTreeWidgetItem* parent)
{
    QTreeWidgetItem* foundItem = nullptr;
    int itemCount = parent ? parent->childCount() : ui->items->topLevelItemCount();
    for (int i = 0; i < itemCount; ++i) {
        QTreeWidgetItem* item = parent ? parent->child(i) : ui->items->topLevelItem(i);
        QVariant data = item->data(0, Qt::UserRole);
        QSharedPointer<Job> itemJob = data.value<QSharedPointer<Job>>();
        if (itemJob && itemJob->uuid() == uuid) {
            return item;
        }
        if (item->childCount() > 0) {
            foundItem = findItemByUuid(uuid, item);
            if (foundItem) {
                return foundItem;
            }
        }
    }
    return nullptr;
}

#include "log.moc"

Log::Log(QWidget* parent)
: QDialog(parent)
, p(new LogPrivate())
{
    p->dialog = this;
    p->init();
}

Log::~Log()
{
}

void
Log::addJob(QSharedPointer<Job> job)
{
    p->addJob(job);
}