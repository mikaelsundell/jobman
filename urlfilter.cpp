// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "urlfilter.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QDebug>

Urlfilter::Urlfilter(QObject* parent)
: QObject(parent)
{
}

Urlfilter::~Urlfilter()
{
}

bool
Urlfilter::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::DragEnter) {
        QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent *>(event);
        if (dragEvent->mimeData()->hasUrls()) {
            QList<QUrl> urls = dragEvent->mimeData()->urls();
            if (!urls.isEmpty()) {
                QString path = urls.first().toLocalFile();
                QFileInfo fileInfo(path);
                if (fileInfo.isDir() || fileInfo.isFile()) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
            }
        }
        return false;
    } else if (event->type() == QEvent::Drop) {
        QDropEvent* dropEvent = static_cast<QDropEvent *>(event);
        QList<QUrl> urls = dropEvent->mimeData()->urls();
        if (!urls.isEmpty()) {
            QUrl url = urls.first();
            QString path = url.toLocalFile();
            QFileInfo fileinfo(path);
            QString dirpath;
            if (fileinfo.isFile()) {
                dirpath = fileinfo.absolutePath();
            } else if (fileinfo.isDir()) {
                dirpath = fileinfo.absoluteFilePath();
            }
            if (dirpath.endsWith('\\') || dirpath.endsWith('/')) {
                dirpath.chop(1);
            }
            if (!dirpath.isEmpty()) {
                bool found = false;
                if (QListWidget* listWidget = qobject_cast<QListWidget*>(obj)) {
                    bool exists = false;
                    for (int i = 0; i < listWidget->count(); ++i) {
                        if (listWidget->item(i)->text() == dirpath) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        listWidget->addItem(dirpath);
                        found = true;
                    }
                }
                else if (QLabel* label = qobject_cast<QLabel*>(obj)) {
                    QFontMetrics metrics(label->font());
                    label->setText(metrics.elidedText(dirpath, Qt::ElideRight, label->maximumSize().width()));
                    found = true;
                }
                else if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(obj)) {
                    lineEdit->setText(dirpath);
                    found = true;
                }
                urlRequested(url);
                if (found) {
                    dropEvent->acceptProposedAction();
                    urlChanged(url);
                    return true;
                }
                
            }
        }
        return false;
    }
    return QObject::eventFilter(obj, event);
}

