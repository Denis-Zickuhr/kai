#pragma once

#include <QWidget>
#include <QStringList>
#include <QVector>

#include "core/models.h"

class QLineEdit;
class QCompleter;
class QStandardItemModel;
class QToolButton;
class QScrollArea;
class QFrame;
class QEvent;

namespace kai::ui {

class FlowLayout;

// Escolha de entradas de Collection já em memória: digitar filtra (fuzzy, como o CollectionSelectorDialog)
// e cada escolha vira uma chip removível, sem diálogo. A lupa (pickButton()) fica exposta para o chamador
// abrir o seletor completo; este widget não conhece o CollectionSelectorDialog.
class CollectionChipPickerWidget : public QWidget {
    Q_OBJECT

public:
    explicit CollectionChipPickerWidget(QWidget *parent = nullptr);

    // Fonte de dados + qual campo do schema vira o texto da chip/sugestão.
    void setEntries(const QVector<core::CollectionEntry> &entries, const QString &displayField);

    // Substitui a seleção inteira e reconstrói as chips. Não emite selectionChanged().
    void setSelectedIds(const QStringList &ids);
    QStringList selectedIds() const { return m_selectedIds; }

    // Botão de lupa; o chamador conecta o clicked().
    QToolButton *pickButton() const { return m_pickButton; }

    QLineEdit *searchField() const { return m_input; }

protected:
    // Backspace no campo de busca vazio apaga a última chip (filtro instalado em m_input).
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // Emitido por mudanças feitas aqui, não por setSelectedIds(). Ligado a ParameterFormDialog::
    // refreshValidationState, que não reconhece este widget no laço genérico de validação.
    void selectionChanged();

private:
    void rebuildChips();
    void addChip(const QString &entryId);
    void removeEntry(const QString &entryId);
    void updateSuggestions(const QString &query);
    QString labelFor(const QString &entryId) const;

    QVector<core::CollectionEntry> m_entries;
    QString m_displayField;
    QStringList m_selectedIds; // ordem de escolha, sem duplicatas

    // this (borda/fundo do card): barra de busca (m_input + m_pickButton), m_separator (só com chips) e
    // m_scrollArea (rola na vertical, altura travada em ~3 linhas) com o m_flowHost das chips.
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_flowHost = nullptr;
    FlowLayout *m_flow = nullptr;
    QFrame *m_separator = nullptr;
    QLineEdit *m_input = nullptr;
    QCompleter *m_completer = nullptr;
    QStandardItemModel *m_completerModel = nullptr;
    QToolButton *m_pickButton = nullptr;
};

} // namespace kai::ui
