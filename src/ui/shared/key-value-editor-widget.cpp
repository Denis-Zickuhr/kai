#include "ui/shared/key-value-editor-widget.h"

#include "ui/shared/table-utils.h"
#include "ui/shared/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "ui/shared/lucide-icons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QToolButton>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QStackedWidget>
#include <QAbstractItemView>
#include <QColor>
#include <QFont>
#include <QPushButton>

namespace kai::ui {

namespace {
// Tabela somente-leitura: 0=Chave, 1=Valor, 2=Ações (lápis/lixeira inline). O modo secreto só habilita máscara/olho no valor.
constexpr int kKvColKey = 0;
constexpr int kKvColValue = 1;

// Formulário de um par chave-valor, com valor expansível (QPlainTextEdit: URLs, tokens, JSON). No modo secreto
// há um toggle "mascarar" e o valor nasce oculto.
class KeyValueRowDialog : public QDialog {
public:
    KeyValueRowDialog(const QString &key, const QString &value, bool secret,
                      bool hasSecret, const QString &keyLabel, const QString &valueLabel,
                      QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(utils::tr(QStringLiteral("keyvalue.edit_row")));
        setSizeGripEnabled(true);
        resize(460, 340);

        auto *outer = new QVBoxLayout(this);
        auto *form = new QFormLayout();
        outer->addLayout(form);

        m_key = new QLineEdit(key, this);
        m_key->setPlaceholderText(utils::tr(QStringLiteral("keyvalue.key.placeholder")));
        form->addRow(keyLabel, m_key);

        m_value = new QPlainTextEdit(value, this);
        m_value->setPlaceholderText(utils::tr(QStringLiteral("keyvalue.value.placeholder")));
        m_value->setMinimumHeight(utils::tokens::space(20));
        form->addRow(valueLabel, m_value);

        if (hasSecret) {
            m_secret = new QCheckBox(utils::tr(QStringLiteral("keyvalue.secret_field")), this);
            // Toggle de formulário (kaiRole="switch").
            m_secret->setProperty("kaiRole", QStringLiteral("switch"));
            m_secret->setChecked(secret);
            // Direto na linha do formulário: um QWidget só para embrulhar o toggle ganharia o fundo escuro global.
            form->addRow(QString(), m_secret);
        }

        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        stripDialogButtonIcons(box);
        connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
        outer->addWidget(box);
        centerOnParent(this);
    }

