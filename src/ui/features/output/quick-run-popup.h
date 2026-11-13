#pragma once

#include <QFrame>
#include <QSet>
#include <QColor>
#include <QString>
#include <QVector>

class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QListWidgetItem;

namespace kai::ui {

// Estado das ações inline da lista (lido pelo delegate): qual ação está sob o mouse e qual a do teclado (Tab).
struct QuickRunActionState {
    bool enabled = false;
    int hoverRow = -1;
    int hoverIndex = -1;
    int activeIndex = -1;
};

// Paleta rápida para achar e executar um comando sem a lista principal à vista (ela pode estar recolhida): um campo de
// busca fuzzy e, embaixo, os itens que casam. Enter (ou clique) escolhe; Esc, ou clicar fora, fecha. Quem abre é a barra
// de guias da Saída (só comandos) ou o Ctrl+K (comandos, pastas, notas, ambientes e ações); quem age é o dono, via
// commandChosen / entryChosen.
class QuickRunPopup : public QFrame {
    Q_OBJECT

public:
    enum class Kind { Command, Http, Folder, Collection, Note, Environment, Action };

    struct Entry {
        QString id;
        QString name;
        QString path; // pastas que levam ao item (ou o atalho, nas ações): só contexto e busca
        Kind kind = Kind::Command;
        double score = 0; // quanto o item é usado (core::PaletteUsage): maior sobe na lista
        QString chipText = QString();  // chip de identificação (Configurações → Identificação), antes do nome; vazio = sem chip
        QColor chipColor = QColor();
        QString chipIcon = QString();  // chip só de ícone (linguagem do script)
    };

    explicit QuickRunPopup(QWidget *parent = nullptr);

    void setEntries(const QVector<Entry> &entries);
    // Abre logo abaixo de `anchor` (coordenadas globais), limitado à tela, com o campo vazio e em foco.
    void openBelow(const QPoint &anchorGlobal);
    // Sem janela do Kai por perto: no alto e no meio da área disponível da tela.
    void openOnScreen(const QRect &available);
    void refreshStyle();
    void setPlaceholder(const QString &text);
    // Abre no alto de `window`, centralizada (o modo do Ctrl+K).
    void openCentered(const QWidget *window);
    // Ações inline na linha ativa (editar, duplicar, mostrar na lista): o Enter continua sendo a ação padrão. Tab percorre as
    // ações da linha e Enter executa a marcada; Ctrl+E edita. Desligadas por padrão.
    void setInlineActionsEnabled(bool enabled);
    // Dispara uma ação ("edit", "duplicate", "reveal") no item atual, como o clique no ícone. false se não existir nele.
    bool triggerCurrentAction(const QString &actionId);

    // Para os testes.
    QLineEdit *searchField() const { return m_search; }
    QListWidget *list() const { return m_list; }
    QStringList visibleIds() const;

signals:
    void commandChosen(const QString &commandId);
    void entryChosen(QuickRunPopup::Kind kind, const QString &id);
    // Uma ação inline: `actionId` é "edit", "duplicate" ou "reveal" (Ambiente só tem "edit").
    void entryActionRequested(QuickRunPopup::Kind kind, const QString &id, const QString &actionId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applyFilter();
    void chooseCurrent();
    void present(const QPoint &topLeft);
    // (Re)monta a barra de chips de filtro por tipo, conforme os tipos presentes nas entradas.
    void rebuildFilterChips();
    // Posiciona os chips (quebrando de linha pela largura atual) e fixa a altura da barra.
    void layoutChips();
    // Chave de memória do filtro (objectName, ou "quickRun"): lembra os tipos escolhidos entre aberturas.
    QString filterMemoryKey() const;
    // Ação inline sob `viewportPos` (ícone à direita da linha); false se não houver. Preenche o item e o índice.
    bool actionAt(const QPoint &viewportPos, QListWidgetItem **item, int *index) const;
    bool handleViewportEvent(QEvent *event);
    void triggerAction(QListWidgetItem *item, int index);
    void cycleAction(int step);

    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_empty = nullptr;
    QWidget *m_filterBar = nullptr;
    QVector<QPushButton *> m_chips;
    QuickRunActionState m_actionState;
    QVector<Entry> m_entries;
    bool m_mixed = false; // há mais que comandos: cada linha ganha o ícone do seu tipo
    // Tipos presentes nas entradas atuais (define quais chips de filtro aparecem).
    QVector<Kind> m_availableKinds;
    // Tipos ATIVOS no filtro; vazio = todos. Lembrado entre aberturas por filterMemoryKey().
    QSet<int> m_activeKinds;
};

} // namespace kai::ui
