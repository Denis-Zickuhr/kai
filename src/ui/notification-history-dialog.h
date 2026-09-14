#pragma once

#include <QDialog>
#include <QVector>

#include "core/notification-history.h"

class QListWidget;
class QLabel;

namespace kai::ui {

// Tela de histórico de notificações (pedido do usuário: "queria melhorar o
// processamento de notificações, talvez salvar elas e adicionar uma seção
// para ler, ver todo o log e conteúdo. Ação de limpar, marcar como lida").
// Mesmo padrão de RunHistoryDialog: lista à esquerda + detalhe à direita.
class NotificationHistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit NotificationHistoryDialog(core::NotificationHistory *history, QWidget *parent = nullptr);

private slots:
    void handleSelectionChanged();
    void handleMarkAllRead();
    void handleClear();

private:
    void setupUi();
    void reload();

    core::NotificationHistory *m_history = nullptr;
    QVector<core::NotificationRecord> m_records;

    QListWidget *m_list = nullptr;
    QLabel *m_detailHeader = nullptr;
    QLabel *m_detailBody = nullptr;
};

} // namespace kai::ui
