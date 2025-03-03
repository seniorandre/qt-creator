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

#include "clangtoolruncontrol.h"

#include "clangtidyclazyrunner.h"
#include "clangtool.h"
#include "clangtoolslogfilereader.h"
#include "clangtoolsprojectsettings.h"
#include "clangtoolssettings.h"
#include "clangtoolsutils.h"

#include <debugger/analyzer/analyzerconstants.h>

#include <clangcodemodel/clangutils.h>

#include <coreplugin/icore.h>
#include <coreplugin/progressmanager/futureprogress.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <cpptools/clangdiagnosticconfigsmodel.h>
#include <cpptools/compileroptionsbuilder.h>
#include <cpptools/cppmodelmanager.h>
#include <cpptools/cppprojectfile.h>
#include <cpptools/cpptoolsreuse.h>
#include <cpptools/projectinfo.h>

#include <projectexplorer/abi.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorericons.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/target.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/toolchain.h>

#include <utils/algorithm.h>
#include <utils/checkablemessagebox.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>

#include <QAction>
#include <QLoggingCategory>

using namespace CppTools;
using namespace ProjectExplorer;
using namespace Utils;

static Q_LOGGING_CATEGORY(LOG, "qtc.clangtools.runcontrol", QtWarningMsg)

static QStringList splitArgs(QString &argsString)
{
    QStringList result;
    Utils::QtcProcess::ArgIterator it(&argsString);
    while (it.next())
        result.append(it.value());
    return result;
}

static QStringList extraOptions(const char *environment)
{
    if (!qEnvironmentVariableIsSet(environment))
        return QStringList();
    QString arguments = QString::fromLocal8Bit(qgetenv(environment));
    return splitArgs(arguments);
}

static QStringList extraClangToolsPrependOptions()
{
    constexpr char csaPrependOptions[] = "QTC_CLANG_CSA_CMD_PREPEND";
    constexpr char toolsPrependOptions[] = "QTC_CLANG_TOOLS_CMD_PREPEND";
    static const QStringList options = extraOptions(csaPrependOptions)
            + extraOptions(toolsPrependOptions);
    if (!options.isEmpty())
        qWarning() << "ClangTools options are prepended with " << options.toVector();
    return options;
}

static QStringList extraClangToolsAppendOptions()
{
    constexpr char csaAppendOptions[] = "QTC_CLANG_CSA_CMD_APPEND";
    constexpr char toolsAppendOptions[] = "QTC_CLANG_TOOLS_CMD_APPEND";
    static const QStringList options = extraOptions(csaAppendOptions)
            + extraOptions(toolsAppendOptions);
    if (!options.isEmpty())
        qWarning() << "ClangTools options are appended with " << options.toVector();
    return options;
}

namespace ClangTools {
namespace Internal {

static ClangTool *tool()
{
    return ClangTool::instance();
}

class ProjectBuilder : public RunWorker
{
public:
    ProjectBuilder(RunControl *runControl)
        : RunWorker(runControl)
    {
        setId("ProjectBuilder");
    }

    bool success() const { return m_success; }

private:
    void start() final
    {
        Target *target = runControl()->target();
        QTC_ASSERT(target, reportFailure(); return);

        if (runControl()->buildType() == BuildConfiguration::Release) {
            const QString wrongMode = ClangToolRunWorker::tr("Release");
            const QString toolName = tool()->name();
            const QString title = ClangToolRunWorker::tr("Run %1 in %2 Mode?").arg(toolName, wrongMode);
            const QString problem = ClangToolRunWorker::tr(
                        "You are trying to run the tool \"%1\" on an application in %2 mode. The tool is "
                        "designed to be used in Debug mode since enabled assertions can reduce the number of "
                        "false positives.").arg(toolName, wrongMode);
            const QString question = ClangToolRunWorker::tr(
                        "Do you want to continue and run the tool in %1 mode?").arg(wrongMode);
            const QString message = QString("<html><head/><body>"
                                            "<p>%1</p>"
                                            "<p>%2</p>"
                                            "</body></html>").arg(problem, question);
            if (Utils::CheckableMessageBox::doNotAskAgainQuestion(Core::ICore::mainWindow(),
                                                                  title, message, Core::ICore::settings(),
                                                                  "ClangToolsCorrectModeWarning") != QDialogButtonBox::Yes)
            {
                reportFailure();
                return;
            }
        }

        connect(BuildManager::instance(), &BuildManager::buildQueueFinished,
                this, &ProjectBuilder::onBuildFinished, Qt::QueuedConnection);

        ProjectExplorerPlugin::buildProject(target->project());
     }

