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

#include "qbsnodes.h"

#include "qbsnodetreebuilder.h"
#include "qbsproject.h"
#include "qbsprojectmanagerconstants.h"
#include "qbsprojectmanagerplugin.h"

#include <android/androidconstants.h>
#include <coreplugin/fileiconprovider.h>
#include <coreplugin/idocument.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>
#include <qtsupport/qtsupportconstants.h>
#include <resourceeditor/resourcenode.h>
#include <utils/algorithm.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>

#include <QtDebug>
#include <QDir>
#include <QIcon>

using namespace ProjectExplorer;

// ----------------------------------------------------------------------
// Helpers:
// ----------------------------------------------------------------------

namespace QbsProjectManager {
namespace Internal {

static const QbsProjectNode *parentQbsProjectNode(const ProjectExplorer::Node *node)
{
    for (const ProjectExplorer::FolderNode *pn = node->managingProject(); pn; pn = pn->parentProjectNode()) {
        const auto prjNode = dynamic_cast<const QbsProjectNode *>(pn);
        if (prjNode)
            return prjNode;
    }
    return nullptr;
}

static const QbsProductNode *parentQbsProductNode(const ProjectExplorer::Node *node)
{
    for (; node; node = node->parentFolderNode()) {
        const auto prdNode = dynamic_cast<const QbsProductNode *>(node);
        if (prdNode)
            return prdNode;
    }
    return nullptr;
}

static qbs::GroupData findMainQbsGroup(const qbs::ProductData &productData)
{
    foreach (const qbs::GroupData &grp, productData.groups()) {
        if (grp.name() == productData.name() && grp.location() == productData.location())
            return grp;
    }
    return qbs::GroupData();
}

class FileTreeNode {
public:
    explicit FileTreeNode(const QString &n = QString(), FileTreeNode *p = nullptr, bool f = false) :
        parent(p), name(n), m_isFile(f)
    {
        if (p)
            p->children.append(this);
    }

    ~FileTreeNode()
    {
        qDeleteAll(children);
    }

    FileTreeNode *addPart(const QString &n, bool isFile)
    {
        foreach (FileTreeNode *c, children) {
            if (c->name == n)
                return c;
        }
        return new FileTreeNode(n, this, isFile);
    }

    bool isFile() const { return m_isFile; }

    static FileTreeNode *moveChildrenUp(FileTreeNode *node)
    {
        QTC_ASSERT(node, return nullptr);

        FileTreeNode *newParent = node->parent;
        if (!newParent)
            return nullptr;

        // disconnect node and parent:
        node->parent = nullptr;
        newParent->children.removeOne(node);

        foreach (FileTreeNode *c, node->children) {
            // update path, make sure there will be no / before "C:" on windows:
            if (Utils::HostOsInfo::isWindowsHost() && node->name.isEmpty())
                c->name = node->name;
            else
                c->name = node->name + QLatin1Char('/') + c->name;

            newParent->children.append(c);
            c->parent = newParent;
        }

        // Delete node
        node->children.clear();
        delete node;
        return newParent;
    }

    // Moves the children of the node pointing to basedir to the root of the tree.
    static void reorder(FileTreeNode *node, const QString &basedir)
    {
        QTC_CHECK(!basedir.isEmpty());
        QString prefix = basedir;
        if (basedir.startsWith(QLatin1Char('/')))
            prefix = basedir.mid(1);
        prefix.append(QLatin1Char('/'));

        if (node->path() == basedir) {
            // Find root node:
            FileTreeNode *root = node;
            while (root->parent)
                root = root->parent;

            foreach (FileTreeNode *c, node->children) {
                // Update children names by prepending basedir
                c->name = prefix + c->name;
                // Update parent information:
                c->parent = root;

                root->children.append(c);
            }

            // Clean up node:
            node->children.clear();
            node->parent->children.removeOne(node);
            node->parent = nullptr;
            delete node;

            return;
        }

        foreach (FileTreeNode *n, node->children)
            reorder(n, basedir);
    }

    static void simplify(FileTreeNode *node)
    {
        foreach (FileTreeNode *c, node->children)
            simplify(c);

        if (!node->parent)
            return;

        if (node->children.isEmpty() && !node->isFile()) {
            // Clean up empty folder nodes:
            node->parent->children.removeOne(node);
            node->parent = nullptr;
            delete node;
        } else if (node->children.count() == 1 && !node->children.at(0)->isFile()) {
            // Compact folder nodes with one child only:
            moveChildrenUp(node);
        }
    }

    QString path() const
    {
        QString p = name;
        FileTreeNode *node = parent;
        while (node) {
            if (!Utils::HostOsInfo::isWindowsHost() || !node->name.isEmpty())
                p = node->name + QLatin1Char('/') + p;
            node = node->parent;
        }
        return p;
    }

