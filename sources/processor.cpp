// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "processor.h"
#include "job.h"
#include "queue.h"

#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QUuid>

class ProcessorPrivate : public QObject {
    Q_OBJECT
public:
    ProcessorPrivate();
    void init();
    QList<QUuid> submit(const QList<QString>& files, const QSharedPointer<Preset>& preset, const Paths& paths);
    QList<QUuid> submit(const QSharedPointer<Preset>& preset, const Paths& paths);

public:
    QString updatePaths(const QString& input, const QString& pattern, const QFileInfo& inputinfo);
    QString updateFiles(const QString& input, const QFileInfo& inputinfo, const QFileInfo& outputinfo);
    QString updateTask(const QString& input, const QString& inputinfo, const QString& outputinfo);
    QStringList updateOptions(QList<QSharedPointer<Option>> options, const QString& input);
    void updateEnvironment(QSharedPointer<Job> job, const Paths& paths);

    QPointer<Queue> queue;
    QPointer<Processor> object;
};

ProcessorPrivate::ProcessorPrivate() {}

void
ProcessorPrivate::init()
{
    queue = Queue::instance();
}

QList<QUuid>
ProcessorPrivate::submit(const QList<QString>& files, const QSharedPointer<Preset>& preset, const Paths& paths)
{
    QList<QUuid> uuids;
    for (const QString& file : files) {
        QMap<QString, QUuid> jobuuids;
        QMap<QString, QString> joboutputs;
        QList<QPair<QSharedPointer<Job>, QString>> dependentjobs;
        QFileInfo inputinfo(file);
        bool first = true;
        for (QSharedPointer<Task> task : preset->tasks()) {
            QString extension = updatePaths(task->extension, "input", inputinfo);
            QString outputdir;
            if (paths.createpaths) {
                outputdir = paths.outputpath + "/" + inputinfo.fileName();
            }
            else {
                outputdir = paths.outputpath;
            }
            QString outputfile = outputdir + "/" + inputinfo.baseName() + "." + extension;
            QFileInfo outputinfo(outputfile);
            QString command
                = updateOptions(preset->options(), updateFiles(task->command, inputinfo, outputinfo)).join(" ");
            QString output
                = updateOptions(preset->options(), updateFiles(task->output, inputinfo, outputinfo)).join(" ");
            QStringList argumentlist = task->arguments.split(" ");
            QStringList replacedlist;
            for (QString& argument : argumentlist) {
                replacedlist.append(
                    updateOptions(preset->options(),
                                  updateTask("output", updateFiles(argument, inputinfo, outputinfo), output)));
            }
            QString startin
                = updateOptions(preset->options(), updateFiles(task->startin, inputinfo, outputinfo)).join(" ");
            // job
            QSharedPointer<Job> job(new Job());
            {
                job->setId(task->id);
                job->setFilename(inputinfo.fileName());
                job->setDir(outputdir);
                job->setName(task->name);
                job->setCommand(command);
                job->setArguments(replacedlist);
                job->setOutput(output);
                job->setOverwrite(paths.overwrite);
                job->setStartin(startin);
                job->setStatus(Job::Waiting);
            }
            updateEnvironment(job, paths);

            if (first) {
                if (paths.copyoriginal) {
                    job->preprocess()->copyoriginal.filename = file;
                }
                first = false;
            }
            if (task->dependson.isEmpty()) {
                QUuid uuid = queue->submit(job);
                jobuuids[task->id] = uuid;
                joboutputs[task->id] = job->output();
                uuids.append(uuid);
            }
            else {
                dependentjobs.append(qMakePair(job, task->dependson));
            }
        }
        for (QPair<QSharedPointer<Job>, QString> depedentjob : dependentjobs) {
            QSharedPointer<Job> job = depedentjob.first;
            QString dependentid = depedentjob.second;
            if (jobuuids.contains(dependentid)) {
                QStringList argumentlist = job->arguments();
                for (QString& argument : argumentlist) {
                    argument = updateTask("input", argument, joboutputs[dependentid]);
                }
                job->setArguments(argumentlist);
                job->setDependson(jobuuids[dependentid]);
                QUuid uuid = queue->submit(job);
                jobuuids[job->id()] = uuid;
                uuids.append(uuid);
            }
            else {
                QString status = QString("Status:\n"
                                         "Dependency not found for job: %1\n")
                                     .arg(job->name());
                job->setLog(status);
                job->setStatus(Job::Failed);
                return uuids;
            }
        }
    }
    return uuids;
}