     void onBuildFinished(bool success)
     {
         disconnect(BuildManager::instance(), &BuildManager::buildQueueFinished,
                    this, &ProjectBuilder::onBuildFinished);
         m_success = success;
         reportDone();
     }

private:
     bool m_success = false;
};

static AnalyzeUnits toAnalyzeUnits(const FileInfos &fileInfos)
{
    AnalyzeUnits unitsToAnalyze;
    const UsePrecompiledHeaders usePrecompiledHeaders = CppTools::getPchUsage();
    for (const FileInfo &fileInfo : fileInfos) {
        CompilerOptionsBuilder optionsBuilder(*fileInfo.projectPart,
                                              UseSystemHeader::No,
                                              UseTweakedHeaderPaths::Yes,
                                              UseLanguageDefines::No,
                                              UseBuildSystemWarnings::No,
                                              QString(CLANG_VERSION),
                                              QString(CLANG_RESOURCE_DIR));
        QStringList arguments = extraClangToolsPrependOptions();
        arguments.append(optionsBuilder.build(fileInfo.kind, usePrecompiledHeaders));
        arguments.append(extraClangToolsAppendOptions());
        unitsToAnalyze << AnalyzeUnit(fileInfo.file.toString(), arguments);
    }

    return unitsToAnalyze;
}

AnalyzeUnits ClangToolRunWorker::unitsToAnalyze()
{
    QTC_ASSERT(m_projectInfo.isValid(), return AnalyzeUnits());

    return toAnalyzeUnits(m_fileInfos);
}

static QDebug operator<<(QDebug debug, const Utils::Environment &environment)
{
    for (const QString &entry : environment.toStringList())
        debug << "\n  " << entry;
    return debug;
}

static QDebug operator<<(QDebug debug, const AnalyzeUnits &analyzeUnits)
{
    for (const AnalyzeUnit &unit : analyzeUnits)
        debug << "\n  " << unit.file;
    return debug;
}


ClangToolRunWorker::ClangToolRunWorker(RunControl *runControl,
                                       const RunSettings &runSettings,
                                       const CppTools::ClangDiagnosticConfig &diagnosticConfig,
                                       const FileInfos &fileInfos,
                                       bool preventBuild)
    : RunWorker(runControl)
    , m_runSettings(runSettings)
    , m_diagnosticConfig(diagnosticConfig)
    , m_fileInfos(fileInfos)
    , m_temporaryDir("clangtools-XXXXXX")
{
    setId("ClangTidyClazyRunner");
    setSupportsReRunning(false);

    if (!preventBuild && runSettings.buildBeforeAnalysis()) {
        m_projectBuilder = new ProjectBuilder(runControl);
        addStartDependency(m_projectBuilder);
    }

    Target *target = runControl->target();
    m_projectInfoBeforeBuild = CppTools::CppModelManager::instance()->projectInfo(target->project());

    BuildConfiguration *buildConfiguration = target->activeBuildConfiguration();
    QTC_ASSERT(buildConfiguration, return);
    m_environment = buildConfiguration->environment();

    ToolChain *toolChain = ToolChainKitAspect::toolChain(target->kit(),
                                                         ProjectExplorer::Constants::CXX_LANGUAGE_ID);
    QTC_ASSERT(toolChain, return);
    m_targetTriple = toolChain->originalTargetTriple();
    m_toolChainType = toolChain->typeId();
}

QList<RunnerCreator> ClangToolRunWorker::runnerCreators()
{
    QList<RunnerCreator> creators;

    if (m_diagnosticConfig.clangTidyMode() != CppTools::ClangDiagnosticConfig::TidyMode::Disabled)
        creators << [this]() { return createRunner<ClangTidyRunner>(); };

    if (!m_diagnosticConfig.clazyChecks().isEmpty()) {
        if (!qEnvironmentVariable("QTC_USE_CLAZY_STANDALONE_PATH").isEmpty())
            creators << [this]() { return createRunner<ClazyStandaloneRunner>(); };
        else
            creators << [this]() { return createRunner<ClazyPluginRunner>(); };
    }

    return creators;
}

void ClangToolRunWorker::start()
{
    TaskHub::clearTasks(Debugger::Constants::ANALYZERTASK_ID);
    ProjectExplorerPlugin::saveModifiedFiles();

    if (m_projectBuilder && !m_projectBuilder->success()) {
        reportFailure();
        return;
    }

    const QString &toolName = tool()->name();
    Project *project = runControl()->project();
    m_projectInfo = CppTools::CppModelManager::instance()->projectInfo(project);
    m_projectFiles = Utils::toSet(project->files(Project::AllFiles));

    // Some projects provides CompilerCallData once a build is finished,
    if (m_projectInfo.configurationOrFilesChanged(m_projectInfoBeforeBuild)) {
        // If it's more than a release/debug build configuration change, e.g.
        // a version control checkout, files might be not valid C++ anymore
        // or even gone, so better stop here.
        reportFailure(tr("The project configuration changed since the start of "
                         "the %1. Please re-run with current configuration.").arg(toolName));
        return;
    }

    const Utils::FilePath projectFile = m_projectInfo.project()->projectFilePath();
    appendMessage(tr("Running %1 on %2 with configuration \"%3\".")
                      .arg(toolName)
                      .arg(projectFile.toUserOutput())
                      .arg(m_diagnosticConfig.displayName()),
                  Utils::NormalMessageFormat);

    // Create log dir
    if (!m_temporaryDir.isValid()) {
        const QString errorMessage
                = tr("%1: Failed to create temporary directory. Stopped.").arg(toolName);
        appendMessage(errorMessage, Utils::ErrorMessageFormat);
        TaskHub::addTask(Task::Error, errorMessage, Debugger::Constants::ANALYZERTASK_ID);
        TaskHub::requestPopup();
        reportFailure(errorMessage);
        return;
    }

    // Collect files
    const AnalyzeUnits unitsToProcess = unitsToAnalyze();
    qCDebug(LOG) << "Files to process:" << unitsToProcess;

    m_queue.clear();
    for (const AnalyzeUnit &unit : unitsToProcess) {
        for (const RunnerCreator &creator : runnerCreators())
            m_queue << QueueItem{unit, creator};
    }
    m_initialQueueSize = m_queue.count();
    m_filesAnalyzed.clear();
    m_filesNotAnalyzed.clear();

    // Set up progress information
    using namespace Core;
    m_progress = QFutureInterface<void>();
    FutureProgress *futureProgress
        = ProgressManager::addTask(m_progress.future(), tr("Analyzing"),
                                   toolName.toStdString().c_str());
    futureProgress->setKeepOnFinish(FutureProgress::HideOnFinish);
    connect(futureProgress, &FutureProgress::canceled,
            this, &ClangToolRunWorker::onProgressCanceled);
    m_progress.setProgressRange(0, m_initialQueueSize);
    m_progress.reportStarted();

    // Start process(es)
    qCDebug(LOG) << "Environment:" << m_environment;
    m_runners.clear();
    const int parallelRuns = m_runSettings.parallelJobs();
    QTC_ASSERT(parallelRuns >= 1, reportFailure(); return);
    m_success = true;

    if (m_queue.isEmpty()) {
        finalize();
        return;
    }

    reportStarted();

    while (m_runners.size() < parallelRuns && !m_queue.isEmpty())
        analyzeNextFile();
}

void ClangToolRunWorker::stop()
{
    for (ClangToolRunner *runner : m_runners) {
        QObject::disconnect(runner, nullptr, this, nullptr);
        delete runner;
    }
    m_projectFiles.clear();
    m_runners.clear();
    m_queue.clear();
    m_progress.reportFinished();

    reportStopped();
}

void ClangToolRunWorker::analyzeNextFile()
{
    if (m_progress.isFinished())
        return; // The previous call already reported that we are finished.

    if (m_queue.isEmpty()) {
        if (m_runners.isEmpty())
            finalize();
        return;
    }

    const QueueItem queueItem = m_queue.takeFirst();
    const AnalyzeUnit unit = queueItem.unit;
    qCDebug(LOG) << "analyzeNextFile:" << unit.file;

    ClangToolRunner *runner = queueItem.runnerCreator();
    m_runners.insert(runner);

    const QString executable = runner->executable();
    if (!isFileExecutable(executable)) {
        const QString errorMessage = tr("%1: Invalid executable \"%2\". Stopped.")
                                         .arg(runner->name(), executable);
        TaskHub::addTask(Task::Error, errorMessage, Debugger::Constants::ANALYZERTASK_ID);
        TaskHub::requestPopup();
        reportFailure(errorMessage);
        stop();
        return;
    }

    QTC_ASSERT(runner->run(unit.file, unit.arguments), return);

    appendMessage(tr("Analyzing \"%1\" [%2].")
                      .arg(FilePath::fromString(unit.file).toUserOutput(), runner->name()),
                  Utils::StdOutFormat);
}

void ClangToolRunWorker::onRunnerFinishedWithSuccess(const QString &filePath)
{
    auto runner = qobject_cast<ClangToolRunner *>(sender());
    const QString outputFilePath = runner->outputFilePath();
    qCDebug(LOG) << "onRunnerFinishedWithSuccess:" << outputFilePath;

    QString errorMessage;
    const Diagnostics diagnostics = tool()->read(runner->outputFileFormat(),
                                                 outputFilePath,
                                                 filePath,
                                                 m_projectFiles,
                                                 &errorMessage);
    QFile::remove(outputFilePath); // Clean-up.

    if (!errorMessage.isEmpty()) {
        m_filesAnalyzed.remove(filePath);
        m_filesNotAnalyzed.insert(filePath);
        qCDebug(LOG) << "onRunnerFinishedWithSuccess: Error reading log file:" << errorMessage;
        const QString filePath = qobject_cast<ClangToolRunner *>(sender())->fileToAnalyze();
        appendMessage(tr("Failed to analyze \"%1\": %2").arg(filePath, errorMessage),
                      Utils::StdErrFormat);
    } else {
        if (!m_filesNotAnalyzed.contains(filePath))
            m_filesAnalyzed.insert(filePath);
        if (!diagnostics.isEmpty())
            tool()->onNewDiagnosticsAvailable(diagnostics);
    }

    handleFinished();
}

void ClangToolRunWorker::onRunnerFinishedWithFailure(const QString &errorMessage,
                                                      const QString &errorDetails)
{
    qCDebug(LOG).noquote() << "onRunnerFinishedWithFailure:"
                           << errorMessage << '\n' << errorDetails;

    auto *toolRunner = qobject_cast<ClangToolRunner *>(sender());
    const QString fileToAnalyze = toolRunner->fileToAnalyze();
    const QString outputFilePath = toolRunner->outputFilePath();

    // Even in the error case the log file was created, so clean it up here, too.
    QFile::remove(outputFilePath);

    m_filesAnalyzed.remove(fileToAnalyze);
    m_filesNotAnalyzed.insert(fileToAnalyze);
    m_success = false;

    const QString message = tr("Failed to analyze \"%1\": %2").arg(fileToAnalyze, errorMessage);
    appendMessage(message, Utils::StdErrFormat);
    appendMessage(errorDetails, Utils::StdErrFormat);
    TaskHub::addTask(Task::Error, message, Debugger::Constants::ANALYZERTASK_ID);
    handleFinished();
}

void ClangToolRunWorker::handleFinished()
{
    m_runners.remove(qobject_cast<ClangToolRunner *>(sender()));
    updateProgressValue();
    sender()->deleteLater();
    analyzeNextFile();
}

void ClangToolRunWorker::onProgressCanceled()
{
    m_progress.reportCanceled();
    runControl()->initiateStop();
}

void ClangToolRunWorker::updateProgressValue()
{
    m_progress.setProgressValue(m_initialQueueSize - m_queue.size());
}

void ClangToolRunWorker::finalize()
{
    const QString toolName = tool()->name();
    appendMessage(tr("%1 finished: "
                     "Processed %2 files successfully, %3 failed.")
                      .arg(toolName)
                      .arg(m_filesAnalyzed.size())
                      .arg(m_filesNotAnalyzed.size()),
                  Utils::NormalMessageFormat);

    if (m_filesNotAnalyzed.size() != 0) {
        QString msg = tr("%1: Not all files could be analyzed.").arg(toolName);
        TaskHub::addTask(Task::Error, msg, Debugger::Constants::ANALYZERTASK_ID);
        Target *target = runControl()->target();
        if (target && !target->activeBuildConfiguration()->buildDirectory().exists()
            && !m_runSettings.buildBeforeAnalysis()) {
            msg = tr("%1: You might need to build the project to generate or update source "
                     "files. To build automatically, enable \"Build the project before starting "
                     "analysis\".")
                      .arg(toolName);
            TaskHub::addTask(Task::Error, msg, Debugger::Constants::ANALYZERTASK_ID);
        }

        TaskHub::requestPopup();
    }

    m_progress.reportFinished();
    runControl()->initiateStop();
}

template<class T>
ClangToolRunner *ClangToolRunWorker::createRunner()
{
    auto runner = new T(m_diagnosticConfig, this);
    runner->init(m_temporaryDir.path(), m_environment);
    connect(runner, &ClangToolRunner::finishedWithSuccess,
            this, &ClangToolRunWorker::onRunnerFinishedWithSuccess);
    connect(runner, &ClangToolRunner::finishedWithFailure,
            this, &ClangToolRunWorker::onRunnerFinishedWithFailure);
    return runner;
}

} // namespace Internal
} // namespace ClangTools
