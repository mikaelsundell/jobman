// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "jobtree.h"
#include "icctransform.h"

#include <QDebug>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStyledItemDelegate>

class JobTreePrivate : public QObject {
    Q_OBJECT
public:
    JobTreePrivate();
    void init();

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

void
JobTree::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
        for (int i = 0; i < topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = topLevelItem(i);
            item->setSelected(true);
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
