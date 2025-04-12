// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "monitor.h"
#include "icctransform.h"
#include "question.h"
#include "queue.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QSharedPointer>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidgetItem>

// generated files
#include "ui_monitor.h"
#include "ui_progressbar.h"

class MonitorPrivate : public QObject {
    Q_OBJECT
public:
    enum Priority { Critical = 1000, High = 100, Medium = 10, Low = 0 };
    Q_ENUM(Priority)

    enum Column { Name = 0, Filename = 1, Created = 2, Priority_ = 3, Status = 4, Progress = 5 };
    Q_ENUM(Column)

public:
    MonitorPrivate();
    void init();
    void updateJob(const QUuid& uuid);
    void updateProgress(QTreeWidgetItem* item);
    void updatePriority(Priority priority);
    void updateMetrics();
    bool eventFilter(QObject* object, QEvent* event);

public Q_SLOTS:
    void jobSubmitted(QSharedPointer<Job> job);
    void jobRemoved(const QUuid& uuid);
    void logChanged(const QString& log);
    void priorityChanged(int priority);
    void statusChanged(Job::Status status);
    void selectionChanged();
    void toggleButtons();
    void start();
    void stop();
    void restart();
    void priority();
    void remove();
    void copyUuid();
    void copyCommand();
    void showInFinder();
    void running();
    void restore();
    void stopped();
    void cleanup();
    void close();
    void showMenu(const QPoint& pos);

public:
    class StatusDelegate : public QStyledItemDelegate {
    public:
        StatusDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
        {}
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            opt.widget->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);
            QColor color;
            QString status = index.data().toString();
            // icc profile
            ICCTransform* transform = ICCTransform::instance();
            if (status == "Dependency failed" || status == "Failed" || status == "Cancelled") {
                color = transform->map(QColor::fromHsl(359, 90, 40).rgb());
            }
            else if (status == "Waiting") {
                color = transform->map(QColor::fromHsl(309, 150, 50).rgb());
            }
            else if (status == "Stopped") {
                color = transform->map(QColor::fromHsl(309, 90, 40).rgb());
            }
            else if (status == "Running") {
                color = transform->map(QColor::fromHsl(120, 150, 50).rgb());
            }
            else if (status == "Completed") {
                color = transform->map(QColor::fromHsl(120, 90, 40).rgb());
            }
            else {
                color = Qt::transparent;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            QFontMetrics metrics(painter->font());
            int textWidth = metrics.horizontalAdvance(status);
            int textHeight = metrics.height();
            int leftPadding = 4;
            QRect textRect(option.rect.left() + leftPadding + 1, (option.rect.center().y() - textHeight / 2) - 0,
                           textWidth, textHeight);
            painter->setBrush(color);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(textRect.adjusted(-5, -5, 5, 5), 8, 8);
            // text
            painter->setPen(Qt::white);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, status);
            painter->restore();
        }
    };
    class PriorityDelegate : public QStyledItemDelegate {
    public:
        PriorityDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
        {}
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            int value = index.data().toInt();
            QString text;
            switch (value) {
            case Critical: text = "Critical"; break;
            case High: text = "High"; break;
            case Medium: text = "Medium"; break;
            case Low: text = "Low"; break;
            default:
                text = QString::number(value);  // Display the number itself if it doesn't match any priority
                break;
            }
            opt.widget->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);
            painter->save();
            painter->setPen(opt.palette.color(QPalette::Text));
            painter->drawText(opt.rect, Qt::AlignLeft | Qt::AlignVCenter, text);
            painter->restore();
        }
    };
    template<typename Func> void verifyItems(Func func)
    {
        std::function<bool(QTreeWidgetItem*)> iterateItems = [&](QTreeWidgetItem* item) -> bool {
            QVariant data = item->data(0, Qt::UserRole);
            QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
            if (func(item, job)) {
                return true;
            }
            for (int i = 0; i < item->childCount(); ++i) {
                if (iterateItems(item->child(i))) {
                    return true;
                }
            }
            return false;
        };
        for (int i = 0; i < ui->items->topLevelItemCount(); ++i) {
            if (iterateItems(ui->items->topLevelItem(i))) {
                break;
            }
        }
    }
    
    template<typename Func> void removeItems(Func func)
    {
        std::function<bool(QTreeWidgetItem*)> iterateItems = [&](QTreeWidgetItem* item) -> bool {
            QVariant data = item->data(0, Qt::UserRole);
            QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
            if (func(item, job)) {
                return true;
            }
            for (int i = 0; i < item->childCount(); ++i) {
                if (item->child(i)->isSelected()) {
                    if (iterateItems(item->child(i))) {
                        return true;
                    }
                }
            }
            return false;
        };

        QList<QTreeWidgetItem*> selectedItems = ui->items->selectedItems();
        QList<QTreeWidgetItem*> filteredItems;
        for (QTreeWidgetItem* item : selectedItems) {
            QTreeWidgetItem* parent = item->parent();
            bool skip = false;
            while (parent) {
                if (selectedItems.contains(parent)) {
                    skip = true;
                    break;
                }
                parent = parent->parent();
            }
            if (!skip) {
                filteredItems.append(item);
            }
        }
        for (QTreeWidgetItem* item : filteredItems) {
            if (iterateItems(item)) {
                break;
            }
        }
    }
    template<typename Func> void selectedItems(Func func)
    {
        std::function<bool(QTreeWidgetItem*)> iterateItems = [&](QTreeWidgetItem* item) -> bool {
            QVariant data = item->data(0, Qt::UserRole);
            QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
            if (func(item, job)) {
                return true;
            }
            for (int i = 0; i < item->childCount(); ++i) {
                if (item->child(i)->isSelected()) {
                    if (iterateItems(item->child(i))) {
                        return true;
                    }
                }
            }
            return false;
        };
        QList<QTreeWidgetItem*> selectedItems = ui->items->selectedItems();
        for (QTreeWidgetItem* selectedItem : selectedItems) {
            if (iterateItems(selectedItem)) {
                break;
            }
        }
    }
    QTreeWidgetItem* findTopLevelItem(QTreeWidgetItem* item);
    QTreeWidgetItem* findItemByUuid(const QUuid& uuid);
    QSharedPointer<Job> itemJob(QTreeWidgetItem* item);
    QSize size;
    QHash<QUuid, QTreeWidgetItem*> jobs;
    QPointer<Queue> queue;
    QPointer<Monitor> dialog;
    QScopedPointer<Ui_Monitor> ui;
};

