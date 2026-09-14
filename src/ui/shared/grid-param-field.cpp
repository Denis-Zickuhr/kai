#include "ui/shared/grid-param-field.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFrame>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFontMetrics>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/grid-param.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/parameter-field-factory.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {

constexpr int kMaxVisibleRows = 8;
constexpr int kMinColumnWidth = 128;
constexpr int kBoolColumnWidth = 96;

QString isoFormat(const QString &mode, const QDateTime &value)
{
    return fields::isoDateText(mode, value);
}

} // namespace

GridParamField::GridParamField(const QVector<core::GridColumn> &columns, QWidget *parent)
    : QWidget(parent)
    , m_columns(columns)
{
    setObjectName(QStringLiteral("gridParamField"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#gridParamField { background: transparent; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));

    // Contêiner que segura o grid inteiro (barra de ações + tabela), como um card do app.
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("gridParamCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(QStringLiteral(
        "QFrame#gridParamCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
        .arg(tk::surface2(), tk::borderColor()).arg(tk::radiusMd()));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    cardLayout->setSpacing(tk::space(2));
    layout->addWidget(card);

    m_table = new QTableWidget(0, int(m_columns.size()) + 1, this);
    m_table->setObjectName(QStringLiteral("gridParamTable"));
    QStringList headers;
    for (const core::GridColumn &column : m_columns) {
        headers << (column.label.isEmpty() ? column.name : column.label);
    }
    headers << QString();
    m_table->setHorizontalHeaderLabels(headers);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(tk::controlHeight() + tk::space(2));
    m_table->horizontalHeader()->setFixedHeight(tk::controlHeight());
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->horizontalHeader()->setSectionsClickable(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    for (int c = 0; c < m_columns.size(); ++c) {
        if (core::gridColumnType(m_columns.at(c)) == core::ParameterType::Date) {
            // O campo de data não cabe numa fatia estreita da tabela.
            m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Fixed);
            m_table->setColumnWidth(c, m_columns.at(c).dateMode == QStringLiteral("datetime") ? 210 : 160);
        } else if (core::gridColumnType(m_columns.at(c)) == core::ParameterType::Bool) {
            m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Fixed);
            m_table->setColumnWidth(c, kBoolColumnWidth);
        } else {
            m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
        }
        if (QTableWidgetItem *header = m_table->horizontalHeaderItem(c)) {
            header->setToolTip(header->text());
        }
    }
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this, [this]() { elideHeaders(); });
    m_table->horizontalHeader()->setMinimumSectionSize(tk::iconButtonSize());
    m_table->horizontalHeader()->setSectionResizeMode(int(m_columns.size()), QHeaderView::Fixed);
    m_table->setColumnWidth(int(m_columns.size()), tk::iconButtonSize() + tk::space(2));
    m_table->viewport()->installEventFilter(this);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(m_table->horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this]() { refreshChrome(); });
    // Valores de células ficam em widgets; sem isto o Tab não passa de uma célula para a seguinte.
    m_table->setTabKeyNavigation(false);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget#gridParamTable { background: transparent; border: none; padding: 0; }"
        "QTableWidget#gridParamTable QWidget#qt_scrollarea_viewport { background: transparent; }"
        "QTableWidget#gridParamTable::item { padding: 0; }"
        "QTableWidget#gridParamTable QHeaderView { background: transparent; }"
        "QTableWidget#gridParamTable QHeaderView::section { background: transparent; color: %1; border: none;"
        " border-bottom: 1px solid %2; padding: %3px %4px; font-weight: 600; }"
        "QLineEdit#gridCellEdit, QComboBox#gridCellCombo {"
        " background-color: %7; border: 1px solid %2; border-radius: %5px; padding: 0 %4px; min-height: %9px; }"
        "QLineEdit#gridCellEdit:hover, QComboBox#gridCellCombo:hover { border: 1px solid %8; }"
        "QLineEdit#gridCellEdit:focus, QComboBox#gridCellCombo:focus {"
        " border: 1px solid %6; background-color: %7; }")
        .arg(tk::mutedFg(), tk::borderColor()).arg(tk::space(1)).arg(tk::space(2)).arg(tk::radiusSm())
        .arg(tk::accent(), tk::bg(), tk::mutedFg()).arg(tk::controlHeight() - tk::space(2) - 2));
    cardLayout->addWidget(m_table);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(tk::space(2));
    QPushButton *add = makeAccentAddButton(this, utils::tr(QStringLiteral("params.grid.add_row")));
    connect(add, &QPushButton::clicked, this, &GridParamField::addRow);
    toolbar->addWidget(add);
    m_pasteButton = makeIconButton(this, QStringLiteral("clipboard-paste"), utils::tr(QStringLiteral("params.grid.paste")),
                                   QColor(tk::mutedFg()));
    connect(m_pasteButton, &QToolButton::clicked, this, &GridParamField::pasteFromClipboard);
    toolbar->addWidget(m_pasteButton);
    toolbar->addStretch(1);
    m_count = new QLabel(this);
    m_count->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    toolbar->addWidget(m_count);
    // Acima da tabela: o botão não muda de lugar quando as linhas crescem.
    cardLayout->insertLayout(0, toolbar);
    layout->addStretch(1); // sobra de altura vai para baixo, nunca para a barra de cima

    refreshChrome();
}

