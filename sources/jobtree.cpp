// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "jobtree.h"
#include "icctransform.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStyledItemDelegate>

class JobTreePrivate : public QObject {
    Q_OBJECT
public:
    JobTreePrivate();
    void init();
    void loadFilter();

public Q_SLOTS:
    void selectionChanged();

public:
    class ItemDelegate : public QStyledItemDelegate {
    public:
        ItemDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
        {}
        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            QSize size = QStyledItemDelegate::sizeHint(option, index);
            size.setHeight(30);
            return size;
        }
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            QTreeWidgetItem* item = static_cast<const QTreeWidget*>(opt.widget)->itemFromIndex(index);
            std::function<bool(QTreeWidgetItem*)> hasSelectedChildren = [&](QTreeWidgetItem* parentItem) -> bool {
                for (int i = 0; i < parentItem->childCount(); ++i) {
                    QTreeWidgetItem* child = parentItem->child(i);
                    if (child->isSelected() || hasSelectedChildren(child)) {
                        return true;
                    }
                }
                return false;
            };
            if (hasSelectedChildren(item)) {
                opt.font.setBold(true);
                opt.font.setItalic(true);
            }
            QStyledItemDelegate::paint(painter, opt, index);
        }
        bool hasSelectedChildren(QTreeWidgetItem* item) const
        {
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem* child = item->child(i);
                if (child->isSelected() || hasSelectedChildren(child)) {
                    return true;
                }
            }
            return false;
        }
    };

public:
    QString filter;
    QPointer<JobTree> widget;
};

JobTreePrivate::JobTreePrivate() {}

void
JobTreePrivate::init()
{
    ItemDelegate* delegate = new ItemDelegate(widget.data());
    widget->setItemDelegate(delegate);
    // connect
    connect(widget.data(), &QTreeWidget::itemSelectionChanged, this, &JobTreePrivate::selectionChanged);
}

void
JobTreePrivate::loadFilter()
{
    std::function<bool(QTreeWidgetItem*)> matchfilter = [&](QTreeWidgetItem* item) -> bool {
        bool matches = false;
        for (int col = 0; col < widget->columnCount(); ++col) {
            if (item->text(col).contains(filter, Qt::CaseInsensitive)) {
                matches = true;
                break;
            }
        }
        bool childMatches = false;
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            if (matchfilter(child)) {
                childMatches = true;
            }
        }

        bool visible = matches || childMatches;
        item->setHidden(!visible);
        return visible;
    };
    for (int i = 0; i < widget->topLevelItemCount(); ++i) {
        matchfilter(widget->topLevelItem(i));
    }
}

void
JobTreePrivate::selectionChanged()
{
    widget->viewport()->update();  // we need to force a redraw
}

#include "jobtree.moc"

JobTree::JobTree(QWidget* parent)
    : QTreeWidget(parent)
    , p(new JobTreePrivate())
{
    p->widget = this;
    p->init();
}

JobTree::~JobTree() {}

QString
JobTree::filter() const
{
    return p->filter;
}

void
JobTree::setFilter(const QString& filter)
{
    p->filter = filter;
    p->loadFilter();
}

void
JobTree::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
        std::function<void(QTreeWidgetItem*)> selectItems = [&](QTreeWidgetItem* item) {
            item->setSelected(true);
            for (int i = 0; i < item->childCount(); ++i) {
                selectItems(item->child(i));
            }
        };
        for (int i = 0; i < topLevelItemCount(); ++i) {
            selectItems(topLevelItem(i));
        }
    }
    else {
        QTreeWidget::keyPressEvent(event);
    }
}

void
JobTree::mousePressEvent(QMouseEvent* event)
{
    QTreeWidget::mousePressEvent(event);
    if (itemAt(event->pos()) == nullptr) {
        clearSelection();
        emit itemSelectionChanged();
    }
}
