#include "ui/notification-history-dialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

#include "ui/dialog-utils.h"
#include "utils/translation-manager.h"

namespace kai::ui {

NotificationHistoryDialog::NotificationHistoryDialog(core::NotificationHistory *history, QWidget *parent)
    : QDialog(parent)
    , m_history(history)
{
    setWindowTitle(utils::tr(QStringLiteral("notifications.history.title")));
    setupUi();
    reload();
    resize(760, 480);
    centerOnParent(this);
}

void NotificationHistoryDialog::setupUi()
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

    m_detailBody = new QLabel(this);
    m_detailBody->setWordWrap(true);
    m_detailBody->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(m_detailBody, 1);
    body->addLayout(right, 1);

    // Rodapé: Marcar todas como lidas / Limpar / Fechar.
    auto *footer = new QHBoxLayout();
    auto *markAllReadButton = new QPushButton(utils::tr(QStringLiteral("notifications.history.action.mark_all_read")), this);
    auto *clearButton = new QPushButton(utils::tr(QStringLiteral("notifications.history.action.clear")), this);
    connect(markAllReadButton, &QPushButton::clicked, this, &NotificationHistoryDialog::handleMarkAllRead);
    connect(clearButton, &QPushButton::clicked, this, &NotificationHistoryDialog::handleClear);
    footer->addWidget(markAllReadButton);
    footer->addWidget(clearButton);
    footer->addStretch();
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(buttonBox);
    outer->addLayout(footer);
}

void NotificationHistoryDialog::reload()
{
    m_records = m_history ? m_history->load() : QVector<core::NotificationRecord>();
    m_list->clear();
    for (const core::NotificationRecord &r : m_records) {
        const QString when = r.createdAt.toString(QStringLiteral("dd/MM HH:mm:ss"));
        // Bolinha cheia = não lida, vazia = lida (mesmo espírito visual do
        // ✓/✗ de RunHistoryDialog — símbolo geométrico básico, sem depender
        // de fonte de emoji).
        const QString mark = r.read ? QStringLiteral("○") : QStringLiteral("●");
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2  %3").arg(mark, when, r.title));
        if (!r.read) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_list->addItem(item);
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    } else {
        m_detailHeader->clear();
        m_detailBody->clear();
    }
}

void NotificationHistoryDialog::handleSelectionChanged()
{
    const int idx = m_list->currentRow();
    if (idx < 0 || idx >= m_records.size()) {
        m_detailHeader->clear();
        m_detailBody->clear();
        return;
    }
    const core::NotificationRecord &r = m_records.at(idx);
    m_detailHeader->setText(QStringLiteral("%1 — %2").arg(r.title, r.createdAt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"))));
    m_detailBody->setText(r.body);

    // Abrir o detalhe marca como lida — mesma convenção de qualquer caixa
    // de notificações/e-mail; o botão "marcar todas como lidas" cobre o
    // caso de limpar o indicador sem abrir uma por uma.
    if (!r.read && m_history) {
        m_history->markRead(r.id);
        m_records[idx].read = true;
        QListWidgetItem *item = m_list->item(idx);
        if (item) {
            QFont f = item->font();
            f.setBold(false);
            item->setFont(f);
            item->setText(QStringLiteral("○  %1  %2")
                .arg(r.createdAt.toString(QStringLiteral("dd/MM HH:mm:ss")), r.title));
        }
    }
}

void NotificationHistoryDialog::handleMarkAllRead()
{
    if (m_history) {
        m_history->markAllRead();
    }
    reload();
}

void NotificationHistoryDialog::handleClear()
{
    if (m_history) {
        m_history->clear();
    }
    reload();
}

} // namespace kai::ui