// Com muitas colunas o campo não encolhe abaixo de kMinColumnWidth: as colunas esticáveis passam a largura fixa e a
// tabela rola na horizontal; com espaço de sobra voltam a dividir a largura.
void GridParamField::updateColumnModes()
{
    QHeaderView *header = m_table->horizontalHeader();
    int fixedWidth = 0;
    int flexible = 0;
    for (int c = 0; c <= m_columns.size(); ++c) {
        const core::ParameterType type = c < m_columns.size() ? core::gridColumnType(m_columns.at(c)) : core::ParameterType::Bool;
        if (c == m_columns.size() || type == core::ParameterType::Date || type == core::ParameterType::Bool) {
            fixedWidth += m_table->columnWidth(c);
        } else {
            ++flexible;
        }
    }
    if (flexible == 0) {
        return;
    }
    const bool tooNarrow = (m_table->viewport()->width() - fixedWidth) / flexible < kMinColumnWidth;
    for (int c = 0; c < m_columns.size(); ++c) {
        const core::ParameterType type = core::gridColumnType(m_columns.at(c));
        if (type == core::ParameterType::Date || type == core::ParameterType::Bool) {
            continue;
        }
        header->setSectionResizeMode(c, tooNarrow ? QHeaderView::Fixed : QHeaderView::Stretch);
        if (tooNarrow) {
            m_table->setColumnWidth(c, kMinColumnWidth);
        }
    }
}

// O estilo do cabeçalho corta o texto dos dois lados em vez de elidir: o rótulo é encurtado aqui, com o completo na dica.
void GridParamField::elideHeaders()
{
    QFont font = m_table->horizontalHeader()->font();
    font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    for (int c = 0; c < m_columns.size(); ++c) {
        QTableWidgetItem *item = m_table->horizontalHeaderItem(c);
        if (!item) {
            continue;
        }
        const QString full = m_columns.at(c).label.isEmpty() ? m_columns.at(c).name : m_columns.at(c).label;
        // Coluna ainda sem largura de verdade (tabela em página não exibida): mantém o rótulo inteiro.
        const int room = m_table->columnWidth(c) - 2 * tk::space(2);
        item->setText(room > metrics.horizontalAdvance(QChar(0x2026)) * 2 ? metrics.elidedText(full, Qt::ElideRight, room) : full);
        item->setToolTip(full);
    }
}

void GridParamField::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        updateColumnModes();
        elideHeaders();
    });
}

bool GridParamField::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table->viewport() && event->type() == QEvent::Resize) {
        updateColumnModes();
        elideHeaders();
    }
    return QWidget::eventFilter(watched, event);
}