    QString key() const { return m_key->text().trimmed(); }
    QString value() const { return m_value->toPlainText(); }
    bool secret() const { return m_secret && m_secret->isChecked(); }

private:
    QLineEdit *m_key = nullptr;
    QPlainTextEdit *m_value = nullptr;
    QCheckBox *m_secret = nullptr;
};
} // namespace

KeyValueEditorWidget::KeyValueEditorWidget(QWidget *parent, const QString &keyHeader,
                                           const QString &valueHeader)
    : QWidget(parent)
    , m_keyHeader(keyHeader)
    , m_valueHeader(valueHeader)
{
    setupUi(keyHeader, valueHeader);
}

void KeyValueEditorWidget::setupUi(const QString &keyHeader, const QString &valueHeader)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(0, 3, this);
    configureTable(m_table, {
        {keyHeader,   240, false},
        {valueHeader, 120, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    // Só a coluna chave fica visível; valor/secreto se editam pelo formulário.
    m_table->setColumnHidden(kKvColValue, true);
    m_table->horizontalHeader()->setSectionResizeMode(kKvColKey, QHeaderView::Stretch);
    // A chave é a coluna elástica e a de ações tem largura fixa: com stretchLastSection (do configureTable) as ações
    // esticavam e empurravam os botões inline para fora da viewport.
    m_table->horizontalHeader()->setStretchLastSection(false);
    // Sem header nem coluna de seleção (como ParameterEditorWidget/OutputRespondersEditorWidget).
    m_table->horizontalHeader()->setVisible(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setObjectName(QStringLiteral("kvTable"));
    m_table->setShowGrid(false);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget#kvTable { background: transparent; }"
        "QTableWidget#kvTable::item { border-bottom: 1px solid %1; padding: 0px %2px; }")
        .arg(utils::tokens::borderColor()).arg(utils::tokens::space(2)));
    m_table->verticalHeader()->setDefaultSectionSize(utils::tokens::iconButtonSize() + 4);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    // Altura mínima de 5 linhas (a tabela costuma ter poucas variáveis, menos que as 6 do configureTable).
    m_table->setMinimumHeight(5 * (utils::tokens::iconButtonSize() + 4));
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Somente-leitura: edição pelo formulário (lápis inline ou duplo-clique).
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_rows.size() && editRow(row)) {
            rebuildTable();
        }
    });

    // Só o "+" fica na barra inferior; mantido aqui (e não só no cabeçalho de um card) porque o widget também é usado sem card (Folder/Environment).
    m_buttonsRow = new QWidget(this);
    auto *buttonsLayout = new QHBoxLayout(m_buttonsRow);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->addStretch();
    m_addButton = makeAddButton(m_buttonsRow, utils::tr(QStringLiteral("keyvalue.add")));
    connect(m_addButton, &QToolButton::clicked, this, &KeyValueEditorWidget::handleAddRowClicked);
    buttonsLayout->addWidget(m_addButton);

    m_emptyLabel = new QLabel(utils::tr(QStringLiteral("keyvalue.empty")), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
        .arg(utils::tokens::mutedFg()));
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_table);
    m_stack->addWidget(m_emptyLabel);
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_buttonsRow);

    // Linha de adição rápida (opt-in, ver setInlineAddRowEnabled): criada escondida para existir se o dono ligar a opção depois de setValues().
    constexpr int kQuickH = 30;
    m_quickAddRow = new QWidget(this);
    m_quickAddRow->setObjectName(QStringLiteral("kvQuickAddRow"));
    m_quickAddRow->setStyleSheet(QStringLiteral("QWidget#kvQuickAddRow { background: transparent; }"));
    auto *quickAddLayout = new QHBoxLayout(m_quickAddRow);
    quickAddLayout->setContentsMargins(0, utils::tokens::space(1), 0, 0);
    quickAddLayout->setSpacing(utils::tokens::space(2));
    const QString fieldCss = QStringLiteral(
        "QLineEdit#%1 { padding: 0px %2px; min-height: 0px; border-radius: %3px; }"
        "QLineEdit#%1:focus { border: 1px solid %4; }")
        .arg(QStringLiteral("kvQuickField")).arg(utils::tokens::space(3))
        .arg(utils::tokens::radiusMdForHeight(kQuickH)).arg(utils::tokens::accent());
    m_quickAddKey = new QLineEdit(m_quickAddRow);
    m_quickAddValue = new QLineEdit(m_quickAddRow);
    for (QLineEdit *edit : {m_quickAddKey, m_quickAddValue}) {
        edit->setObjectName(QStringLiteral("kvQuickField"));
        edit->setFixedHeight(kQuickH);
        edit->setStyleSheet(fieldCss);
    }
    m_quickAddKey->setPlaceholderText(utils::tr(QStringLiteral("keyvalue.quick_add.key_placeholder")));
    m_quickAddValue->setPlaceholderText(utils::tr(QStringLiteral("keyvalue.quick_add.value_placeholder")));
    m_quickAddSave = new QPushButton(utils::tr(QStringLiteral("keyvalue.quick_add.save")), m_quickAddRow);
    m_quickAddSave->setIcon(LucideIcons::icon(QStringLiteral("check"), QColor(utils::tokens::fg()), 16));
    makeCompactButton(m_quickAddSave, kQuickH);
    connect(m_quickAddSave, &QPushButton::clicked, this, &KeyValueEditorWidget::handleQuickAddSave);
    connect(m_quickAddKey, &QLineEdit::returnPressed, this, &KeyValueEditorWidget::handleQuickAddSave);
    connect(m_quickAddValue, &QLineEdit::returnPressed, this, &KeyValueEditorWidget::handleQuickAddSave);
    quickAddLayout->addWidget(m_quickAddKey, 1);
    quickAddLayout->addWidget(m_quickAddValue, 2);
    quickAddLayout->addWidget(m_quickAddSave);
    m_quickAddRow->setVisible(false);
    layout->addWidget(m_quickAddRow);
}

