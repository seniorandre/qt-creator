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

#include "qmlprojectrunconfiguration.h"
#include "qmlproject.h"
#include "qmlprojectmanagerconstants.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>

#include <projectexplorer/kitinformation.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/target.h>

#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtsupportconstants.h>

#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/mimetypes/mimedatabase.h>
#include <utils/qtcprocess.h>
#include <utils/winutils.h>

#include <qmljstools/qmljstoolsconstants.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QStandardItem>

using namespace Core;
using namespace ProjectExplorer;
using namespace QtSupport;
using namespace Utils;

namespace QmlProjectManager {

const char M_CURRENT_FILE[] = "CurrentFile";
const char CURRENT_FILE[]  = QT_TRANSLATE_NOOP("QmlManager", "<Current File>");

static bool caseInsensitiveLessThan(const QString &s1, const QString &s2)
{
    return s1.toLower() < s2.toLower();
}

// MainQmlFileAspect

class MainQmlFileAspect : public ProjectConfigurationAspect
{
public:
    explicit MainQmlFileAspect(QmlProject *project);
    ~MainQmlFileAspect() override { delete m_fileListCombo; }

    enum MainScriptSource {
        FileInEditor,
        FileInProjectFile,
        FileInSettings
    };

    void addToLayout(LayoutBuilder &builder) final;
    void toMap(QVariantMap &map) const final;
    void fromMap(const QVariantMap &map) final;

    void updateFileComboBox();
    MainScriptSource mainScriptSource() const;
    void setMainScript(int index);

    void setScriptSource(MainScriptSource source, const QString &settingsPath = QString());

    QString mainScript() const;
    void changeCurrentFile(IEditor *editor = nullptr);
    bool isQmlFilePresent();

public:
    QmlProject *m_project;
    QPointer<QComboBox> m_fileListCombo;
    QStandardItemModel m_fileListModel;
    QString m_scriptFile;
    // absolute path to current file (if being used)
    QString m_currentFileFilename;
    // absolute path to selected main script (if being used)
    QString m_mainScriptFilename;
};

MainQmlFileAspect::MainQmlFileAspect(QmlProject *project)
    : m_project(project)
    , m_scriptFile(M_CURRENT_FILE)
{
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, &MainQmlFileAspect::changeCurrentFile);
    connect(EditorManager::instance(), &EditorManager::currentDocumentStateChanged,
            this, [this] { changeCurrentFile(); });
}

void MainQmlFileAspect::addToLayout(LayoutBuilder &builder)
{
    QTC_ASSERT(!m_fileListCombo, delete m_fileListCombo);
    m_fileListCombo = new QComboBox;
    m_fileListCombo->setModel(&m_fileListModel);

    updateFileComboBox();

    connect(ProjectExplorerPlugin::instance(), &ProjectExplorerPlugin::fileListChanged,
            this, &MainQmlFileAspect::updateFileComboBox);
    connect(m_fileListCombo, QOverload<int>::of(&QComboBox::activated),
            this, &MainQmlFileAspect::setMainScript);

    builder.addItems(QmlProjectRunConfiguration::tr("Main QML file:"), m_fileListCombo.data());
}

void MainQmlFileAspect::toMap(QVariantMap &map) const
{
    map.insert(QLatin1String(Constants::QML_MAINSCRIPT_KEY), m_scriptFile);
}

void MainQmlFileAspect::fromMap(const QVariantMap &map)
{
    m_scriptFile = map.value(QLatin1String(Constants::QML_MAINSCRIPT_KEY),
                             QLatin1String(M_CURRENT_FILE)).toString();

    if (m_scriptFile == QLatin1String(M_CURRENT_FILE))
        setScriptSource(FileInEditor);
    else if (m_scriptFile.isEmpty())
        setScriptSource(FileInProjectFile);
    else
        setScriptSource(FileInSettings, m_scriptFile);
}

