#pragma once

#include <QWidget>
#include <QVector>

#include "core/models.h"

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace kai::ui {

// Widget de seleção de Pre/Post/Cleanup Hooks (resumo da spec seção 3:
// "vincular requisições HTTP a comandos Shell através de Hooks").
//
// REDESENHO (mockup enviado pelo usuário): antes eram TRÊS colunas lado a
// lado, cada uma com sua própria busca — em nomes de comando longos, isso
// cortava texto e exigia rolagem horizontal. Agora é um segmented control
// "Pre-Hooks (N) | Post-Hooks (N) | Cleanup (N)" trocando uma ÚNICA lista
// (QStackedWidget) de largura cheia, com uma busca COMPARTILHADA que filtra
// a fase ativa. Cada fase continua sendo uma lista de checkboxes
// independente por baixo (m_preList/m_postList/m_cleanupList) — só a
// APRESENTAÇÃO mudou, o modelo de dados (core::Hooks) é o mesmo de sempre.
class HooksEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HooksEditorWidget(QWidget *parent = nullptr);

    // `availableCommands` deve conter apenas comandos da mesma pasta,
    // excluindo o comando atual (se estiver editando um já existente). É
    // este o conjunto exibido por padrão (busca vazia).
    void setAvailableCommands(const QVector<core::Command> &availableCommands);

    // `allCommands` é o universo COMPLETO de comandos do app (todas as
    // pastas), excluindo o comando atual — usado só quando o usuário digita
    // algo na busca (feedback do usuário: "por padrão só lista hooks da
    // pasta, mas se pesquisar, aparecem TODOS"). Opcional: se nunca for
    // chamado, a busca continua restrita a `availableCommands` (comportamento
    // antigo), então chamadores que não tiverem a lista completa à mão não
    // quebram nada.
    void setAllAvailableCommands(const QVector<core::Command> &allCommands);

    void setHooks(const core::Hooks &hooks);
    core::Hooks hooks() const;
    // Total de hooks configurados nas 3 fases somadas — usado pelo badge
    // do CollapsibleSectionCard que envolve este widget.
    int totalCount() const;

signals:
    // Emitido sempre que uma fase muda (marcar/desmarcar um item) — ver
    // ParameterEditorWidget::changed() pro mesmo propósito.
    void changed();

private:
    void setupUi();
    void populateList(QListWidget *list, const QVector<core::Command> &source, const QStringList &selectedIds);
    static QStringList checkedIds(QListWidget *list);
    // Busca vazia: repopula `list` a partir de `m_availableCommands` (só a
    // pasta). Busca não-vazia: repopula a partir de `m_allCommands` (todo o
    // app, se disponível — senão cai de volta em `m_availableCommands`),
    // filtrando por fuzzy match no nome. Preserva o que já estava marcado.
    void applyFilter(QListWidget *list, const QString &query);
    void switchPhase(int phaseIndex);
    void updatePhaseButtonLabels();
    // Anota, no texto de cada item de `list`, se o comando já está
    // marcado em OUTRA fase (mockup: tag "[PRE-HOOK]" num item enquanto
    // se olha a aba Post-Hooks, por exemplo) — evita duplicar sem querer.
    void refreshCrossPhaseAnnotations();

    QPushButton *m_preTabButton = nullptr;
    QPushButton *m_postTabButton = nullptr;
    QPushButton *m_cleanupTabButton = nullptr;
    QLineEdit *m_search = nullptr; // compartilhada entre as 3 fases
    QStackedWidget *m_listStack = nullptr;
    QListWidget *m_preList = nullptr;
    QListWidget *m_postList = nullptr;
    QListWidget *m_cleanupList = nullptr;
    int m_activePhase = 0; // 0=pre, 1=post, 2=cleanup
    QVector<core::Command> m_availableCommands;
    QVector<core::Command> m_allCommands; // universo completo, usado só na busca
};

} // namespace kai::ui
