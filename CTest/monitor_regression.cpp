// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause

#include "jobtree.h"
#include "monitor.h"
#include "queue.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>

#include <functional>
#include <stdexcept>

namespace {
void
require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void
waitFor(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        require(timer.elapsed() < 15000, "Timed out waiting for the real monitor slots");
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}
}  // namespace

int
main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Could not create monitor fixtures");
        Monitor monitor;
        auto* tree = monitor.findChild<JobTree*>("items");
        auto* log = monitor.findChild<QTextBrowser*>("job");
        auto* stop = monitor.findChild<QAbstractButton*>("stop");
        auto* metrics = monitor.findChild<QLabel*>("metrics");
        require(tree && log && stop && metrics, "Monitor widgets were not found");
        require(tree->headerItem()->text(2) == "Time", "Monitor did not expose the new Time column");
        Queue* queue = Queue::instance();
        queue->setThreads(1);
        QSharedPointer<Job> job(new Job());
        job->setName("monitor-job");
        job->setCommand(QFileInfo(application.arguments().value(1)).absoluteFilePath());
        job->setFilename(job->command());
        job->setDir(temporary.path());
        job->setOutput(temporary.filePath("result.txt"));
        job->setOverwrite(true);
        job->setArguments({ "job", job->output(), "monitor-job", "none", "0", "2300" });
        queue->submit(job);
        waitFor([&]() { return tree->topLevelItemCount() == 1; });
        QTreeWidgetItem* item = tree->topLevelItem(0);
        tree->setCurrentItem(item);
        item->setSelected(true);
        waitFor([&]() {
            return item->text(4) == "Running" && stop->isEnabled() && log->toPlainText().contains("\nStarted:\n");
        });
        job->setPriority(77);
        waitFor([&]() { return item->text(3) == "77"; });
        waitFor([&]() { return item->text(4) == "Running" && item->text(2) != "00:00:00"; });
        require(item->toolTip(2).contains("Created: ") && item->toolTip(2).contains("Started: ")
                    && item->toolTip(2).contains("Duration: "),
                "Running time tooltip lost timestamps or duration");
        waitFor([&]() {
            return !queue->isProcessing() && item->text(4) == "Completed" && log->toPlainText() == job->log();
        });
        require(!stop->isEnabled(), "Monitor stop button did not follow completion");
        for (const auto stale : { Job::Running, Job::Waiting, Job::Failed, Job::Stopped }) {
            require(QMetaObject::invokeMethod(job.data(), "statusChanged", Qt::DirectConnection,
                                              Q_ARG(Job::Status, stale)),
                    "Could not deliver a delayed status notification");
            QCoreApplication::processEvents();
            require(item->text(4) == "Completed" && !stop->isEnabled(),
                    "A delayed status payload overwrote the current model state");
        }
        require(job->started().isValid() && job->finished() >= job->started() && item->toolTip(2).contains("Finished: ")
                    && metrics->text() == "Files: 1 - Jobs: 1 (1) - (completed: 1)",
                "Completed execution times or selected-job metrics were incorrect");
        require(log->toPlainText().contains("Command output:\nstdout:monitor-job\n")
                    && log->toPlainText().contains("Command error:\nstderr:monitor-job\n"),
                "Monitor log slot did not receive the captured output");
        QFile artifact(job->output());
        require(artifact.open(QIODevice::ReadOnly) && artifact.readAll() == "artifact:monitor-job\n",
                "Monitor job output differed from the queue result");
        monitor.show();
        QCoreApplication::processEvents();
        bool menuValid = false;
        QTimer::singleShot(0, &monitor, [&]() {
            if (auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                for (QAction* action : popup->actions()) {
                    if (action->text().startsWith("Show in ") && action->menu()) {
                        const auto entries = action->menu()->actions();
                        menuValid = entries.size() == 2 && entries[0]->text() == "Input file"
                                    && entries[1]->text() == "Output file";
                    }
                }
                popup->close();
            }
        });
        tree->customContextMenuRequested(tree->visualItemRect(item).center());
        require(menuValid, "File-browser menu did not show Input file then Output file");
        queue->restart(job->uuid());
        waitFor([&]() {
            return !queue->isProcessing() && item->text(4) == "Completed" && log->toPlainText() == job->log()
                   && log->toPlainText().contains("\nRestarted:\n");
        });
        require(log->toPlainText().contains("\nFilename:\n" + job->filename() + " (")
                    && !log->toPlainText().contains("\nCreated:\n"),
                "Monitor restart log lost its filename/size or restart header");
        queue->remove(job->uuid());
        waitFor([&]() { return tree->topLevelItemCount() == 0 && log->toPlainText().isEmpty(); });
        require(metrics->text() == "Files: 0 - Jobs: 0", "Removal did not clear the job metrics");
        QSharedPointer<Job> failed(new Job());
        QSharedPointer<Job> dependent(new Job());
        for (const auto& next : { failed, dependent }) {
            next->setCommand(job->command());
            next->setFilename(job->filename());
            next->setDir(temporary.path());
            next->setOutput(temporary.filePath(next == failed ? "failed.txt" : "dependent.txt"));
            next->setArguments({ "job", next->output(), "failed", "none", "7", "1000" });
        }
        dependent->setDependson(failed->uuid());
        queue->submit({ failed, dependent });
        waitFor([&]() { return tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 1; });
        auto* waiting = tree->topLevelItem(0)->child(0);
        require(waiting->text(2) == dependent->created().toString("HH:mm:ss"),
                "Waiting job did not display its creation time");
        waitFor(
            [&]() { return !queue->isProcessing() && metrics->text().contains("failed: 1, dependency failed: 1"); });
        require(metrics->text().startsWith("Files: 1 - Jobs: 2 - ") && waiting->toolTip(2).contains("Finished: ")
                    && !dependent->started().isValid(),
                "Failure metrics or dependency-failure timestamp were incorrect");
        queue->remove(failed->uuid());
        waitFor([&]() { return tree->topLevelItemCount() == 0; });
        qInfo() << "PASS real monitor submission, status, priority, log, buttons, and removal slots";
        return 0;
    } catch (const std::exception& error) {
        qCritical() << "FAIL monitor_signals" << error.what();
        return 1;
    }
}