MonitorPrivate::MonitorPrivate() { qRegisterMetaType<QSharedPointer<Job>>("QSharedPointer<Job>"); }

void
MonitorPrivate::init()
{
    // queue
    queue = Queue::instance();
    // ui
    ui.reset(new Ui_Monitor());
    ui->setupUi(dialog);
    ui->items->setHeaderLabels(QStringList() << "Name"
                                             << "Filename"
                                             << "Created"
                                             << "Priority"
                                             << "Status"
                                             << "Progress");
    ui->items->setColumnWidth(Name, 160);
    ui->items->setColumnWidth(Filename, 130);
    ui->items->setColumnWidth(Created, 140);
    ui->items->setColumnWidth(Priority_, 75);
    ui->items->setColumnWidth(Status, 115);
    ui->items->setColumnWidth(Progress, 40);
    ui->items->sortByColumn(Created, Qt::AscendingOrder);
    ui->items->header()->setStretchLastSection(true);
    ui->items->setItemDelegateForColumn(3, new PriorityDelegate(ui->items));
    ui->items->setItemDelegateForColumn(4, new StatusDelegate(ui->items));
    ui->items->setContextMenuPolicy(Qt::CustomContextMenu);
    QPalette palette = ui->job->palette(); // workaround for unfocused textbrowser
    QColor textcolor = QColor::fromHslF(0.0, 0.0, 0.8);
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, textcolor);
    ui->job->setPalette(palette);
    // event filter
    dialog->installEventFilter(this);
    // layout
    // connect
    connect(ui->start, &QPushButton::pressed, this, &MonitorPrivate::start);
    connect(ui->stop, &QPushButton::pressed, this, &MonitorPrivate::stop);
    connect(ui->restart, &QPushButton::pressed, this, &MonitorPrivate::restart);
    connect(ui->priority, &QPushButton::pressed, this, &MonitorPrivate::priority);
    connect(ui->remove, &QPushButton::pressed, this, &MonitorPrivate::remove);
    connect(ui->running, &QPushButton::pressed, this, &MonitorPrivate::running);
    connect(ui->stopped, &QPushButton::pressed, this, &MonitorPrivate::stopped);
    connect(ui->restore, &QPushButton::pressed, this, &MonitorPrivate::restore);
    connect(ui->cleanup, &QPushButton::pressed, this, &MonitorPrivate::cleanup);
    connect(ui->close, &QPushButton::pressed, this, &MonitorPrivate::close);
    connect(ui->items, &JobTree::itemSelectionChanged, this, &MonitorPrivate::selectionChanged);
    connect(ui->items, &QTreeWidget::customContextMenuRequested, this, &MonitorPrivate::showMenu);
    connect(queue.data(), &Queue::jobSubmitted, this, &MonitorPrivate::jobSubmitted);
    connect(queue.data(), &Queue::jobRemoved, this, &MonitorPrivate::jobRemoved);
}

