#include "ui/features/history/process-list-dialog.h"

#include "ui/shared/dialog-utils.h"
#include <QSet>
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QScrollBar>
#include <QColor>
#include <QSignalBlocker>

#include "utils/translation-manager.h"

namespace kai::ui {

namespace {
constexpr int kCommandIdRole = Qt::UserRole + 1;

QString statusLabel(engine::ProcessStatus status)
{
    switch (status) {
    case engine::ProcessStatus::Running: return utils::tr(QStringLiteral("processes.status.running"));
    case engine::ProcessStatus::Success: return utils::tr(QStringLiteral("processes.status.success"));
    case engine::ProcessStatus::Error:   return utils::tr(QStringLiteral("processes.status.error"));
    }
    return utils::tr(QStringLiteral("processes.status.unknown"));
}

// Indicador visual por linha (feedback do usuário: "quero um
// indicador visual na linha, não só texto"). Bolinha colorida como prefixo
// (● — símbolo geométrico básico do Unicode, com cobertura garantida em
// fontes comuns como DejaVu Sans, diferente de emojis coloridos reais
// como 🔵/🟢/🔴 que exigem uma fonte de emoji instalada e não apareciam
// em sistemas sem ela) e cor de texto correspondente ao estado do processo.
QString statusIcon(engine::ProcessStatus status)
{
    Q_UNUSED(status);
    return QStringLiteral("●");
}

QColor statusColor(engine::ProcessStatus status)
{
    switch (status) {
    case engine::ProcessStatus::Running: return QColor(86, 156, 214);
    case engine::ProcessStatus::Success: return QColor(106, 191, 105);
    case engine::ProcessStatus::Error:   return QColor(224, 108, 117);
    }
    return QColor(Qt::gray);
}
}

ProcessListDialog::ProcessListDialog(engine::ProcessManager *processManager, QWidget *parent)
    : QDialog(parent)
    , m_processManager(processManager)
{
    setWindowTitle(utils::tr(QStringLiteral("processes.title")));
    setSizeGripEnabled(true);
    resize(600, 400);
    setupUi();
    refreshProcessList();
    centerOnParent(this);
}

void ProcessListDialog::setupUi()
{
    auto *mainLayout = new QHBoxLayout(this);

    auto *leftColumn = new QVBoxLayout();
    leftColumn->addWidget(new QLabel(utils::tr(QStringLiteral("processes.list_label")), this));
    m_processListWidget = new QListWidget(this);
    connect(m_processListWidget, &QListWidget::currentItemChanged, this, &ProcessListDialog::handleCurrentItemChanged);
    leftColumn->addWidget(m_processListWidget);

    m_stopButton = new QPushButton(utils::tr(QStringLiteral("processes.stop")), this);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &ProcessListDialog::handleStopClicked);
    leftColumn->addWidget(m_stopButton);

    auto *rightColumn = new QVBoxLayout();
    rightColumn->addWidget(new QLabel(utils::tr(QStringLiteral("processes.log_label")), this));
    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(utils::tokens::codeAreaQss()));
    rightColumn->addWidget(m_logView);

    mainLayout->addLayout(leftColumn, 1);
    mainLayout->addLayout(rightColumn, 2);
}

