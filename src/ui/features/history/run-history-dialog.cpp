#include "ui/features/history/run-history-dialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

#include "ui/shared/dialog-utils.h"
#include "utils/translation-manager.h"

namespace kai::ui {

RunHistoryDialog::RunHistoryDialog(core::RunHistory *history, QWidget *parent)
    : QDialog(parent)
    , m_history(history)
{
    setWindowTitle(utils::tr(QStringLiteral("runs.title")));
    setupUi();
    reload();
    resize(760, 480);
    centerOnParent(this);
}

void RunHistoryDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    auto *body = new QHBoxLayout();
    outer->addLayout(body, 1);

    m_list = new QListWidget(this);
    m_list->setMinimumWidth(280);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { handleSelectionChanged(); });
    body->addWidget(m_list);

    auto *right = new QVBoxLayout();
    m_detailHeader = new QLabel(this);
    m_detailHeader->setWordWrap(true);
    m_detailHeader->setStyleSheet(QStringLiteral("font-weight: bold;"));
    right->addWidget(m_detailHeader);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    right->addWidget(m_output, 1);
    body->addLayout(right, 1);

    // Rodapé: Reexecutar / Limpar / Fechar.
    auto *footer = new QHBoxLayout();
    auto *rerunButton = new QPushButton(utils::tr(QStringLiteral("runs.action.rerun")), this);
    auto *clearButton = new QPushButton(utils::tr(QStringLiteral("runs.action.clear")), this);
    connect(rerunButton, &QPushButton::clicked, this, &RunHistoryDialog::handleRerun);
    connect(clearButton, &QPushButton::clicked, this, &RunHistoryDialog::handleClear);
    footer->addWidget(rerunButton);
    footer->addWidget(clearButton);
    footer->addStretch();
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(buttonBox);
    outer->addLayout(footer);
}

void RunHistoryDialog::reload()
{
    m_records = m_history ? m_history->load() : QVector<core::RunRecord>();
    m_list->clear();
    for (const core::RunRecord &r : m_records) {
        const QString when = r.startedAt.toString(QStringLiteral("dd/MM HH:mm:ss"));
        const QString mark = r.success ? QStringLiteral("✓") : QStringLiteral("✗");
        m_list->addItem(QStringLiteral("%1  %2  [%3]  %4")
            .arg(mark, when, r.commandType, r.commandName));
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    } else {
        m_detailHeader->clear();
        m_output->clear();
    }
}

void RunHistoryDialog::handleSelectionChanged()
{
    const int idx = m_list->currentRow();
    if (idx < 0 || idx >= m_records.size()) {
        m_detailHeader->clear();
        m_output->clear();
        return;
    }
    const core::RunRecord &r = m_records.at(idx);
    m_detailHeader->setText(QStringLiteral("%1 — %2 — %3 — %4 ms — %5")
        .arg(r.commandName,
             r.commandType,
             r.startedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")))
        .arg(r.durationMs)
        .arg(r.success ? utils::tr(QStringLiteral("runs.success")) : utils::tr(QStringLiteral("runs.failed"))));
    m_output->setPlainText(r.output);
}

void RunHistoryDialog::handleRerun()
{
    const int idx = m_list->currentRow();
    if (idx < 0 || idx >= m_records.size()) {
        return;
    }
    emit rerunRequested(m_records.at(idx).commandId);
    accept();
}

void RunHistoryDialog::handleClear()
{
    if (m_history) {
        m_history->clear();
    }
    reload();
}

} // namespace kai::ui