void KeyValueEditorWidget::setValueColumnVisible(bool visible)
{
    m_valueColumnVisible = visible;
    m_table->setColumnHidden(kKvColValue, !visible);
    // A chave é a coluna elástica; o valor é Interactive; as ações são ResizeToContents (updateActionsColumnWidth).
    m_table->horizontalHeader()->setSectionResizeMode(kKvColKey, QHeaderView::Stretch);
    if (visible) {
        m_table->horizontalHeader()->setSectionResizeMode(kKvColValue, QHeaderView::Stretch);
    }
}

void KeyValueEditorWidget::setMonospaceFont(bool mono)
{
    m_monospaceFont = mono;
    rebuildTable();
}

void KeyValueEditorWidget::setSecretRevealEnabled(bool enabled)
{
    m_secretRevealEnabled = enabled;
    updateActionsColumnWidth();
    rebuildTable();
}

void KeyValueEditorWidget::setInlineAddRowEnabled(bool enabled)
{
    if (m_quickAddRow) {
        m_quickAddRow->setVisible(enabled);
    }
}

void KeyValueEditorWidget::handleQuickAddSave()
{
    const QString key = m_quickAddKey->text().trimmed();
    if (key.isEmpty()) {
        m_quickAddKey->setFocus();
        return;
    }
    m_rows.append(Row{key, m_quickAddValue->text(), false});
    m_quickAddKey->clear();
    m_quickAddValue->clear();
    m_quickAddKey->setFocus();
    rebuildTable();
}

void KeyValueEditorWidget::updateActionsColumnWidth()
{
    // Ações dimensionam pelo conteúdo real do cellWidget (2 ou 3 ícones conforme a linha): o cálculo manual cortava o último ícone ou comia o espaço do nome.
    m_table->horizontalHeader()->setSectionResizeMode(actionsColumn(), QHeaderView::ResizeToContents);
}

void KeyValueEditorWidget::setShowOwnAddButton(bool show)
{
    m_buttonsRow->setVisible(show);
}

void KeyValueEditorWidget::rebuildTable()
{
    m_table->setRowCount(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
        const Row &r = m_rows.at(row);
        auto makeItem = [this](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (m_monospaceFont) {
                // Nunca QFont(string CSS de fallback): o Qt trata a string inteira como um único nome de família. tokens::monoFont() monta via setFamilies().
                item->setFont(utils::tokens::monoFont());
            }
            return item;
        };
        m_table->setItem(row, kKvColKey, makeItem(r.key));
        // Secretos aparecem mascarados; com setSecretRevealEnabled(true) revelam linha a linha (m_revealedKeys).
        const bool revealed = m_secretRevealEnabled && m_revealedKeys.contains(r.key);
        const QString shownValue = (m_hasSecretColumn && r.secret && !revealed)
            ? QString(qMax(1, r.value.size()), QChar(0x2022))
            : r.value;
        m_table->setItem(row, kKvColValue, makeItem(shownValue));

        if (m_secretRevealEnabled && m_hasSecretColumn) {
            // Host próprio (3 ícones com o olho) em vez de makeRowActionsCell, que só monta dois.
            auto *host = new QWidget(m_table);
            host->setObjectName(QStringLiteral("tableCellHost"));
            host->setAttribute(Qt::WA_TranslucentBackground);
            host->setStyleSheet(QStringLiteral("background: transparent;"));
            auto *hostLayout = new QHBoxLayout(host);
            // Margem à direita: o último ícone (lixeira) encostava na borda da coluna e saía cortado.
            hostLayout->setContentsMargins(0, 0, utils::tokens::space(1), 0);
            hostLayout->setSpacing(utils::tokens::space(1));
            const QString key = r.key;
            if (!r.secret) {
                // Reserva o lugar do olho: lápis/lixeira alinham em todas as linhas.
                auto *spacer = new QWidget(host);
                spacer->setFixedSize(utils::tokens::iconButtonSize(), utils::tokens::iconButtonSize());
                hostLayout->addWidget(spacer);
            }
            auto *eyeButton = r.secret ? makeIconButton(host, revealed ? QStringLiteral("eye-off") : QStringLiteral("eye"),
                utils::tr(revealed ? QStringLiteral("keyvalue.secret.hide") : QStringLiteral("keyvalue.secret.reveal")),
                QColor(utils::tokens::mutedFg())) : nullptr;
            if (eyeButton) connect(eyeButton, &QToolButton::clicked, this, [this, key]() {
                if (m_revealedKeys.contains(key)) {
                    m_revealedKeys.remove(key);
                } else {
                    m_revealedKeys.insert(key);
                }
                rebuildTable();
            });
            if (eyeButton) hostLayout->addWidget(eyeButton, 0, Qt::AlignCenter);
            auto *rowActions = makeRowActionsCell(host,
                [this, row]() { if (editRow(row)) rebuildTable(); },
                [this, row]() { removeRowAt(row); });
            hostLayout->addWidget(rowActions);
            m_table->setCellWidget(row, actionsColumn(), host);
        } else {
            m_table->setCellWidget(row, actionsColumn(), makeRowActionsCell(m_table,
                [this, row]() { if (editRow(row)) rebuildTable(); },
                [this, row]() { removeRowAt(row); }));
        }
    }
    m_stack->setCurrentWidget(m_rows.isEmpty() ? static_cast<QWidget *>(m_emptyLabel) : m_table);
    emit changed();
}

