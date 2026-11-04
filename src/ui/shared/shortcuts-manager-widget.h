#pragma once

#include <QWidget>
#include <QMap>
#include <QString>
#include <QStringList>

class QTableWidget;

namespace kai::ui {

// Gerenciador de atalhos: tabela rolável com uma linha por ação (todas as de utils::actionShortcutSpecs entram
// sozinhas), com os atalhos atuais como chips removíveis e um "+" por linha para capturar outro (uma ação pode ter
// várias sequências). Não bloqueia conflito entre ações. Usa ShortcutCaptureField, mas ANEXA o resultado a uma lista.
class ShortcutsManagerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ShortcutsManagerWidget(QWidget *parent = nullptr);

    // Uma linha por spec de utils::actionShortcutSpecs; ações ausentes de `shortcuts` usam o default da spec.
    void setShortcuts(const QMap<QString, QStringList> &shortcuts);
    // Estado atual (id de ação -> sequências), lido por SettingsDialog::buildSettings().
    QMap<QString, QStringList> shortcuts() const { return m_shortcuts; }

    // Filtro ao vivo por nome/descrição (FuzzyMatcher); vazio mostra tudo. Só esconde linhas, não mexe em m_shortcuts.
    void setFilterText(const QString &query);

    // Descarta os bindings customizados e volta aos defaults das specs. Não mexe no atalho global (resetado pelo SettingsDialog).
    void resetToDefaults();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildTable();
    void styleCell(QWidget *cell);
    // A descrição quebra em até duas linhas: a altura da linha vem da célula, não de uma conta com a fonte padrão.
    void adjustRowHeights();
    // (Re)constrói a célula de bindings da linha `row` a partir de m_shortcuts, na montagem e a cada binding adicionado/removido.
    void rebuildBindingsCell(int row, const QString &actionId);
    void addBinding(const QString &actionId, const QString &sequence);
    void removeBinding(const QString &actionId, const QString &sequence);
    // Troca a célula da linha por um ShortcutCaptureField vazio, aguardando a próxima combinação (clique no "+").
    void beginCapture(int row, const QString &actionId);

    QTableWidget *m_table = nullptr;
    QMap<QString, QStringList> m_shortcuts;
    QMap<QString, int> m_rowForActionId;
};

} // namespace kai::ui
