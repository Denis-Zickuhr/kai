#include "engine/pipeline-step-executor.h"

namespace kai::engine {

namespace {
constexpr int kMaxOutputChars = 64 * 1024;
}

PipelineStepExecutor::PipelineStepExecutor(const QMap<QString, core::Command> *allCommands, Configure configure,
                                           QObject *parent)
    : StepExecutor(parent), m_allCommands(allCommands), m_configure(std::move(configure))
{
}

void PipelineStepExecutor::run(const core::Command &command, core::EnvironmentManager &env)
{
    m_command = command;
    m_isHttp = command.type == core::CommandType::Http;
    m_pipeline = new ExecutionPipeline(this);
    if (m_configure) {
        m_configure(*m_pipeline);
    }
    connect(m_pipeline, &ExecutionPipeline::logMessage, this, [this](const QString &, const QString &text, bool) {
        m_output += text;
        if (m_output.size() > kMaxOutputChars) {
            m_output = m_output.right(kMaxOutputChars);
        }
        emit logLine(text);
    });
    connect(m_pipeline, &ExecutionPipeline::httpResultReady, this,
            [this](const QString &, const HttpResult &result) {
                m_httpStatus = result.statusCode;
                m_httpBody = result.body;
            });
    connect(m_pipeline, &ExecutionPipeline::kipSessionStarted, this,
            [this](const QString &, KipSession *session) {
                m_kip = session;
                emit kipSessionStarted(session);
            });
    connect(m_pipeline, &ExecutionPipeline::pipelineFinished, this, &PipelineStepExecutor::handleFinished);
    m_pipeline->run(command, *m_allCommands, env);
}

void PipelineStepExecutor::stop()
{
    m_stopRequested = true;
    if (!m_pipeline) {
        return;
    }
    if (m_isHttp) {
        m_pipeline->abortHttp(m_command.id);
    }
    if (ProcessRunner *runner = m_pipeline->runnerFor(m_command.id)) {
        runner->stop();
    }
    if (m_kip) {
        m_kip->cancel();
    }
}

void PipelineStepExecutor::handleFinished(const PipelineResult &result)
{
    StepExecResult out;
    out.success = result.success;
    out.stopped = result.stoppedByRequest || m_stopRequested;
    out.exitCode = result.exitCode;
    out.errorMessage = result.errorMessage;
    out.httpStatus = m_httpStatus;
    out.output = m_isHttp ? QString::fromUtf8(m_httpBody.right(kMaxOutputChars)) : m_output;
    out.isKip = m_command.kip;
    if (m_kip && m_kip->screen().done()) {
        const core::KipDone &done = *m_kip->screen().done();
        out.hasDone = true;
        out.doneLevel = done.level;
        out.doneTitle = done.title;
        out.doneText = done.text;
    }
    emit finished(out);
}

} // namespace kai::engine