void KeyValueEditorWidget::removeRowAt(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    m_rows.remove(row);
    rebuildTable();
}

void KeyValueEditorWidget::setValues(const QMap<QString, QString> &values)
{
    m_rows.clear();
    m_revealedKeys.clear();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        m_rows.append({it.key(), it.value(), false});
    }
    rebuildTable();
}

QMap<QString, QString> KeyValueEditorWidget::values() const
{
    QMap<QString, QString> result;
    for (const Row &r : m_rows) {
        const QString key = r.key.trimmed();
        if (!key.isEmpty()) {
            result[key] = r.value;
        }
    }
    return result;
}

bool KeyValueEditorWidget::editRow(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return false;
    }
    const Row &r = m_rows.at(row);
    KeyValueRowDialog dialog(r.key, r.value, r.secret, m_hasSecretColumn,
                             m_keyHeader, m_valueHeader, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    m_rows[row] = {dialog.key(), dialog.value(), dialog.secret()};
    return true;
}

void KeyValueEditorWidget::handleAddRowClicked()
{
    m_rows.append(Row{});
    const int row = m_rows.size() - 1;
    if (editRow(row)) {
        rebuildTable();
    } else {
        m_rows.remove(row);
    }
}

void KeyValueEditorWidget::enableSecretColumn(const QString &secretHeader)
{
    // Modo secreto: linhas podem ser marcadas como secretas (valor mascarado, toggle no formulário, botão de olho); não há coluna "Secreta".
    m_hasSecretColumn = true;
    m_secretHeader = secretHeader;
    updateActionsColumnWidth();
    rebuildTable();
}

void KeyValueEditorWidget::setValuesWithSecrets(const QMap<QString, QString> &values,
                                                const QSet<QString> &secretKeys)
{
    m_rows.clear();
    m_revealedKeys.clear();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        m_rows.append({it.key(), it.value(), secretKeys.contains(it.key())});
    }
    rebuildTable();
}

int KeyValueEditorWidget::mergeValues(const QMap<QString, QString> &values)
{
    int merged = 0;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        bool found = false;
        for (Row &row : m_rows) {
            if (row.key.trimmed() == it.key()) {
                row.value = it.value();
                found = true;
                break;
            }
        }
        if (!found) {
            m_rows.append({it.key(), it.value(), false});
        }
        ++merged;
    }
    rebuildTable();
    return merged;
}

QSet<QString> KeyValueEditorWidget::secretKeys() const
{
    QSet<QString> result;
    if (!m_hasSecretColumn) {
        return result;
    }
    for (const Row &r : m_rows) {
        const QString key = r.key.trimmed();
        if (!key.isEmpty() && r.secret) {
            result.insert(key);
        }
    }
    return result;
}

} // namespace kai::ui
