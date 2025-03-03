/****************************************************************************
**
** Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
** Contact: http://www.qt.io/licensing
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "nimblebuildsystem.h"
#include "nimbleproject.h"

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QProcess>
#include <QStandardPaths>

using namespace Nim;
using namespace ProjectExplorer;
using namespace Utils;

namespace {

std::vector<NimbleTask> parseTasks(const QString &nimblePath, const QString &workingDirectory)
{
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.start(QStandardPaths::findExecutable(nimblePath), {"tasks"});
    process.waitForFinished();

    std::vector<NimbleTask> result;

    QList<QByteArray> lines = process.readAllStandardOutput().split('\n');
    lines = Utils::transform(lines, [](const QByteArray &line){ return line.trimmed(); });
    Utils::erase(lines, [](const QByteArray &line) { return line.isEmpty(); });

    for (const QByteArray &line : lines) {
        QList<QByteArray> tokens = line.trimmed().split(' ');
        QTC_ASSERT(!tokens.empty(), continue);
        QString taskName = QString::fromUtf8(tokens.takeFirst());
        QString taskDesc = QString::fromUtf8(tokens.join(' '));
        result.push_back({std::move(taskName), std::move(taskDesc)});
    }

    return result;
}

NimbleMetadata parseMetadata(const QString &nimblePath, const QString &workingDirectory) {
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.start(QStandardPaths::findExecutable(nimblePath), {"dump"});
    process.waitForFinished();

    NimbleMetadata result = {};

    QList<QByteArray> lines = process.readAllStandardOutput().split('\n');
    lines = Utils::transform(lines, [](const QByteArray &line){ return line.trimmed(); });
    Utils::erase(lines, [](const QByteArray &line) { return line.isEmpty(); });

    for (const QByteArray &line : lines) {
        QList<QByteArray> tokens = line.trimmed().split(':');
        QTC_ASSERT(tokens.size() == 2, continue);
        QString name = QString::fromUtf8(tokens.takeFirst()).trimmed();
        QString value = QString::fromUtf8(tokens.takeFirst()).trimmed();
        QTC_ASSERT(value.size() >= 2, continue);
        QTC_ASSERT(value.front() == QChar('"'), continue);
        QTC_ASSERT(value.back() == QChar('"'), continue);
        value.remove(0, 1);
        value.remove(value.size() - 1, 1);

        if (name == "binDir")
            result.binDir = value;
        else if (name == "srcDir")
            result.srcDir = value;
        else if (name == "bin") {
            QStringList bin = value.split(',');
            bin = Utils::transform(bin, [](const QString &x){ return x.trimmed(); });
            Utils::erase(bin, [](const QString &x) { return x.isEmpty(); });
            result.bin = std::move(bin);
        }
    }

    return result;
}

}

NimbleBuildSystem::NimbleBuildSystem(Project *project)
    : NimBuildSystem(project)
{
    // Not called in parseProject due to nimble behavior to create temporary
    // files in project directory. This creation in turn stimulate the fs watcher
    // that in turn causes project parsing (thus a loop if invoke in parseProject).
    // For this reason we call this function manually during project creation
    // See https://github.com/nim-lang/nimble/issues/720
    m_directoryWatcher.addFile(this->project()->projectFilePath().toString(),
                               FileSystemWatcher::WatchModifiedDate);
    connect(&m_directoryWatcher, &FileSystemWatcher::fileChanged, this, [this](const QString &path) {
        if (path == this->project()->projectFilePath().toString()) {
            updateProject();
        }
    });
    updateProject();
}

void NimbleBuildSystem::parseProject(BuildSystem::ParsingContext &&ctx)
{
    NimBuildSystem::parseProject(std::move(ctx));
}

void NimbleBuildSystem::updateProject()
{
    updateProjectMetaData();
    updateProjectTasks();
}

void NimbleBuildSystem::updateProjectTasks()
{
    auto prj = dynamic_cast<NimbleProject*>(project());
    QTC_ASSERT(prj, return);
    prj->setTasks(parseTasks(QStandardPaths::findExecutable("nimble"), project()->projectDirectory().toString()));
}

void NimbleBuildSystem::updateProjectMetaData()
{
    auto prj = dynamic_cast<NimbleProject*>(project());
    QTC_ASSERT(prj, return);
    prj->setMetadata(parseMetadata(QStandardPaths::findExecutable("nimble"), project()->projectDirectory().toString()));
}