void
MonitorPrivate::updateJob(const QUuid& uuid)
{
    QTreeWidgetItem* item = findItemByUuid(uuid);
    QVariant data = item->data(0, Qt::UserRole);
    QSharedPointer<Job> itemjob = data.value<QSharedPointer<Job>>();
    if (itemjob->uuid() == uuid) {
        item->setText(Name, itemjob->name());
        item->setText(Filename, QFileInfo(itemjob->filename()).fileName());
        item->setText(Created, itemjob->created().toString("yyyy-MM-dd HH:mm:ss"));
        item->setText(Priority_, QString::number(itemjob->priority()));
        switch (itemjob->status()) {
        case Job::Waiting: {
            item->setText(Status, "Waiting");
        } break;
        case Job::Running: {
            item->setText(Status, "Running");
        } break;
        case Job::Completed: {
            item->setText(Status, "Completed");
        } break;
        case Job::DependencyFailed: {
            item->setText(Status, "Dependency failed");
        } break;
        case Job::Failed: {
            item->setText(Status, "Failed");
        } break;
        case Job::Stopped: {
            item->setText(Status, "Stopped");
        } break;
        }
        QWidget* widget = ui->items->itemWidget(item, Progress);
        if (!item->parent()) {
            if (widget) {
                QProgressBar* progress = widget->findChild<QProgressBar*>("progress");
                progress->setValue(50);
            }
            else {
                QWidget* container = new QWidget(ui->items);
                Ui::ProgressBar progressbar;
                progressbar.setupUi(container);
                progressbar.progress->setValue(50);
                progressbar.spinner->setVisible(false);
                ui->items->setItemWidget(item, Progress, container);
            }
        }
        updateProgress(item);
        if (item->isSelected()) {
            ui->job->setText(itemjob->log());
        }
    }
    toggleButtons();
}

void
MonitorPrivate::updateProgress(QTreeWidgetItem* item)
{
    QTreeWidgetItem* topLevelItem = findTopLevelItem(item);
    int items = 0, running = 0, totals = 0;
    std::function<void(QTreeWidgetItem*)> progressItems = [&](QTreeWidgetItem* parentItem) {
        totals++;
        QVariant data = parentItem->data(0, Qt::UserRole);
        QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
        if (job->status() == Job::Running) {
            running++;
        }
        if (job->status() == Job::Completed || job->status() == Job::Failed || job->status() == Job::Stopped) {
            items++;
        }
        for (int i = 0; i < parentItem->childCount(); ++i) {
            progressItems(parentItem->child(i));
        }
    };
    progressItems(topLevelItem);
    int progress = (totals > 0) ? (items * 100 / totals) : 0;
    QWidget* widget = ui->items->itemWidget(topLevelItem, Progress);
    QProgressBar* progressbar = widget->findChild<QProgressBar*>("progress");
    progressbar->setValue(progress);
    QLabel* status = widget->findChild<QLabel*>("status");
    status->setText(QString("%1 / %2").arg(items).arg(totals));
    QLabel* spinner = widget->findChild<QLabel*>("spinner");
    if (running) {
        if (!spinner->isVisible()) {
            spinner->show();
        }
    }
    else {
        spinner->hide();
    }
    updateMetrics();
}