    QList<FileTreeNode *> children;
    FileTreeNode *parent;
    QString name;
    bool m_isFile;
};


static bool supportsNodeAction(ProjectAction action, const Node *node)
{
    const QbsProject * const project = parentQbsProjectNode(node)->project();
    if (!project->isProjectEditable())
        return false;
    if (action == RemoveFile || action == Rename)
        return node->asFileNode();
    return false;
}

// --------------------------------------------------------------------
// QbsGroupNode:
// --------------------------------------------------------------------

QbsGroupNode::QbsGroupNode(const qbs::GroupData &grp, const QString &productPath) :
    ProjectNode(Utils::FilePath())
{
    static QIcon groupIcon = QIcon(QString(Constants::QBS_GROUP_ICON));
    setIcon(groupIcon);

    m_productPath = productPath;
    m_qbsGroupData = grp;
}

FolderNode::AddNewInformation QbsGroupNode::addNewInformation(const QStringList &files,
                                                              Node *context) const
{
    AddNewInformation info = ProjectNode::addNewInformation(files, context);
    if (context != this)
        --info.priority;
    return info;
}

QVariant QbsGroupNode::data(Core::Id role) const
{
    if (role == ProjectExplorer::Constants::QT_KEYWORDS_ENABLED)
        return m_qbsGroupData.properties().getModuleProperty("Qt.core", "enableKeywords");
    return QVariant();
}

// --------------------------------------------------------------------
// QbsProductNode:
// --------------------------------------------------------------------

QbsProductNode::QbsProductNode(const qbs::ProductData &prd) :
    ProjectNode(Utils::FilePath::fromString(prd.location().filePath())),
    m_qbsProductData(prd)
{
    static QIcon productIcon = Core::FileIconProvider::directoryIcon(Constants::QBS_PRODUCT_OVERLAY_ICON);
    setIcon(productIcon);
    if (m_qbsProductData.isRunnable()) {
        setProductType(ProductType::App);
    } else if (m_qbsProductData.type().contains("dynamiclibrary")
               || m_qbsProductData.type().contains("staticlibrary")) {
        setProductType(ProductType::Lib);
    } else {
        setProductType(ProductType::Other);
    }
}

void QbsProductNode::build()
{
    QbsProjectManagerPlugin::buildNamedProduct(static_cast<QbsProject *>(getProject()),
                                               QbsProject::uniqueProductName(qbsProductData()));
}

QStringList QbsProductNode::targetApplications() const
{
    return QStringList{m_qbsProductData.targetExecutable()};
}

QString QbsProductNode::buildKey() const
{
    return QbsProject::uniqueProductName(m_qbsProductData);
}

QVariant QbsProductNode::data(Core::Id role) const
{
    if (role == Android::Constants::AndroidDeploySettingsFile) {
        for (const auto &artifact : m_qbsProductData.generatedArtifacts()) {
            if (artifact.fileTags().contains("qt_androiddeployqt_input"))
                return artifact.filePath();
        }
        return {};
    }

    if (role == Android::Constants::AndroidSoLibPath) {
        QStringList ret{m_qbsProductData.buildDirectory()};
        for (const auto &artifact : m_qbsProductData.generatedArtifacts()) {
            if (artifact.fileTags().contains("dynamiclibrary")) {
                ret << QFileInfo(artifact.filePath()).path();
            }
        }
        ret.removeDuplicates();
        return ret;
    }

    if (role == Android::Constants::AndroidManifest) {
        for (const auto &artifact : m_qbsProductData.generatedArtifacts()) {
            if (artifact.fileTags().contains("android.manifest_final"))
                return artifact.filePath();
        }
        return {};
    }

    if (role == Android::Constants::AndroidApk)
        return m_qbsProductData.targetExecutable();

    if (role == ProjectExplorer::Constants::QT_KEYWORDS_ENABLED)
        return m_qbsProductData.moduleProperties().getModuleProperty("Qt.core", "enableKeywords");

    return {};
}

// --------------------------------------------------------------------
// QbsProjectNode:
// --------------------------------------------------------------------

QbsProjectNode::QbsProjectNode(const Utils::FilePath &projectDirectory) :
    ProjectNode(projectDirectory)
{
    static QIcon projectIcon = Core::FileIconProvider::directoryIcon(ProjectExplorer::Constants::FILEOVERLAY_QT);
    setIcon(projectIcon);
}

QbsProject *QbsProjectNode::project() const
{
    return static_cast<QbsProjectNode *>(parentFolderNode())->project();
}

const qbs::Project QbsProjectNode::qbsProject() const
{
    return project()->qbsProject();
}

void QbsProjectNode::setProjectData(const qbs::ProjectData &data)
{
    m_projectData = data;
}

// --------------------------------------------------------------------
// QbsRootProjectNode:
// --------------------------------------------------------------------

QbsRootProjectNode::QbsRootProjectNode(QbsProject *project) :
    QbsProjectNode(project->projectDirectory()),
    m_project(project)
{ }

// --------------------------------------------------------------------
// QbsBuildSystem:
// --------------------------------------------------------------------

QbsBuildSystem::QbsBuildSystem(Project *project)
    : BuildSystem(project)
{
}

bool QbsBuildSystem::supportsAction(Node *context, ProjectAction action, const Node *node) const
{
    if (dynamic_cast<QbsGroupNode *>(context)) {
        if (action == AddNewFile || action == AddExistingFile)
            return true;
    }

    if (dynamic_cast<QbsProductNode *>(context)) {
        if (action == AddNewFile || action == AddExistingFile)
            return true;
    }

    return supportsNodeAction(action, node);
}

bool QbsBuildSystem::addFiles(Node *context, const QStringList &filePaths, QStringList *notAdded)
{
    if (auto n = dynamic_cast<QbsGroupNode *>(context)) {
        QStringList notAddedDummy;
        if (!notAdded)
            notAdded = &notAddedDummy;

        const QbsProjectNode *prjNode = parentQbsProjectNode(n);
        if (!prjNode || !prjNode->qbsProject().isValid()) {
            *notAdded += filePaths;
            return false;
        }

        const QbsProductNode *prdNode = parentQbsProductNode(n);
        if (!prdNode || !prdNode->qbsProductData().isValid()) {
            *notAdded += filePaths;
            return false;
        }

        return prjNode->project()->addFilesToProduct(filePaths, prdNode->qbsProductData(),
                                                     n->m_qbsGroupData, notAdded);
    }

    if (auto n = dynamic_cast<QbsProductNode *>(context)) {
        QStringList notAddedDummy;
        if (!notAdded)
            notAdded = &notAddedDummy;

        const QbsProjectNode *prjNode = parentQbsProjectNode(n);
        if (!prjNode || !prjNode->qbsProject().isValid()) {
            *notAdded += filePaths;
            return false;
        }

        qbs::GroupData grp = findMainQbsGroup(n->qbsProductData());
        if (grp.isValid())
            return prjNode->project()->addFilesToProduct(filePaths, n->qbsProductData(), grp, notAdded);

        QTC_ASSERT(false, return false);
    }

    return BuildSystem::addFiles(context, filePaths, notAdded);
}

RemovedFilesFromProject QbsBuildSystem::removeFiles(Node *context, const QStringList &filePaths,
                                                    QStringList *notRemoved)
{
    if (auto n = dynamic_cast<QbsGroupNode *>(context)) {
        QStringList notRemovedDummy;
        if (!notRemoved)
            notRemoved = &notRemovedDummy;

        const QbsProjectNode *prjNode = parentQbsProjectNode(n);
        if (!prjNode || !prjNode->qbsProject().isValid()) {
            *notRemoved += filePaths;
            return RemovedFilesFromProject::Error;
        }

        const QbsProductNode *prdNode = parentQbsProductNode(n);
        if (!prdNode || !prdNode->qbsProductData().isValid()) {
            *notRemoved += filePaths;
            return RemovedFilesFromProject::Error;
        }

        return project()->removeFilesFromProduct(filePaths, prdNode->qbsProductData(),
                                                 n->m_qbsGroupData, notRemoved);
    }

    if (auto n = dynamic_cast<QbsProductNode *>(context)) {
        QStringList notRemovedDummy;
        if (!notRemoved)
            notRemoved = &notRemovedDummy;

        const QbsProjectNode *prjNode = parentQbsProjectNode(n);
        if (!prjNode || !prjNode->qbsProject().isValid()) {
            *notRemoved += filePaths;
            return RemovedFilesFromProject::Error;
        }

        qbs::GroupData grp = findMainQbsGroup(n->qbsProductData());
        if (grp.isValid()) {
            return prjNode->project()->removeFilesFromProduct(filePaths, n->qbsProductData(), grp,
                                                              notRemoved);
        }

        QTC_ASSERT(false, return RemovedFilesFromProject::Error);
    }

    return BuildSystem::removeFiles(context, filePaths, notRemoved);
}

bool QbsBuildSystem::renameFile(Node *context, const QString &filePath, const QString &newFilePath)
{
    if (auto *n = dynamic_cast<QbsGroupNode *>(context)) {
        const QbsProjectNode *prjNode = parentQbsProjectNode(n);
        if (!prjNode || !prjNode->qbsProject().isValid())
            return false;
        const QbsProductNode *prdNode = parentQbsProductNode(n);
        if (!prdNode || !prdNode->qbsProductData().isValid())
            return false;

        return project()->renameFileInProduct(filePath, newFilePath,
                                              prdNode->qbsProductData(), n->m_qbsGroupData);
    }

    if (auto *n = dynamic_cast<QbsProductNode *>(context)) {
        const QbsProjectNode * prjNode = parentQbsProjectNode(n);
        if (!prjNode || !prjNode->qbsProject().isValid())
            return false;
        const qbs::GroupData grp = findMainQbsGroup(n->qbsProductData());
        QTC_ASSERT(grp.isValid(), return false);
        return prjNode->project()->renameFileInProduct(filePath, newFilePath, n->qbsProductData(), grp);
    }

    return BuildSystem::renameFile(context, filePath, newFilePath);
}

QbsProject *QbsBuildSystem::project() const
{
    return static_cast<QbsProject *>(BuildSystem::project());
}

} // namespace Internal
} // namespace QbsProjectManager