QList<QUuid>
ProcessorPrivate::submit(const QSharedPointer<Preset>& preset, const Paths& paths)
{
    QList<QUuid> uuids;
    QMap<QString, QUuid> jobuuids;
    QMap<QString, QString> joboutputs;
    QList<QPair<QSharedPointer<Job>, QString>> dependentjobs;
    QFileInfo inputinfo;
    bool first = true;
    for (QSharedPointer<Task> task : preset->tasks()) {
        if (first) {
            inputinfo = QFileInfo(task->output);
        }
        QString extension = updatePaths(task->extension, "input", inputinfo);
        QString outputdir;
        if (paths.createpaths) {
            outputdir = paths.outputpath + "/" + inputinfo.fileName();
        }
        else {
            outputdir = paths.outputpath;
        }

        QString outputfile = outputdir + "/" + inputinfo.baseName() + "." + extension;
        QFileInfo outputinfo(outputfile);
        QString command = updateOptions(preset->options(), updateFiles(task->command, inputinfo, outputinfo)).join(" ");
        QString output = updateOptions(preset->options(), updateFiles(task->output, inputinfo, outputinfo)).join(" ");
        QStringList argumentlist = task->arguments.split(" ");
        QStringList replacedlist;
        for (QString& argument : argumentlist) {
            replacedlist.append(
                updateOptions(preset->options(),
                              updateTask("output", updateFiles(argument, inputinfo, outputinfo), output)));
        }
        QString startin = updateOptions(preset->options(), updateFiles(task->startin, inputinfo, outputinfo)).join(" ");

        // job
        QSharedPointer<Job> job(new Job());
        {
            job->setId(task->id);
            job->setFilename(inputinfo.fileName());
            job->setDir(outputdir);
            job->setName(task->name);
            job->setCommand(command);
            job->setArguments(replacedlist);
            job->setOutput(output);
            job->setOverwrite(paths.overwrite);
            job->setStartin(startin);
            job->setStatus(Job::Waiting);
        }
        updateEnvironment(job, paths);

        if (task->dependson.isEmpty()) {
            QUuid uuid = queue->submit(job);
            jobuuids[task->id] = uuid;
            joboutputs[task->id] = job->output();
            uuids.append(uuid);
        }
        else {
            dependentjobs.append(qMakePair(job, task->dependson));
        }
    }
    for (QPair<QSharedPointer<Job>, QString> depedentjob : dependentjobs) {
        QSharedPointer<Job> job = depedentjob.first;
        QString dependentid = depedentjob.second;
        if (jobuuids.contains(dependentid)) {
            QStringList argumentlist = job->arguments();
            for (QString& argument : argumentlist) {
                argument = updateTask("input", argument, joboutputs[dependentid]);
            }
            job->setArguments(argumentlist);
            job->setDependson(jobuuids[dependentid]);
            QUuid uuid = queue->submit(job);
            jobuuids[job->id()] = uuid;
            uuids.append(uuid);
        }
        else {
            QString status = QString("Status:\n"
                                     "Dependency not found for job: %1\n")
                                 .arg(job->name());
            job->setLog(status);
            job->setStatus(Job::Failed);
            return uuids;
        }
    }
    return uuids;
}

QString
ProcessorPrivate::updatePaths(const QString& input, const QString& pattern, const QFileInfo& fileinfo)
{
    QString result = input;
    QList<QPair<QString, QString>> replacements = { { QString("%%1dir%").arg(pattern), fileinfo.absolutePath() },
                                                    { QString("%%1file%").arg(pattern), fileinfo.absoluteFilePath() },
                                                    { QString("%%1ext%").arg(pattern), fileinfo.suffix() },
                                                    { QString("%%1base%").arg(pattern), fileinfo.baseName() } };
    for (const auto& replacement : replacements) {
        result.replace(replacement.first, replacement.second);
    }
    return result;
}

QString
ProcessorPrivate::updateFiles(const QString& input, const QFileInfo& inputinfo, const QFileInfo& outputinfo)
{
    return updatePaths(updatePaths(input, "input", inputinfo), "output", outputinfo);
}

QString
ProcessorPrivate::updateTask(const QString& input, const QString& inputinfo, const QString& outputinfo)
{
    QString result = inputinfo;
    result.replace(QString("%task:%1%").arg(input), outputinfo);
    return result;
}

QStringList
ProcessorPrivate::updateOptions(QList<QSharedPointer<Option>> options, const QString& input)
{
    QStringList result;
    bool found = false;
    for (QSharedPointer<Option> option : options) {
        QString pattern = QString("%options:%1%").arg(option->id);
        if (input.contains(pattern)) {
            if (option->enabled) {
                QString replacement = option->flag;
                if (!option->flagonly) {
                    if (replacement.length()) {
                        replacement += " ";
                    }
                    replacement += option->value.toString();
                    result.append(QString(input).replace(pattern, replacement).split(" "));
                }
                else {
                    result.append(replacement);
                }
            }
            found = true;
            break;
        }
    }
    if (!result.count() && !found) {
        result.append(input);
    }
    return result;
}

void
ProcessorPrivate::updateEnvironment(QSharedPointer<Job> job, const Paths& paths)
{
    QSettings settings(APP_IDENTIFIER, APP_NAME);
    job->os()->searchpaths = settings.value("searchpaths", paths.searchpaths).toStringList();
    QVariantList environmentvars = settings.value("environmentvars").toList();
    for (const QVariant& environmentvar : environmentvars) {
        QVariantMap environmentvarmap = environmentvar.toMap();
        if (environmentvarmap["checked"].toBool()) {
            job->os()->environmentvars.append(qMakePair(QString(environmentvarmap["name"].toString()),
                                                        QString(environmentvarmap["value"].toString())));
        }
    }
}

#include "processor.moc"

Processor::Processor(QObject* object)
    : QObject(object)
    , p(new ProcessorPrivate())
{
    p->object = this;
    p->init();
}

Processor::~Processor() {}

QList<QUuid>
Processor::submit(const QList<QString>& files, const QSharedPointer<Preset>& preset, const Paths& paths)
{
    return p->submit(files, preset, paths);
}

QList<QUuid>
Processor::submit(const QSharedPointer<Preset>& preset, const Paths& paths)
{
    return p->submit(preset, paths);
}