void
MonitorPrivate::updatePriority(enum Priority priority)
{
    selectedItems([this, &priority](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        QTreeWidgetItem* parentItem = findItemByUuid(job->uuid());
        std::function<void(QTreeWidgetItem*)> priorityItems = [&](QTreeWidgetItem* parentItem) {
            QVariant data = parentItem->data(0, Qt::UserRole);
            QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
            job->setPriority(priority);
            for (int i = 0; i < parentItem->childCount(); ++i) {
                priorityItems(parentItem->child(i));
            }
        };
        priorityItems(parentItem);
        return false;
    });
}

void
MonitorPrivate::updateMetrics()
{
    int waitingCount = 0;
    int completedCount = 0;
    int dependencyFailedCount = 0;
    int stoppedCount = 0;
    int runningCount = 0;
    int failedCount = 0;
    for (auto it = jobs.constBegin(); it != jobs.constEnd(); ++it) {
        QTreeWidgetItem* item = it.value();
        QVariant data = item->data(0, Qt::UserRole);
        QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
        switch (job->status()) {
        case Job::Waiting: waitingCount++; break;
        case Job::Running: runningCount++; break;
        case Job::Completed: completedCount++; break;
        case Job::DependencyFailed: dependencyFailedCount++; break;
        case Job::Failed: failedCount++; break;
        case Job::Stopped: stoppedCount++; break;
        default: break;
        }
    }
    QStringList parts;
    if (waitingCount > 0)
        parts << QString("waiting: %1").arg(waitingCount);
    if (runningCount > 0)
        parts << QString("running: %1").arg(runningCount);
    if (completedCount > 0)
        parts << QString("completed: %1").arg(completedCount);
    if (stoppedCount > 0)
        parts << QString("stopped: %1").arg(stoppedCount);
    if (failedCount > 0)
        parts << QString("failed: %1").arg(failedCount);
    QString metricsText = QString("Files: %1").arg(ui->items->topLevelItemCount());
    if (parts.count()) {
        QString text = "Jobs: " + parts.join(", ");
        metricsText.append(QString(" (%1)").arg(text));
    }
    int total = waitingCount + runningCount + completedCount;
    int percentage = (total > 0) ? (completedCount * 100) / total : 0;
    if (percentage > 0 && percentage < 100) {
        metricsText.append(QString(" - %1%").arg(percentage));
    }
    ui->metrics->setText(metricsText);
}

bool
MonitorPrivate::eventFilter(QObject* object, QEvent* event)
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
MonitorPrivate::jobSubmitted(QSharedPointer<Job> job)
{
    QTreeWidgetItem* parent = nullptr;
    QUuid dependsonUuid = job->dependson();
    if (!dependsonUuid.isNull()) {
        parent = findItemByUuid(dependsonUuid);
    }
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setData(0, Qt::UserRole, QVariant::fromValue(job));
    if (parent) {
        parent->addChild(item);
    }
    else {
        ui->items->addTopLevelItem(item);
    }
    // connect
    connect(job.data(), &Job::logChanged, this, &MonitorPrivate::logChanged, Qt::QueuedConnection);
    connect(job.data(), &Job::priorityChanged, this, &MonitorPrivate::priorityChanged, Qt::QueuedConnection);
    connect(job.data(), &Job::statusChanged, this, &MonitorPrivate::statusChanged, Qt::QueuedConnection);
    // update
    jobs.insert(job->uuid(), item);
    updateMetrics();
    updateJob(job->uuid());
}

