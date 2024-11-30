// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "filedrop.h"

#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QPointer>
#include <QStyle>

class FiledropPrivate : public QObject
{
    Q_OBJECT
    public:
        FiledropPrivate();
        void init();
        void update();

    public:
        QList<QString> files;
        QPointer<QLabel> label;
        QPointer<Filedrop> widget;
};

FiledropPrivate::FiledropPrivate()
{
}

void 
FiledropPrivate::init()
{
    widget->setAcceptDrops(true);
    widget->setAttribute(Qt::WA_StyledBackground, true); // needed for stylesheet
    // layout
    QVBoxLayout* layout = new QVBoxLayout(widget);
    label = new QLabel(widget);
    QPixmap pixmap(":/icons/resources/Arrow.png");
    label->setPixmap(pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    label->setFixedSize(64, 64);
    layout->addWidget(label, 0, Qt::AlignHCenter);
}

void
FiledropPrivate::update()
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

#include "filedrop.moc"

Filedrop::Filedrop(QWidget* parent)
: QWidget(parent)
, p(new FiledropPrivate())
{
    p->widget = this;
    p->init();
}

Filedrop::~Filedrop()
{
}

void
Filedrop::dragEnterEvent(QDragEnterEvent* event)
{
    Q_UNUSED(event);
    if (event->mimeData()->hasUrls()) {
        p->files.clear();
        bool found = false;
        QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls) {
            QFileInfo fileinfo(url.toLocalFile());
            if (fileinfo.isFile()) {
                p->files.append(fileinfo.filePath());
                found = true;
            }
        }
        if (found) {
            event->acceptProposedAction();
            setProperty("dragging", true);
            p->update();
        } else {
            event->ignore();
        }
    } else {
        event->ignore();
    }
}

void
Filedrop::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    setProperty("dragging", false);
    p->update();
}

void
Filedrop::dropEvent(QDropEvent* event)
{
    Q_UNUSED(event);
    setProperty("dragging", false);
    p->update();
    event->accept();
    filesDropped(p->files);
}
