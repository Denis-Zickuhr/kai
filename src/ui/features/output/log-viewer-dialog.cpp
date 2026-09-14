#include "ui/features/output/log-viewer-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "utils/logger.h"

#include <QFile>
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QToolButton>
#include <QLabel>

#include "utils/translation-manager.h"

namespace kai::ui {

LogViewerDialog::LogViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("logs.title")));
    setSizeGripEnabled(true);
    resize(720, 420);
    setupUi();
    centerOnParent(this);

    // CARREGA O HISTÓRICO DO ARQUIVO antes de escutar o sinal.
    // Causa raiz do "visualizador de logs não funciona no Windows": este
    // diálogo SÓ escutava LoggerSink::logEmitted, então nascia vazio e só
    // mostrava o que fosse logado DEPOIS de aberto. Como no Windows o Kai é GUI
    // app (sem console), o usuário recorria a esta janela justamente para ver o
    // que JÁ aconteceu — e encontrava o vazio. O arquivo sempre existiu
    // (Logger grava com flush) e Logger::logFilePath() já era exposto; faltava
    // alguém ler.
    loadExistingLogFile();

    connect(&utils::LoggerSink::instance(), &utils::LoggerSink::logEmitted,
            this, &LogViewerDialog::handleLogEmitted);
}

void LogViewerDialog::loadExistingLogFile()
{
    const QString path = utils::Logger::logFilePath();
    if (path.isEmpty()) {
        m_logView->appendPlainText(
            utils::tr(QStringLiteral("logs.file_unavailable")));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_logView->appendPlainText(
            QStringLiteral("%1\n%2").arg(utils::tr(QStringLiteral("logs.file_unreadable")), path));
        return;
    }
    // Lê apenas a CAUDA do arquivo: um log de sessões acumuladas pode ter
    // megabytes e travar a GUI se despejado inteiro no QPlainTextEdit.
    constexpr qint64 kMaxTail = 512 * 1024;
    if (file.size() > kMaxTail) {
        file.seek(file.size() - kMaxTail);
        file.readLine(); // descarta a primeira linha, provavelmente cortada
    }
    const QString content = QString::fromUtf8(file.readAll());
    if (!content.trimmed().isEmpty()) {
        m_logView->appendPlainText(content.trimmed());
    }
    m_logView->appendPlainText(
        QStringLiteral("--- %1: %2 ---").arg(utils::tr(QStringLiteral("logs.file")), path));
}

void LogViewerDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->addWidget(new QLabel(utils::tr(QStringLiteral("logs.min_level")), this));

    m_levelFilter = new QComboBox(this);
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.debug")), static_cast<int>(utils::LogLevel::Debug));
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.info")), static_cast<int>(utils::LogLevel::Info));
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.warning")), static_cast<int>(utils::LogLevel::Warning));
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.error")), static_cast<int>(utils::LogLevel::Error));
    connect(m_levelFilter, &QComboBox::currentIndexChanged, this, &LogViewerDialog::handleLevelFilterChanged);

    m_clearButton = new QToolButton(this);
    m_clearButton->setText(utils::tr(QStringLiteral("logs.clear")));
    connect(m_clearButton, &QToolButton::clicked, this, &LogViewerDialog::handleClearClicked);

    toolbarLayout->addWidget(m_levelFilter);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_clearButton);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(kMaxEntries);
    m_logView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(utils::tokens::codeAreaQss()));

    mainLayout->addLayout(toolbarLayout);
    mainLayout->addWidget(m_logView);
}

QString LogViewerDialog::levelLabel(utils::LogLevel level)
{
    switch (level) {
    case utils::LogLevel::Debug: return QStringLiteral("DEBUG");
    case utils::LogLevel::Info: return QStringLiteral("INFO");
    case utils::LogLevel::Warning: return QStringLiteral("WARN");
    case utils::LogLevel::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString LogViewerDialog::formatEntry(const utils::LogEntry &entry)
{
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")),
             levelLabel(entry.level),
             entry.tag,
             entry.message);
}

void LogViewerDialog::handleLogEmitted(const utils::LogEntry &entry)
{
    appendEntry(entry);
}

void LogViewerDialog::appendEntry(const utils::LogEntry &entry)
{
    m_entries.append(entry);
    if (m_entries.size() > kMaxEntries) {
        m_entries.remove(0, m_entries.size() - kMaxEntries);
    }

    if (static_cast<int>(entry.level) < static_cast<int>(m_minLevel)) {
        return;
    }

    m_logView->appendPlainText(formatEntry(entry));
}

void LogViewerDialog::handleClearClicked()
{
    m_entries.clear();
    m_logView->clear();
}

void LogViewerDialog::handleLevelFilterChanged(int index)
{
    m_minLevel = static_cast<utils::LogLevel>(m_levelFilter->itemData(index).toInt());
    rebuildView();
}

void LogViewerDialog::rebuildView()
{
    m_logView->clear();
    for (const utils::LogEntry &entry : m_entries) {
        if (static_cast<int>(entry.level) < static_cast<int>(m_minLevel)) {
            continue;
        }
        m_logView->appendPlainText(formatEntry(entry));
    }
}

} // namespace kai::ui