void
MonitorPrivate::jobRemoved(const QUuid& uuid)
{
    verifyItems([this, &uuid](QTreeWidgetItem* item, const QSharedPointer<Job>& job) -> bool {
        if (job->uuid() == uuid) {
            if (item->isSelected()) {
                item->setSelected(false);
            }
            QTreeWidgetItem* parent = item->parent();
            if (parent) {
                parent->removeChild(item);
                delete item;
            }
            else {
                int index = ui->items->indexOfTopLevelItem(item);
                if (index != -1) {
                    delete ui->items->takeTopLevelItem(index);
                }
            }
            jobs.remove(uuid);
            return true;
        }
        return false;
    });
    updateMetrics();
}

void
MonitorPrivate::logChanged(const QString& log)
{
    QUuid uuid = qobject_cast<Job*>(sender())->uuid();
    if (jobs.contains(uuid)) {
        QTreeWidgetItem* item = jobs[uuid];
        if (item->isSelected()) {
            ui->job->setText(log);
        }
    }
}

void
MonitorPrivate::priorityChanged(int priority)
{
    QUuid uuid = qobject_cast<Job*>(sender())->uuid();
    if (jobs.contains(uuid)) {
        QTreeWidgetItem* item = jobs[uuid];
        item->setText(Priority_, QString::number(priority));
    }
}

void
MonitorPrivate::statusChanged(Job::Status status)
{
    QUuid uuid = qobject_cast<Job*>(sender())->uuid();
    if (jobs.contains(uuid)) {
        QTreeWidgetItem* item = jobs[uuid];
        switch (status) {
        case Job::Waiting: {
            item->setText(Status, "Waiting");
        } break;
        case Job::Running: {
            item->setText(Status, "Running");
        } break;
        case Job::Completed: {
            item->setText(Status, "Completed");
        } break;
        case Job::DependencyFailed: {
            item->setText(Status, "Dependency failed");
        } break;
        case Job::Failed: {
            item->setText(Status, "Failed");
        } break;
        case Job::Stopped: {
            item->setText(Status, "Stopped");
        } break;
        }
        updateProgress(item);
        updateMetrics();
        toggleButtons();
    }
}

void
MonitorPrivate::selectionChanged()
{
    qint64 selectionCount = ui->items->selectedItems().count();
    if (selectionCount > 0) {
        if (selectionCount == 1) {
            QTreeWidgetItem* item = ui->items->selectedItems().first();
            QVariant data = item->data(0, Qt::UserRole);
            QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
            ui->job->setText(job->log());
        }
        else {
            ui->job->setText("[Multiple selection]");
        }
    }
    else {
        ui->job->setText(QString());
    }
    toggleButtons();
}

void
MonitorPrivate::toggleButtons()
{
    bool start = false;
    bool stop = false;
    bool restart = false;
    bool priority = false;
    bool cleanup = false;
    selectedItems([&start, &stop, &restart, &priority, &cleanup](const QTreeWidgetItem* item,
                                                                 const QSharedPointer<Job>& job) {
        if (job->status() == Job::Stopped) {
            start = true;
        }
        if (job->status() == Job::Running) {
            stop = true;
        }
        if (job->status() == Job::Completed || job->status() == Job::Stopped || job->status() == Job::DependencyFailed
            || job->status() == Job::Failed) {
            std::function<bool(const QTreeWidgetItem*)> restartItems = [&](const QTreeWidgetItem* parentItem) -> bool {
                for (int i = 0; i < parentItem->childCount(); ++i) {
                    QTreeWidgetItem* child = parentItem->child(i);
                    QVariant data = child->data(0, Qt::UserRole);
                    QSharedPointer<Job> childJob = data.value<QSharedPointer<Job>>();
                    if (childJob->status() == Job::Running || !restartItems(child)) {
                        return false;
                    }
                }
                return true;
            };
            if (restartItems(item)) {
                restart = true;
            }
        }
        priority = true;
        return false;
    });
    verifyItems([&cleanup](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) -> bool {
        if (job->status() == Job::Completed) {
            cleanup = true;
            return true;
        }
        return false;
    });
    ui->start->setEnabled(start);
    ui->stop->setEnabled(stop);
    ui->restart->setEnabled(restart);
    ui->priority->setEnabled(priority);
    if (ui->items->topLevelItemCount() > 0) {
        ui->running->setEnabled(true);
        ui->stopped->setEnabled(true);
        ui->restore->setEnabled(true);
        ui->remove->setEnabled(true);
    }
    else {
        ui->running->setEnabled(false);
        ui->stopped->setEnabled(false);
        ui->restore->setEnabled(false);
        ui->remove->setEnabled(false);
    }
    ui->cleanup->setEnabled(cleanup);
}