void MainQmlFileAspect::updateFileComboBox()
{
    QDir projectDir(m_project->projectDirectory().toString());

    if (mainScriptSource() == FileInProjectFile) {
        const QString mainScriptInFilePath = projectDir.relativeFilePath(mainScript());
        m_fileListModel.clear();
        m_fileListModel.appendRow(new QStandardItem(mainScriptInFilePath));
        if (m_fileListCombo)
            m_fileListCombo->setEnabled(false);
        return;
    }

    if (m_fileListCombo)
        m_fileListCombo->setEnabled(true);
    m_fileListModel.clear();
    m_fileListModel.appendRow(new QStandardItem(QLatin1String(CURRENT_FILE)));
    QModelIndex currentIndex;

    QStringList sortedFiles = Utils::transform(m_project->files(Project::SourceFiles),
                                               &Utils::FilePath::toString);

    // make paths relative to project directory
    QStringList relativeFiles;
    for (const QString &fn : qAsConst(sortedFiles))
        relativeFiles += projectDir.relativeFilePath(fn);
    sortedFiles = relativeFiles;

    std::stable_sort(sortedFiles.begin(), sortedFiles.end(), caseInsensitiveLessThan);

    QString mainScriptPath;
    if (mainScriptSource() != FileInEditor)
        mainScriptPath = projectDir.relativeFilePath(mainScript());

    for (const QString &fn : qAsConst(sortedFiles)) {
        QFileInfo fileInfo(fn);
        if (fileInfo.suffix() != QLatin1String("qml"))
            continue;

        auto item = new QStandardItem(fn);
        m_fileListModel.appendRow(item);

        if (mainScriptPath == fn)
            currentIndex = item->index();
    }

    if (m_fileListCombo) {
        if (currentIndex.isValid())
            m_fileListCombo->setCurrentIndex(currentIndex.row());
        else
            m_fileListCombo->setCurrentIndex(0);
    }
}

MainQmlFileAspect::MainScriptSource MainQmlFileAspect::mainScriptSource() const
{
    if (!m_project->mainFile().isEmpty())
        return FileInProjectFile;
    if (!m_mainScriptFilename.isEmpty())
        return FileInSettings;
    return FileInEditor;
}

void MainQmlFileAspect::setMainScript(int index)
{
    if (index == 0) {
        setScriptSource(FileInEditor);
    } else {
        const QString path = m_fileListModel.data(m_fileListModel.index(index, 0)).toString();
        setScriptSource(FileInSettings, path);
    }
}

void MainQmlFileAspect::setScriptSource(MainScriptSource source, const QString &settingsPath)
{
    if (source == FileInEditor) {
        m_scriptFile = QLatin1String(M_CURRENT_FILE);
        m_mainScriptFilename.clear();
    } else if (source == FileInProjectFile) {
        m_scriptFile.clear();
        m_mainScriptFilename.clear();
    } else { // FileInSettings
        m_scriptFile = settingsPath;
        m_mainScriptFilename = m_project->projectDirectory().toString() + '/' + m_scriptFile;
    }

    emit changed();
    updateFileComboBox();
}

/**
  Returns absolute path to main script file.
  */
QString MainQmlFileAspect::mainScript() const
{
    if (!m_project->mainFile().isEmpty()) {
        const QString pathInProject = m_project->mainFile();
        if (QFileInfo(pathInProject).isAbsolute())
            return pathInProject;
        else
            return QDir(m_project->canonicalProjectDir().toString()).absoluteFilePath(pathInProject);
    }

    if (!m_mainScriptFilename.isEmpty())
        return m_mainScriptFilename;

    return m_currentFileFilename;
}

void MainQmlFileAspect::changeCurrentFile(IEditor *editor)
{
    if (!editor)
        editor = EditorManager::currentEditor();

    if (editor)
        m_currentFileFilename = editor->document()->filePath().toString();

    emit changed();
}


// QmlProjectRunConfiguration

