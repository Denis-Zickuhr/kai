#pragma once

#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QString>
#include <QTimer>
#include <QVector>

#include <functional>

#include "core/notification-history.h"

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QToolButton;

namespace kai::ui {

// Central de notificações: lista agrupada por dia (ícone do tipo, título,
// prévia, hora, indicador de não lida), filtro Todas/Não lidas, busca, painel
// de detalhe e ações (marcar como lida/não lida, ir para o comando, copiar,
// excluir, marcar todas, limpar). Segue os design tokens (cores, cantos).
//
// Não marca nada como lido ao abrir: a notificação só vira lida depois de
// ficar selecionada um instante (ou pelas ações explícitas). Reage a mudanças
// do histórico (nova notificação chegando com o diálogo aberto).
class NotificationHistoryDialog : public QDialog {
    Q_OBJECT

public:
    // Devolve o NOME do comando com esse id, ou vazio se ele não existe mais
    // (aí "Ir para o comando" some).
    using CommandResolver = std::function<QString(const QString &commandId)>;

    explicit NotificationHistoryDialog(core::NotificationHistory *history, QWidget *parent = nullptr);

    void setCommandResolver(CommandResolver resolver);

    // Texto curto de "quando": "agora", "há 5 min" ou HH:mm. Estático para
    // teste.
    static QString relativeTime(const QDateTime &when, const QDateTime &now);
    // Rótulo do cabeçalho de grupo: Hoje / Ontem / dd/MM/yyyy.
    static QString dayLabel(const QDate &date, const QDate &today);

signals:
    // Pedido para abrir o comando que originou a notificação. O diálogo fecha.
    void commandRequested(const QString &commandId);

private:
    enum class Filter { All, Unread };

    void setupUi();
    void applyStyle();
    void reload();
    void scheduleReload();
    void updateDetail();
    const core::NotificationRecord *selectedRecord() const;
    void showContextMenu(const QPoint &pos);

    void toggleSelectedRead();
    void deleteSelected();
    void copySelected();
    void goToSelectedCommand();
    void markAllRead();
    void clearAll();

    core::NotificationHistory *m_history = nullptr;
    CommandResolver m_resolver;
    QVector<core::NotificationRecord> m_records;   // todos, mais recentes primeiro
    QString m_selectedId;
    Filter m_filter = Filter::All;
    bool m_reloadQueued = false;
    QTimer m_markReadTimer;

    QLabel *m_summaryLabel = nullptr;
    QToolButton *m_filterAllButton = nullptr;
    QToolButton *m_filterUnreadButton = nullptr;
    QLineEdit *m_searchField = nullptr;

    QStackedWidget *m_listStack = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_emptyListText = nullptr;

    QStackedWidget *m_detailStack = nullptr;
    QLabel *m_detailIcon = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailChip = nullptr;
    QLabel *m_detailWhen = nullptr;
    QTextBrowser *m_detailBody = nullptr;
    QPushButton *m_toggleReadButton = nullptr;
    QPushButton *m_goToButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_deleteButton = nullptr;

    QPushButton *m_markAllButton = nullptr;
    QPushButton *m_clearButton = nullptr;
};

} // namespace kai::ui
