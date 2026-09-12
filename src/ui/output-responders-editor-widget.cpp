#include "ui/output-responders-editor-widget.h"

#include "ui/table-utils.h"
#include "ui/row-edit-dialog.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QCheckBox>
#include <QToolButton>
#include <QColor>
#include <QAbstractItemView>

namespace kai::ui {

namespace {
// Tabela SOMENTE-LEITURA. 0=Nome, 1=Ações (lápis/lixeira inline — novo
// padrão visual, ver ParameterEditorWidget). Só o nome é listado (o resto —
// condição/resposta/limite — edita-se no formulário). Nome é obrigatório.
constexpr int kOrColName = 0;
constexpr int kOrColActions = 1;
constexpr int kOrColumnCount = 2;
} // namespace

OutputRespondersEditorWidget::OutputRespondersEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void OutputRespondersEditorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_table = new QTableWidget(0, kOrColumnCount, this);
    configureTable(m_table, {
        {utils::tr(QStringLiteral("responders.col.name")), 240, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Sem header nem coluna de seleção (mockup enviado pelo usuário: só uma
    // coluna de dado + ações inline — ver ParameterEditorWidget).
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(kOrColName, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_responders.size() && editRowViaForm(row)) {
            rebuildTable();
        }
    });
    layout->addWidget(m_table);
}

void OutputRespondersEditorWidget::rebuildTable()
{
    m_table->setRowCount(m_responders.size());
    for (int row = 0; row < m_responders.size(); ++row) {
        const core::OutputResponder &r = m_responders.at(row);
        auto makeItem = [](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        // Só o nome é listado. Se desabilitado, sinaliza entre parênteses.
        const QString shown = r.enabled ? r.name
            : QStringLiteral("%1  (%2)").arg(r.name, utils::tr(QStringLiteral("responders.disabled_tag")));
        m_table->setItem(row, kOrColName, makeItem(shown));

        // Ações inline (lápis/lixeira) — ver ParameterEditorWidget::rebuildTable.
        m_table->setCellWidget(row, kOrColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editRowViaForm(row)) rebuildTable(); },
            [this, row]() { removeResponderAt(row); }));
    }
    emit changed();
}

void OutputRespondersEditorWidget::removeResponderAt(int row)
{
    if (row < 0 || row >= m_responders.size()) {
        return;
    }
    m_responders.remove(row);
    rebuildTable();
}

void OutputRespondersEditorWidget::setResponders(const QVector<core::OutputResponder> &responders)
{
    m_responders = responders;
    rebuildTable();
}

QVector<core::OutputResponder> OutputRespondersEditorWidget::responders() const
{
    QVector<core::OutputResponder> result;
    for (const core::OutputResponder &r : m_responders) {
        // Nome é obrigatório (feedback do usuário): sem nome, o responsor não
        // entra na lista salva.
        if (!r.name.trimmed().isEmpty()) {
            result.append(r);
        }
    }
    return result;
}

void OutputRespondersEditorWidget::handleAddRowClicked()
{
    m_responders.append(core::OutputResponder{});
    const int row = m_responders.size() - 1;
    if (editRowViaForm(row)) {
        rebuildTable();
    } else {
        m_responders.remove(row);
    }
}

bool OutputRespondersEditorWidget::editRowViaForm(int row)
{
    if (row < 0 || row >= m_responders.size()) {
        return false;
    }
    const core::OutputResponder &r = m_responders.at(row);

    QVector<RowEditDialog::FieldSpec> fields;
    fields.append({QStringLiteral("name"), utils::tr(QStringLiteral("responders.field.name")),
                   RowEditDialog::FieldType::Text, r.name,
                   {}, utils::tr(QStringLiteral("responders.field.name.placeholder")), false,
                   utils::tr(QStringLiteral("responders.field.name.tip"))});
    fields.append({QStringLiteral("enabled"), utils::tr(QStringLiteral("responders.field.enabled")),
                   RowEditDialog::FieldType::Bool,
                   r.enabled ? QStringLiteral("true") : QStringLiteral("false"),
                   {}, {}, false, {}});
    // "pattern"/"response" placeholders abaixo são EXEMPLOS DE SINTAXE (um
    // regex literal e uma resposta de uma letra), não prosa da UI — traduzir
    // mudaria o significado do exemplo (o regex deixaria de casar o prompt
    // real) ou seria sem sentido para um valor de uma letra ("y"). Mantidos
    // como estão de propósito (ver AGENTS.md §8 / tarefa de auditoria i18n).
    fields.append({QStringLiteral("pattern"), utils::tr(QStringLiteral("responders.field.pattern")),
                   RowEditDialog::FieldType::Text, r.pattern,
                   {}, QStringLiteral("Continuar\\? \\[y/N\\]"), false,
                   utils::tr(QStringLiteral("responders.field.pattern.tip"))});
    fields.append({QStringLiteral("response"), utils::tr(QStringLiteral("responders.field.response")),
                   RowEditDialog::FieldType::Text, r.response,
                   {}, QStringLiteral("y"), false,
                   utils::tr(QStringLiteral("responders.field.response.tip"))});
    fields.append({QStringLiteral("limit"), utils::tr(QStringLiteral("responders.field.limit")),
                   RowEditDialog::FieldType::Bool,
                   r.limitTriggers ? QStringLiteral("true") : QStringLiteral("false"),
                   {}, {}, false,
                   utils::tr(QStringLiteral("responders.field.limit.tip"))});
    // Campo "máximo de disparos": só faz sentido com o limite ligado. O
    // RowEditDialog não tem visibilidade condicional, então o mostramos
    // sempre, mas o valor só é usado quando o limite está marcado.
    fields.append({QStringLiteral("max"), utils::tr(QStringLiteral("responders.field.max")),
                   RowEditDialog::FieldType::Text,
                   QString::number(r.maxTriggers > 0 ? r.maxTriggers : 1),
                   {}, QStringLiteral("1"), false,
                   utils::tr(QStringLiteral("responders.field.max.tip"))});

    RowEditDialog dialog(utils::tr(QStringLiteral("responders.edit_row")), fields, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    core::OutputResponder updated;
    updated.name = dialog.value(QStringLiteral("name")).trimmed();
    updated.enabled = dialog.value(QStringLiteral("enabled")) == QStringLiteral("true");
    updated.pattern = dialog.value(QStringLiteral("pattern"));
    updated.response = dialog.value(QStringLiteral("response"));
    updated.limitTriggers = dialog.value(QStringLiteral("limit")) == QStringLiteral("true");
    const int maxv = dialog.value(QStringLiteral("max")).toInt();
    updated.maxTriggers = maxv > 0 ? maxv : 1;
    m_responders[row] = updated;
    return true;
}

void OutputRespondersEditorWidget::handleRemoveRowClicked()
{
    // Sem uso interno desde que a exclusão virou um ícone inline por linha
    // (ver rebuildTable/removeResponderAt) — mantido público só por
    // compatibilidade de API; remove a linha atualmente selecionada, se
    // houver.
    removeResponderAt(m_table->currentRow());
}

} // namespace kai::ui
