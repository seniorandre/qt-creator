/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
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

#pragma once

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/projectnodes.h>

#include <qbs.h>

namespace QbsProjectManager {
namespace Internal {

class QbsNodeTreeBuilder;
class QbsProject;

class QbsBuildSystem : public ProjectExplorer::BuildSystem
{
public:
    explicit QbsBuildSystem(ProjectExplorer::Project *project);

    bool supportsAction(ProjectExplorer::Node *context,
                        ProjectExplorer::ProjectAction action,
                        const ProjectExplorer::Node *node) const final;
    bool addFiles(ProjectExplorer::Node *context,
                  const QStringList &filePaths,
                  QStringList *notAdded = nullptr) override;
    ProjectExplorer::RemovedFilesFromProject removeFiles(ProjectExplorer::Node *context,
                                                         const QStringList &filePaths,
                                                         QStringList *notRemoved = nullptr) override;
    bool renameFile(ProjectExplorer::Node *context,
                    const QString &filePath, const QString &newFilePath) override;

    QbsProject *project() const;
};

// --------------------------------------------------------------------
// QbsGroupNode:
// --------------------------------------------------------------------

class QbsGroupNode : public ProjectExplorer::ProjectNode
{
public:
    QbsGroupNode(const qbs::GroupData &grp, const QString &productPath);

    bool showInSimpleTree() const final { return false; }

private:
    friend class QbsBuildSystem;
    AddNewInformation addNewInformation(const QStringList &files, Node *context) const override;
    QVariant data(Core::Id role) const override;

    qbs::GroupData m_qbsGroupData;
    QString m_productPath;
};

// --------------------------------------------------------------------
// QbsProductNode:
// --------------------------------------------------------------------

class QbsProductNode : public ProjectExplorer::ProjectNode
{
public:
    explicit QbsProductNode(const qbs::ProductData &prd);

    void build() override;
    QStringList targetApplications() const override;

    QString buildKey() const override;

    const qbs::ProductData qbsProductData() const { return m_qbsProductData; }
    QVariant data(Core::Id role) const override;

private:
    const qbs::ProductData m_qbsProductData;
};

// ---------------------------------------------------------------------------
// QbsProjectNode:
// ---------------------------------------------------------------------------

class QbsProjectNode : public ProjectExplorer::ProjectNode
{
public:
    explicit QbsProjectNode(const Utils::FilePath &projectDirectory);

    virtual QbsProject *project() const;
    const qbs::Project qbsProject() const;
    const qbs::ProjectData qbsProjectData() const { return m_projectData; }

    void setProjectData(const qbs::ProjectData &data); // FIXME: Needed?

private:
    qbs::ProjectData m_projectData;

    friend class QbsNodeTreeBuilder;
};

// --------------------------------------------------------------------
// QbsRootProjectNode:
// --------------------------------------------------------------------

class QbsRootProjectNode : public QbsProjectNode
{
public:
    explicit QbsRootProjectNode(QbsProject *project);

    QbsProject *project() const  override { return m_project; }

private:
    QbsProject *const m_project;
};


} // namespace Internal
} // namespace QbsProjectManager