QWidget *GridParamField::makeEditor(const core::GridColumn &column, const QJsonValue &value)
{
    const QString text = core::gridCellText(column, value);
    QWidget *editor = nullptr;
    switch (core::gridColumnType(column)) {
    case core::ParameterType::Number: {
        auto *edit = new QLineEdit(text, m_table);
        edit->setObjectName(QStringLiteral("gridCellEdit"));
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral(R"(-?\d*[.,]?\d*)")), edit));
        connect(edit, &QLineEdit::textChanged, this, &GridParamField::changed);
        editor = edit;
        break;
    }
    case core::ParameterType::Bool: {
        auto *host = new QWidget(m_table);
        host->setObjectName(QStringLiteral("gridCellBool"));
        host->setAttribute(Qt::WA_StyledBackground, true);
        host->setStyleSheet(QStringLiteral("QWidget#gridCellBool { background: transparent; }"));
        auto *hostLayout = new QHBoxLayout(host);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        auto *box = new QCheckBox(host);
        box->setChecked(text == QStringLiteral("true"));
        hostLayout->addWidget(box, 0, Qt::AlignCenter);
        connect(box, &QCheckBox::toggled, this, &GridParamField::changed);
        editor = host;
        break;
    }
    case core::ParameterType::Select: {
        auto *combo = new QComboBox(m_table);
        combo->setObjectName(QStringLiteral("gridCellCombo"));
        for (const QString &option : column.options) {
            const fields::ChoiceOption parsed = fields::parseParamOption(option);
            combo->addItem(parsed.label, parsed.value);
        }
        const int index = combo->findData(text);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        connect(combo, &QComboBox::currentIndexChanged, this, &GridParamField::changed);
        editor = combo;
        break;
    }
    case core::ParameterType::Date: {
        const QString mode = column.dateMode;
        const fields::DatePickField picked = fields::makeDatePickField(
            m_table, text, mode, false, [mode](const QDateTime &dt) { return isoFormat(mode, dt); });
        // O campo tem estilo próprio (feito para um formulário solto): aqui precisa caber na altura da linha.
        picked.edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: transparent; color: %1; border: none; padding: 0 %2px; min-height: 0; }")
            .arg(tk::fg()).arg(tk::space(2)));
        picked.container->setObjectName(QStringLiteral("gridCellDate"));
        picked.container->setAttribute(Qt::WA_StyledBackground, true);
        picked.container->setStyleSheet(QStringLiteral(
            "QWidget#gridCellDate { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
            .arg(tk::bg(), tk::borderColor()).arg(tk::radiusSm()));
        connect(picked.edit, &QLineEdit::textChanged, this, &GridParamField::changed);
        editor = picked.container;
        break;
    }
    default: {
        auto *edit = new QLineEdit(text, m_table);
        edit->setObjectName(QStringLiteral("gridCellEdit"));
        connect(edit, &QLineEdit::textChanged, this, &GridParamField::changed);
        editor = edit;
        break;
    }
    }
    // Altura travada e margem: sem isso o min-height do QSS global estoura a linha e o campo sai cortado.
    return editor->objectName() == QStringLiteral("gridCellBool") ? editor : cellHost(editor, m_table);
}

QString GridParamField::editorText(const core::GridColumn &column, QWidget *editor) const
{
    switch (core::gridColumnType(column)) {
    case core::ParameterType::Bool: {
        const auto *box = editor->findChild<QCheckBox *>();
        return box && box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    }
    case core::ParameterType::Select: {
        const auto *combo = editor->findChild<QComboBox *>();
        return combo ? combo->currentData().toString() : QString();
    }
    case core::ParameterType::Date: {
        const auto *edit = editor->findChild<QLineEdit *>();
        if (!edit) {
            return QString();
        }
        const QVariant formatted = edit->property(fields::kDatePropStartFormatted);
        return formatted.isValid() ? formatted.toString() : edit->text();
    }
    default: {
        const auto *edit = editor->findChild<QLineEdit *>();
        return edit ? edit->text() : QString();
    }
    }
}

