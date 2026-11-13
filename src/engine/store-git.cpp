#include "engine/store-git.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace kai::engine {

StoreGitCheckout::StoreGitCheckout(QObject *parent) : QObject(parent) {}

StoreGitCheckout::~StoreGitCheckout()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString StoreGitCheckout::gitExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("git"));
}

bool StoreGitCheckout::isAllowedRepoUrl(const QString &url)
{
    if (url.startsWith(QLatin1Char('-'))) {
        return false; // nunca vira opção do git
    }
    if (url.startsWith(QLatin1String("https://")) || url.startsWith(QLatin1String("file://"))) {
        return true;
    }
    if (url.startsWith(QLatin1String("http://"))) {
        const QString host = QUrl(url).host().toLower();
        return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1");
    }
    // Caminho local (Windows "C:\..." incluso), mas não "host:caminho" (scp-like) nem outros esquemas.
    if (url.contains(QLatin1String("::")) || url.contains(QLatin1String("://"))) {
        return false;
    }
    return url.startsWith(QLatin1Char('/')) || url.startsWith(QLatin1Char('.'))
        || (url.size() > 2 && url[1] == QLatin1Char(':') && (url[2] == QLatin1Char('\\') || url[2] == QLatin1Char('/')));
}

void StoreGitCheckout::checkout(const QString &url, const QString &ref, const QString &destDir,
                                std::function<void(const QString &)> done)
{
    m_done = std::move(done);
    m_dir = destDir;
    m_cancelled = false;
    m_finished = false;
    m_index = 0;
    m_steps.clear();

    if (gitExecutable().isEmpty()) {
        QTimer::singleShot(0, this, [this]() { finish(QStringLiteral("git_missing")); });
        return;
    }
    if (!isAllowedRepoUrl(url)) {
        QTimer::singleShot(0, this, [this]() { finish(QStringLiteral("bad_url")); });
        return;
    }
    QDir().mkpath(destDir);
    const bool existing = QFileInfo::exists(QDir(destDir).filePath(QStringLiteral(".git")));
    if (!existing) {
        m_steps << QStringList{QStringLiteral("init"), QStringLiteral("--quiet")};
        m_steps << QStringList{QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), url};
    } else {
        m_steps << QStringList{QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("origin"), url};
    }
    const QString wanted = ref.isEmpty() ? QStringLiteral("HEAD") : ref;
    m_steps << QStringList{QStringLiteral("fetch"), QStringLiteral("--quiet"), QStringLiteral("--depth"),
                           QStringLiteral("1"), QStringLiteral("--no-tags"), QStringLiteral("origin"),
                           QStringLiteral("--"), wanted};
    m_steps << QStringList{QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("--force"),
                           QStringLiteral("--detach"), QStringLiteral("FETCH_HEAD")};
    runNext();
}

void StoreGitCheckout::runNext()
{
    if (m_cancelled) {
        finish(QStringLiteral("cancelled"));
        return;
    }
    if (m_index >= m_steps.size()) {
        finish(QString());
        return;
    }
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_ALLOW_PROTOCOL"), QStringLiteral("https:file"));
    env.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
    env.insert(QStringLiteral("GIT_ASKPASS"), QStringLiteral("echo"));
    m_process->setProcessEnvironment(env);
    QStringList args = {QStringLiteral("-c"), QStringLiteral("core.symlinks=false"), QStringLiteral("-c"),
                        QStringLiteral("protocol.ext.allow=never")};
    args += m_steps[m_index];
    QTimer *timeout = new QTimer(m_process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, m_process, [this]() {
        if (m_process) {
            m_process->setProperty("kai_timeout", true);
            m_process->kill();
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && m_process) {
            QProcess *p = m_process;
            m_process = nullptr;
            p->deleteLater();
            finish(QStringLiteral("git_missing"));
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        QProcess *p = m_process;
        if (!p) {
            return;
        }
        m_process = nullptr;
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        const bool timedOut = p->property("kai_timeout").toBool();
        p->deleteLater();
        if (m_cancelled) {
            finish(QStringLiteral("cancelled"));
        } else if (timedOut) {
            finish(QStringLiteral("timeout"));
        } else if (status != QProcess::NormalExit || exitCode != 0) {
            finish(QStringLiteral("git_failed:") + err);
        } else {
            ++m_index;
            runNext();
        }
    });
    m_process->start(gitExecutable(), args);
    timeout->start(180000);
}

void StoreGitCheckout::cancel()
{
    m_cancelled = true;
    if (m_process) {
        m_process->kill();
    }
}

void StoreGitCheckout::finish(const QString &error)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    auto done = std::move(m_done);
    m_done = nullptr;
    if (done) {
        done(error);
    }
}

} // namespace kai::engine