QmlProjectRunConfiguration::QmlProjectRunConfiguration(Target *target, Id id)
    : RunConfiguration(target, id)
{
    auto envAspect = addAspect<EnvironmentAspect>();

    auto envModifier = [target](Environment env) {
        if (auto project = qobject_cast<const QmlProject *>(target->project()))
            env.modify(project->environment());
        return env;
    };

    const Id deviceTypeId = DeviceTypeKitAspect::deviceTypeId(target->kit());
    if (deviceTypeId == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE) {
        envAspect->addPreferredBaseEnvironment(tr("System Environment"), [envModifier] {
            return envModifier(Environment::systemEnvironment());
        });
    }

    envAspect->addSupportedBaseEnvironment(tr("Clean Environment"), [envModifier] {
        return envModifier(Environment());
    });

    m_qmlViewerAspect = addAspect<BaseStringAspect>();
    m_qmlViewerAspect->setLabelText(tr("QML Viewer:"));
    m_qmlViewerAspect->setPlaceHolderText(commandLine().executable().toString());
    m_qmlViewerAspect->setDisplayStyle(BaseStringAspect::LineEditDisplay);
    m_qmlViewerAspect->setHistoryCompleter("QmlProjectManager.viewer.history");

    auto argumentAspect = addAspect<ArgumentsAspect>();
    argumentAspect->setSettingsKey(Constants::QML_VIEWER_ARGUMENTS_KEY);

    setCommandLineGetter([this] {
        return CommandLine(FilePath::fromString(theExecutable()),
                           commandLineArguments(),
                           CommandLine::Raw);
    });

    auto qmlProject = qobject_cast<QmlProject *>(target->project());
    QTC_ASSERT(qmlProject, return);
    m_mainQmlFileAspect = addAspect<MainQmlFileAspect>(qmlProject);
    connect(m_mainQmlFileAspect, &MainQmlFileAspect::changed,
            this, &QmlProjectRunConfiguration::updateEnabledState);

    connect(target, &Target::kitChanged,
            this, &QmlProjectRunConfiguration::updateEnabledState);

    setDisplayName(tr("QML Scene", "QMLRunConfiguration display name."));
    updateEnabledState();
}

Runnable QmlProjectRunConfiguration::runnable() const
{
    Runnable r;
    r.setCommandLine(commandLine());
    r.environment = aspect<EnvironmentAspect>()->environment();
    r.workingDirectory = static_cast<QmlProject *>(project())->targetDirectory(target()).toString();
    return r;
}

QString QmlProjectRunConfiguration::disabledReason() const
{
    if (mainScript().isEmpty())
        return tr("No script file to execute.");
    if (DeviceTypeKitAspect::deviceTypeId(target()->kit())
            == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE
            && !commandLine().executable().exists()) {
        return tr("No qmlscene found.");
    }
    if (commandLine().executable().isEmpty())
        return tr("No qmlscene binary specified for target device.");
    return RunConfiguration::disabledReason();
}

QString QmlProjectRunConfiguration::theExecutable() const
{
    const QString qmlViewer = m_qmlViewerAspect->value();
    if (!qmlViewer.isEmpty())
        return qmlViewer;

    BaseQtVersion *version = QtKitAspect::qtVersion(target()->kit());
    if (!version) // No Qt version in Kit. Don't try to run qmlscene.
        return QString();

    const Id deviceType = DeviceTypeKitAspect::deviceTypeId(target()->kit());
    if (deviceType == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE) {
        // If not given explicitly by Qt Version, try to pick it from $PATH.
        return version->type() == QtSupport::Constants::DESKTOPQT
                ? version->qmlsceneCommand() : QString("qmlscene");
    }

    IDevice::ConstPtr dev = DeviceKitAspect::device(target()->kit());
    if (dev.isNull()) // No device set. We don't know where to run qmlscene.
        return QString();

    const QString qmlscene = dev->qmlsceneCommand();
    // If not given explicitly by device, try to pick it from $PATH.
    return qmlscene.isEmpty() ? QString("qmlscene") : qmlscene;
}