void GridParamField::appendRow(const QJsonObject &row)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    for (int c = 0; c < m_columns.size(); ++c) {
        m_table->setCellWidget(r, c, makeEditor(m_columns.at(c), row.value(m_columns.at(c).name)));
    }
    // A coluna de ações guarda o marcador da linha: o índice muda quando outra linha é removida.
    auto *marker = new QWidget(m_table);
    marker->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *markerLayout = new QHBoxLayout(marker);
    markerLayout->setContentsMargins(0, 0, 0, 0);
    QToolButton *remove = makeRemoveButton(marker, utils::tr(QStringLiteral("params.grid.remove_row")));
    connect(remove, &QToolButton::clicked, this, [this, marker]() { removeRow(marker); });
    markerLayout->addWidget(remove, 0, Qt::AlignCenter);
    m_table->setCellWidget(r, int(m_columns.size()), marker);
}

void GridParamField::removeRow(QWidget *rowMarker)
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->cellWidget(r, int(m_columns.size())) == rowMarker) {
            m_table->removeRow(r);
            refreshChrome();
            emit changed();
            return;
        }
    }
}

void GridParamField::setRows(const QJsonArray &rows)
{
    m_table->setRowCount(0);
    for (const QJsonValue &row : rows) {
        appendRow(row.toObject());
    }
    refreshChrome();
    emit changed();
}

QJsonArray GridParamField::rows() const
{
    QJsonArray result;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QJsonObject row;
        for (int c = 0; c < m_columns.size(); ++c) {
            row.insert(m_columns.at(c).name, core::gridCellValue(m_columns.at(c), editorText(m_columns.at(c), m_table->cellWidget(r, c))));
        }
        result.append(row);
    }
    return result;
}

int GridParamField::rowCount() const
{
    return m_table->rowCount();
}

bool GridParamField::hasContent() const
{
    const QJsonArray current = rows();
    const QJsonObject blank = core::gridDefaultRow(m_columns);
    for (const QJsonValue &row : current) {
        if (row.toObject() != blank) {
            return true;
        }
    }
    return false;
}

void GridParamField::addRow()
{
    appendRow(core::gridDefaultRow(m_columns));
    refreshChrome();
    if (QWidget *first = m_table->cellWidget(m_table->rowCount() - 1, 0)) {
        m_table->scrollToBottom();
        if (QWidget *inner = first->findChild<QWidget *>()) {
            inner->setFocus(Qt::OtherFocusReason);
        } else {
            first->setFocus(Qt::OtherFocusReason);
        }
    }
    emit changed();
}

void GridParamField::appendRowsFromText(const QString &text)
{
    const QJsonArray parsed = core::gridRowsFromDelimitedText(m_columns, text);
    for (const QJsonValue &row : parsed) {
        appendRow(row.toObject());
    }
    refreshChrome();
    emit changed();
}

void GridParamField::pasteFromClipboard()
{
    appendRowsFromText(QGuiApplication::clipboard()->text());
}

// Altura da tabela: acompanha as linhas até kMaxVisibleRows e rola a partir daí.
void GridParamField::refreshChrome()
{
    if (!m_count) {
        return; // ainda no construtor
    }
    const int visible = qBound(1, m_table->rowCount(), kMaxVisibleRows);
    const int rowHeight = m_table->verticalHeader()->defaultSectionSize();
    const int frame = 0; // a tabela não tem borda nem recuo próprios: o card é quem os tem
    const int scrollBar = m_table->horizontalScrollBar()->maximum() > 0 ? m_table->horizontalScrollBar()->sizeHint().height() : 0;
    m_table->setFixedHeight(m_table->horizontalHeader()->height() + visible * rowHeight + frame + scrollBar);
    m_count->setText(utils::tr(QStringLiteral("params.grid.count")).arg(m_table->rowCount()));
}

} // namespace kai::ui
