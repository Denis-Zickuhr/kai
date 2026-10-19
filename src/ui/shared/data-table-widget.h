#pragma once

#include "core/kip-protocol.h"

#include <QPointer>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>

#include <functional>

class QLabel;
class QPushButton;

namespace kai::ui::fields {

// Papéis de dado das células de uma tabela de dados KIP.
inline constexpr int kRowKeyRole = Qt::UserRole;          // chave da linha (todas as células)
inline constexpr int kBadgeToneRole = Qt::UserRole + 3;   // badge: tom
inline constexpr int kRowActionsRole = Qt::UserRole + 4;  // coluna de ações: ids permitidos na linha (QStringList)

// Tabela de dados (KIP `table`, bloco e campo): Ctrl+C e o menu de contexto copiam a célula atual (§11), as colunas
// têm formato (mono, muted, number, badge, copy — §27) e a altura mostra N linhas inteiras.
class DataTableWidget : public QTableWidget {
    Q_OBJECT

public:
    using QTableWidget::QTableWidget;

    // Altura que mostra `rows` linhas inteiras. É recalculada quando o tema é aplicado e ao exibir: na criação o
    // header ainda não tem o padding do QSS e parece bem menor do que será, o que roubava linhas da tabela.
    void fitVisibleRows(int rows);
    // Altura a mais (a linha de confirmação de uma ação é mais alta que as outras).
    void setExtraHeight(int px);

    void setColumnFormats(const QStringList &formats) { m_formats = formats; }
    QString columnFormat(int column) const { return m_formats.value(column); }
    QString rowKey(int row) const;
    int rowForKey(const QString &key) const;

    // Linhas pintadas de outro jeito pelas ações de linha: rodando (texto apagado) e sucesso (tinta verde breve).
    void setBusyRow(const QString &key);
    void setFlashRow(const QString &key);
    const QString &busyRow() const { return m_busyRow; }
    const QString &flashRow() const { return m_flashRow; }
    int hoverRow() const { return m_hoverRow; }

    // Mais itens no menu de contexto de uma linha (as ações de linha entram aqui: o teclado chega nelas pelo menu).
    using MenuHook = std::function<void(QMenu &menu, int row)>;
    void setMenuHook(MenuHook hook) { m_menuHook = std::move(hook); }

signals:
    void hoverRowChanged(int row);

protected:
    bool event(QEvent *e) override;
    bool viewportEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void refit();
    void copyCurrentCell();
    bool handleCopyClick(const QPoint &pos);

    int m_visibleRows = 0;
    int m_extraHeight = 0;
    int m_hoverRow = -1;
    QStringList m_formats;
    QString m_busyRow;
    QString m_flashRow;
    MenuHook m_menuHook;
};

// Pinta as células conforme o formato da coluna: badge (pílula tingida), mono, muted, number (à direita) e copy
// (ícone de copiar no hover). Também a tinta das linhas rodando/sucesso.
class DataCellDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit DataCellDelegate(DataTableWidget *table);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    // A faixa de confirmação de uma ação de linha ocupa a célula inteira (o padrão desconta o padding do QSS).
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    // Retângulo do ícone de copiar numa célula `copy`.
    static QRect copyIconRect(const QRect &cell);

private:
    DataTableWidget *m_table;
};

QColor kipToneColor(const QString &tone);

// ============================================================================================================
// Ações por linha (§27): uma coluna de ícones à direita (aparecem no hover, na linha selecionada e quando a linha
// está rodando), menu de contexto com as mesmas ações, e a confirmação que transforma a própria linha numa faixa
// "Apagar X? [Manter] [Apagar]". Não guarda estado de verdade: tudo vem de sync() (o KipChipRun da tela).
// ============================================================================================================
class RowActionsController : public QObject {
    Q_OBJECT

public:
    enum class Phase { None, Confirming, Running, Success, Error };

    RowActionsController(DataTableWidget *table, int actionsColumn, const QVector<core::KipChip> &actions);

    // Estado vindo do modelo: a execução corrente DESTA tabela (row vazio = nenhuma) e se dá para clicar agora.
    void sync(const QString &row, const QString &action, Phase phase, bool enabled);

    DataTableWidget *table() const { return m_table; }
    int actionsColumn() const { return m_column; }
    const QVector<core::KipChip> &actions() const { return m_actions; }
    QStringList allowedFor(int row) const;
    bool enabled() const { return m_enabled; }
    Phase phase() const { return m_phase; }
    const QString &runRow() const { return m_row; }
    const QString &runAction() const { return m_action; }
    int spinnerAngle() const { return m_angle; }
    int hoverIcon() const { return m_hoverIcon; }
    // Ícones visíveis na linha (até 3; o resto vai para "…") e os retângulos deles dentro da célula.
    QStringList visibleIcons(int row) const;
    QRect iconRect(const QRect &cell, int index, int count) const;
    QWidget *confirmStrip() const { return m_confirm; }

    static int columnWidthFor(int iconCount);

signals:
    void actionClicked(const QString &row, const QString &action);
    void confirmed();
    void cancelled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showConfirm(int row, const core::KipChip &action);
    void hideConfirm();
    void showOverflowMenu(int row, const QPoint &globalPos);
    const core::KipChip *actionNamed(const QString &id) const;

    DataTableWidget *m_table;
    int m_column;
    QVector<core::KipChip> m_actions;
    bool m_enabled = false;
    Phase m_phase = Phase::None;
    QString m_row;
    QString m_action;
    QString m_flashKey;
    int m_hoverIcon = -1;
    int m_angle = 0;
    QTimer m_spin;
    QTimer m_flash;
    QPointer<QWidget> m_confirm;
    int m_confirmRow = -1;
};

// Pinta a coluna de ações (ícones, spinner, ✓).
class RowActionsDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit RowActionsDelegate(RowActionsController *controller);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    RowActionsController *m_controller;
};

} // namespace kai::ui::fields