QString QmlProjectRunConfiguration::commandLineArguments() const
{
    // arguments in .user file
    QString args = aspect<ArgumentsAspect>()->arguments(macroExpander());
    const Target *currentTarget = target();
    const IDevice::ConstPtr device = DeviceKitAspect::device(currentTarget->kit());
    const Utils::OsType osType = device ? device->osType() : Utils::HostOsInfo::hostOs();

    // arguments from .qmlproject file
    const QmlProject *project = static_cast<QmlProject *>(currentTarget->project());
    foreach (const QString &importPath,
             QmlProject::makeAbsolute(project->targetDirectory(currentTarget), project->customImportPaths())) {
        Utils::QtcProcess::addArg(&args, QLatin1String("-I"), osType);
        Utils::QtcProcess::addArg(&args, importPath, osType);
    }

    for (const QString &fileSelector : project->customFileSelectors()) {
        Utils::QtcProcess::addArg(&args, QLatin1String("-S"), osType);
        Utils::QtcProcess::addArg(&args, fileSelector, osType);
    }

    const QString main = project->targetFile(Utils::FilePath::fromString(mainScript()),
                                             currentTarget).toString();
    if (!main.isEmpty())
        Utils::QtcProcess::addArg(&args, main, osType);
    return args;
}

void QmlProjectRunConfiguration::updateEnabledState()
{
    bool enabled = false;
    if (m_mainQmlFileAspect->isQmlFilePresent() && !commandLine().executable().isEmpty()) {
        Project *p = target()->project();
        enabled = !p->isParsing() && p->hasParsingData();
    }
    setEnabled(enabled);
}

bool MainQmlFileAspect::isQmlFilePresent()
{
    bool qmlFileFound = false;
    if (mainScriptSource() == FileInEditor) {
        IDocument *document = EditorManager::currentDocument();
        Utils::MimeType mainScriptMimeType = Utils::mimeTypeForFile(mainScript());
        if (document) {
            m_currentFileFilename = document->filePath().toString();
            if (mainScriptMimeType.matchesName(
                        QLatin1String(ProjectExplorer::Constants::QML_MIMETYPE))
                    || mainScriptMimeType.matchesName(
                        QLatin1String(ProjectExplorer::Constants::QMLUI_MIMETYPE))) {
                qmlFileFound = true;
            }
        }
        if (!document
                || mainScriptMimeType.matchesName(QLatin1String(QmlJSTools::Constants::QMLPROJECT_MIMETYPE))) {
            // find a qml file with lowercase filename. This is slow, but only done
            // in initialization/other border cases.
            const auto files = m_project->files(Project::SourceFiles);
            for (const Utils::FilePath &filename : files) {
                const QFileInfo fi = filename.toFileInfo();

                if (!filename.isEmpty() && fi.baseName().at(0).isLower()) {
                    Utils::MimeType type = Utils::mimeTypeForFile(fi);
                    if (type.matchesName(QLatin1String(ProjectExplorer::Constants::QML_MIMETYPE))
                            || type.matchesName(
                                QLatin1String(ProjectExplorer::Constants::QMLUI_MIMETYPE))) {
                        m_currentFileFilename = filename.toString();
                        qmlFileFound = true;
                        break;
                    }
                }
            }
        }
    } else { // use default one
        qmlFileFound = !mainScript().isEmpty();
    }
    return qmlFileFound;
}

QString QmlProjectRunConfiguration::mainScript() const
{
    return m_mainQmlFileAspect->mainScript();
}

namespace Internal {

QmlProjectRunConfigurationFactory::QmlProjectRunConfigurationFactory()
    : FixedRunConfigurationFactory(QmlProjectRunConfiguration::tr("QML Scene"), false)
{
    registerRunConfiguration<QmlProjectRunConfiguration>
            ("QmlProjectManager.QmlRunConfiguration.QmlScene");
    addSupportedProjectType(QmlProjectManager::Constants::QML_PROJECT_ID);
}

} // namespace Internal
} // namespace QmlProjectManager
