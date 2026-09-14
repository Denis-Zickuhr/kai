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

// Seleção de Pre/Post/Cleanup Hooks: segmented control "Pre-Hooks (N) | Post-Hooks (N) | Cleanup (N)"
// trocando uma única lista de largura cheia (QStackedWidget), com busca compartilhada que filtra a
// fase ativa. Cada fase é uma lista de checkboxes independente; o modelo é core::Hooks.
class HooksEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit HooksEditorWidget(QWidget *parent = nullptr);

    // Conjunto exibido por padrão (busca vazia): comandos da mesma pasta, sem o comando atual.
    void setAvailableCommands(const QVector<core::Command> &availableCommands);

    // Universo completo (todas as pastas, sem o comando atual), usado só quando há busca. Opcional:
    // sem ele a busca fica restrita a `availableCommands`.
    void setAllAvailableCommands(const QVector<core::Command> &allCommands);

    void setHooks(const core::Hooks &hooks);
    core::Hooks hooks() const;
    // Total de hooks nas 3 fases (badge do CollapsibleSectionCard).
    int totalCount() const;

signals:
    // Emitido quando uma fase muda.
    void changed();

private:
    void setupUi();
    void populateList(QListWidget *list, const QVector<core::Command> &source, const QStringList &selectedIds);
    static QStringList checkedIds(QListWidget *list);
    // Busca vazia: repopula de `m_availableCommands`; senão de `m_allCommands` (ou `m_availableCommands`
    // se ausente) com fuzzy match no nome. Preserva o que estava marcado.
    void applyFilter(QListWidget *list, const QString &query);
    void switchPhase(int phaseIndex);
    void updatePhaseButtonLabels();
    // Anota no texto de cada item se o comando já está marcado em outra fase.
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