void ProcessListDialog::refreshProcessList()
{
    const QString previousSelection = m_processListWidget->currentItem()
        ? m_processListWidget->currentItem()->data(kCommandIdRole).toString()
        : QString();

    // Bloqueia sinais durante o rebuild: QListWidget::clear() dispara
    // currentItemChanged() de forma síncrona, que por sua vez consultaria
    // o ProcessManager potencialmente durante uma reentrância perigosa
    // (crash reportado ao finalizar processos em background).
    const QSignalBlocker blocker(m_processListWidget);

    m_processListWidget->clear();
    QSet<QString> alreadyListed;

    for (const QString &commandId : m_processManager->trackedCommandIds()) {
        const QString displayName = m_displayNames.value(commandId, commandId);
        const engine::ProcessStatus status = m_processManager->statusOf(commandId);

        // Indicador visual por linha (bolinha colorida + texto de status),
        // em vez de apenas texto entre colchetes — mais fácil de escanear
        // visualmente numa lista com vários processos.
        const QString label = QStringLiteral("%1  %2  %3").arg(statusIcon(status), displayName, statusLabel(status));

        auto *item = new QListWidgetItem(label, m_processListWidget);
        item->setData(kCommandIdRole, commandId);
        item->setForeground(statusColor(status));

        if (commandId == previousSelection) {
            m_processListWidget->setCurrentItem(item);
        }
        alreadyListed.insert(commandId);
    }

    // FOREGROUND: comandos em execução que não passam pelo ProcessManager.
    if (m_foregroundProvider) {
        for (const auto &entry : m_foregroundProvider()) {
            if (alreadyListed.contains(entry.first)) {
                continue;
            }
            const QString displayName = m_displayNames.value(entry.first, entry.first);
            QString label = QStringLiteral("%1  %2  %3")
                                .arg(statusIcon(engine::ProcessStatus::Running), displayName,
                                     statusLabel(engine::ProcessStatus::Running));
            if (entry.second > 0) {
                label += QStringLiteral("  (pid %1)").arg(entry.second);
            }
            auto *item = new QListWidgetItem(label, m_processListWidget);
            item->setData(kCommandIdRole, entry.first);
            item->setForeground(statusColor(engine::ProcessStatus::Running));
            if (entry.first == previousSelection) {
                m_processListWidget->setCurrentItem(item);
            }
        }
    }
}

void ProcessListDialog::setForegroundProvider(
    std::function<QList<QPair<QString, qint64>>()> provider)
{
    m_foregroundProvider = std::move(provider);
}

void ProcessListDialog::setCommandName(const QString &commandId, const QString &displayName)
{
    m_displayNames[commandId] = displayName;
}

void ProcessListDialog::appendLogFor(const QString &commandId, const QString &text, bool isError)
{
    Q_UNUSED(isError);
    m_logs[commandId] += text;

    QListWidgetItem *current = m_processListWidget->currentItem();
    if (current && current->data(kCommandIdRole).toString() == commandId) {
        m_logView->setPlainText(m_logs.value(commandId));
        m_logView->verticalScrollBar()->setValue(m_logView->verticalScrollBar()->maximum());
    }
}

void ProcessListDialog::handleCurrentItemChanged()
{
    QListWidgetItem *current = m_processListWidget->currentItem();
    if (!current) {
        m_logView->clear();
        m_stopButton->setEnabled(false);
        return;
    }

    const QString commandId = current->data(kCommandIdRole).toString();
    m_logView->setPlainText(m_logs.value(commandId));
    m_stopButton->setEnabled(m_processManager->statusOf(commandId) == engine::ProcessStatus::Running);
}

void ProcessListDialog::handleStopClicked()
{
    QListWidgetItem *current = m_processListWidget->currentItem();
    if (!current) {
        return;
    }
    m_processManager->stop(current->data(kCommandIdRole).toString());
}

void ProcessListDialog::applyThemeVariables(const QMap<QString, QString> &variables)
{
    // Fundo/fonte seguindo o tema (feedback do usuário): usa variáveis
    // específicas de terminal se declaradas, caindo para bg/fg base do
    // tema, com fallback final escuro seguro. Fonte monospace no log
    // (saída de processo) e a fonte do tema na lista.
    const QString bg = variables.value(QStringLiteral("terminal_bg"),
        variables.value(QStringLiteral("bg"), utils::tokens::codeBg()));
    const QString fg = variables.value(QStringLiteral("terminal_fg"),
        variables.value(QStringLiteral("fg"), QStringLiteral("#f0f0f0")));
    const QString altBg = variables.value(QStringLiteral("alt_bg"), bg);
    const QString selBg = variables.value(QStringLiteral("sel_bg"),
        variables.value(QStringLiteral("accent_color"), utils::tokens::accent()));

    m_logView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; font-family: monospace; "
        "border: none; padding: 4px; }").arg(bg, fg));

    m_processListWidget->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: %1; color: %2; border: none; }"
        "QListWidget::item { padding: 4px; }"
        "QListWidget::item:selected { background-color: %3; color: %2; }")
        .arg(altBg, fg, selBg));
}

} // namespace kai::ui