void
MonitorPrivate::start()
{
    selectedItems([this](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        queue->start(job->uuid());
        return false;
    });
    toggleButtons();
}

void
MonitorPrivate::stop()
{
    selectedItems([this](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        queue->stop(job->uuid());
        return false;
    });
    toggleButtons();
}

void
MonitorPrivate::restart()
{
    QList<QTreeWidgetItem*> items;
    selectedItems([&](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        QTreeWidgetItem* parent = const_cast<QTreeWidgetItem*>(item);
        while (parent->parent()) {
            parent = parent->parent();
        }
        if (!items.contains(parent)) {
            items.append(parent);
        }
        return false;
    });
    QList<QUuid> uuids;
    for (QTreeWidgetItem* item : items) {
        QSharedPointer<Job> job = item->data(0, Qt::UserRole).value<QSharedPointer<Job>>();
        if (job) {
            uuids.push_back(job->uuid());
        }
    }
    queue->restart(uuids);
    toggleButtons();
}

void
MonitorPrivate::priority()
{
    QMenu* priority = new QMenu("Priority", ui->items);
    QAction* critical = new QAction("Critical", this);
    QAction* high = new QAction("High", this);
    QAction* medium = new QAction("Medium", this);
    QAction* low = new QAction("Low", this);
    connect(critical, &QAction::triggered, this, [this]() { updatePriority(Critical); });
    connect(high, &QAction::triggered, this, [this]() { updatePriority(High); });
    connect(medium, &QAction::triggered, this, [this]() { updatePriority(Medium); });
    connect(low, &QAction::triggered, this, [this]() { updatePriority(Low); });
    priority->addAction(critical);
    priority->addAction(high);
    priority->addAction(medium);
    priority->addAction(low);
    QPoint pos = ui->priority->mapToGlobal(QPoint(0, ui->priority->height()));
    priority->exec(pos);
}

void
MonitorPrivate::remove()
{
    qint64 selectionCount = ui->items->selectedItems().count();
    if (selectionCount > 10) {
        if (!Question::askQuestion(dialog.data(),
                                   "More than 10 jobs have been selected for removal. Do you want to proceed?\n")) {
            return;
        }
    }
    removeItems([this](QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        item->setSelected(false);
        queue->remove(job->uuid());
        return false;
    });
    toggleButtons();
}

void
MonitorPrivate::copyUuid()
{
    QList<QString> uuids;
    selectedItems([this, &uuids](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        QTreeWidgetItem* parentItem = findItemByUuid(job->uuid());

        std::function<void(QTreeWidgetItem*)> uuidItems = [&](QTreeWidgetItem* item) {
            QVariant data = item->data(0, Qt::UserRole);
            QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
            if (job) {
                uuids.append(job->uuid().toString());
            }
            for (int i = 0; i < item->childCount(); ++i) {
                uuidItems(item->child(i));
            }
        };
        uuidItems(parentItem);
        return false;
    });

    if (!uuids.isEmpty()) {
        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->setText(uuids.join('\n'));
    }
}

void
MonitorPrivate::copyCommand()
{
    QList<QString> commands;
    selectedItems([this, &commands](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        QTreeWidgetItem* parentItem = findItemByUuid(job->uuid());

        std::function<void(QTreeWidgetItem*)> uuidItems = [&](QTreeWidgetItem* item) {
            QVariant data = item->data(0, Qt::UserRole);
            QSharedPointer<Job> job = data.value<QSharedPointer<Job>>();
            if (job) {
                commands.append(QString("%1 %2").arg(job->command()).arg(job->arguments().join(' ')));
            }
            for (int i = 0; i < item->childCount(); ++i) {
                uuidItems(item->child(i));
            }
        };
        uuidItems(parentItem);
        return false;
    });

    if (!commands.isEmpty()) {
        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->setText(commands.join('\n'));
    }

}

