#include "ui/external-run-session.h"

namespace kai::ui {

ExternalRunSession::ExternalRunSession(const QString &commandId, InputWriter inputWriter, QObject *parent)
    : QObject(parent)
    , m_commandId(commandId)
    , m_inputWriter(std::move(inputWriter))
{
}

void ExternalRunSession::writeInput(const QString &text)
{
    if (!m_finished && m_inputWriter) {
        m_inputWriter(text);
    }
}

void ExternalRunSession::publishOutput(const QString &text, bool isError)
{
    if (!m_finished) {
        emit output(text, isError);
    }
}

void ExternalRunSession::publishBackground(qint64 pid)
{
    if (!m_finished) {
        emit background(pid);
    }
}

void ExternalRunSession::publishFinished(int exitCode, const QString &message)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    emit finished(exitCode, message);
    deleteLater();
}

} // namespace kai::ui