void
MonitorPrivate::showInFinder()
{
    QList<QString> outputs;
    selectedItems([this, &outputs](const QTreeWidgetItem* item, const QSharedPointer<Job>& job) {
        QString output = job->dir();
        if (!outputs.contains(output) && QDir(output).exists()) {
            outputs.append(output);
        }
        return false;
    });
    if (outputs.count() > 10) {
        if (!Question::askQuestion(dialog.data(),
                                   QString("%1 jobs are selected. Are you sure you want to show all of them in Finder?")
                                       .arg(outputs.count()))) {
            return;
        }
    }
    for (const QString& output : outputs) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(output));
    }
}

void
MonitorPrivate::running()
{
    restore();
    std::function<void(QTreeWidgetItem*)> expandItems = [&](QTreeWidgetItem* item) {
        QVariant data = item->data(0, Qt::UserRole);
        QSharedPointer<Job> itemjob = data.value<QSharedPointer<Job>>();
        if (itemjob && itemjob->status() == Job::Running) {
            item->setExpanded(true);
            item->setSelected(true);
            QTreeWidgetItem* parent = item->parent();
            while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
            }
        }
        for (int i = 0; i < item->childCount(); ++i) {
            expandItems(item->child(i));
        }
    };
    for (int i = 0; i < ui->items->topLevelItemCount(); ++i) {
        expandItems(ui->items->topLevelItem(i));
    }
}

void
MonitorPrivate::restore()
{
    std::function<void(QTreeWidgetItem*)> collapseItems = [&](QTreeWidgetItem* item) {
        item->setExpanded(false);
        item->setSelected(false);
        for (int i = 0; i < item->childCount(); ++i) {
            collapseItems(item->child(i));
        }
    };
    ui->items->clearSelection();
    for (int i = 0; i < ui->items->topLevelItemCount(); ++i) {
        collapseItems(ui->items->topLevelItem(i));
    }
}

void
MonitorPrivate::stopped()
{
    restore();
    std::function<void(QTreeWidgetItem*)> expandItems = [&](QTreeWidgetItem* item) {
        QVariant data = item->data(0, Qt::UserRole);
        QSharedPointer<Job> itemjob = data.value<QSharedPointer<Job>>();
        if (itemjob && itemjob->status() == Job::Stopped) {
            item->setExpanded(true);
            item->setSelected(true);
            QTreeWidgetItem* parent = item->parent();
            while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
            }
        }
        for (int i = 0; i < item->childCount(); ++i) {
            expandItems(item->child(i));
        }
    };
    for (int i = 0; i < ui->items->topLevelItemCount(); ++i) {
        expandItems(ui->items->topLevelItem(i));
    }
}

void
MonitorPrivate::cleanup()
{
    std::function<void(QTreeWidgetItem*)> removeChilds = [&](QTreeWidgetItem* item) {
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            removeChilds(child);  // Recursively process children
        }
        QVariant data = item->data(0, Qt::UserRole);
        QSharedPointer<Job> itemJob = data.value<QSharedPointer<Job>>();
        if (itemJob) {
            jobs.remove(itemJob->uuid());
        }
    };
    std::function<bool(QTreeWidgetItem*)> itemsCompleted = [&](QTreeWidgetItem* item) -> bool {
        QVariant data = item->data(0, Qt::UserRole);
        QSharedPointer<Job> itemJob = data.value<QSharedPointer<Job>>();
        if (!itemJob || itemJob->status() != Job::Completed) {
            return false;
        }
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            if (!itemsCompleted(child)) {
                return false;
            }
        }
        return true;
    };
    for (int i = ui->items->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem* topLevelItem = ui->items->topLevelItem(i);
        if (itemsCompleted(topLevelItem)) {
            removeChilds(topLevelItem);

            if (topLevelItem->isSelected()) {
                ui->job->clear();
            }
            QVariant data = topLevelItem->data(0, Qt::UserRole);
            QSharedPointer<Job> itemJob = data.value<QSharedPointer<Job>>();
            jobs.remove(itemJob->uuid());
            delete ui->items->takeTopLevelItem(i);
        }
    }
    updateMetrics();
    toggleButtons();
}

void
MonitorPrivate::close()
{
    dialog->close();
}

void
MonitorPrivate::showMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = ui->items->itemAt(pos);
    if (item) {
        QMenu contextMenu(tr("Context Menu"), ui->items);

        QAction* start = new QAction("Start", this);
        connect(start, &QAction::triggered, this, &MonitorPrivate::start);
        start->setEnabled(ui->start->isEnabled());
        contextMenu.addAction(start);

        QAction* stop = new QAction("Stop", this);
        connect(stop, &QAction::triggered, this, &MonitorPrivate::stop);
        stop->setEnabled(ui->stop->isEnabled());
        contextMenu.addAction(stop);

        QAction* restart = new QAction("Restart", this);
        connect(restart, &QAction::triggered, this, &MonitorPrivate::restart);
        restart->setEnabled(ui->restart->isEnabled());
        contextMenu.addAction(restart);

        QAction* priority = new QAction("Priority", this);
        {
            QMenu* priorityMenu = new QMenu("Set Priority", ui->items);
            QAction* critical = new QAction("Critical", this);
            QAction* high = new QAction("High", this);
            QAction* medium = new QAction("Medium", this);
            QAction* low = new QAction("Low", this);
            // connect
            connect(critical, &QAction::triggered, this, [this]() { updatePriority(Critical); });
            connect(high, &QAction::triggered, this, [this]() { updatePriority(High); });
            connect(medium, &QAction::triggered, this, [this]() { updatePriority(Medium); });
            connect(low, &QAction::triggered, this, [this]() { updatePriority(Low); });
            priorityMenu->addAction(critical);
            priorityMenu->addAction(high);
            priorityMenu->addAction(medium);
            priorityMenu->addAction(low);
            priority->setMenu(priorityMenu);
        }
        contextMenu.addAction(priority);

        QAction* remove = new QAction("Remove", this);
        connect(remove, &QAction::triggered, this, &MonitorPrivate::remove);
        remove->setEnabled(ui->remove->isEnabled());
        contextMenu.addAction(remove);

        contextMenu.addSeparator();

        QAction* copy = new QAction("Copy", this);
        {
            QMenu* copyMenu = new QMenu("Copy", ui->items);
            QAction* uuid = new QAction("Uuid", this);
            QAction* command = new QAction("Command", this);
            // connect
            connect(uuid, &QAction::triggered, this, &MonitorPrivate::copyUuid);
            connect(command, &QAction::triggered, this, &MonitorPrivate::copyCommand);
            copyMenu->addAction(uuid);
            copyMenu->addAction(command);
            copy->setMenu(copyMenu);
        }
        contextMenu.addAction(copy);

        contextMenu.addSeparator();

        QAction* finder = new QAction("Show in finder", this);
        connect(finder, &QAction::triggered, this, &MonitorPrivate::showInFinder);
        contextMenu.addAction(finder);
        contextMenu.exec(ui->items->mapToGlobal(pos));
    }
}

QTreeWidgetItem*
MonitorPrivate::findTopLevelItem(QTreeWidgetItem* item)
{
    QTreeWidgetItem* topLevelItem = item;
    while (topLevelItem->parent() != nullptr) {
        topLevelItem = topLevelItem->parent();
    }
    return topLevelItem;
}

QTreeWidgetItem*
MonitorPrivate::findItemByUuid(const QUuid& uuid)
{
    return jobs[uuid];
}

QSharedPointer<Job>
MonitorPrivate::itemJob(QTreeWidgetItem* item)
{
    QVariant data = item->data(0, Qt::UserRole);
    return data.value<QSharedPointer<Job>>();
}

#include "monitor.moc"

Monitor::Monitor(QWidget* parent)
    : QDialog(parent)
    , p(new MonitorPrivate())
{
    p->dialog = this;
    p->init();
}

Monitor::~Monitor() {}
