#include "ui/key-value-editor-widget.h"

#include "ui/table-utils.h"
#include "ui/dialog-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "ui/lucide-icons.h"

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
#include <QAbstractItemView>
#include <QColor>
#include <QFont>
#include <QPushButton>

namespace kai::ui {

namespace {
// Tabela SOMENTE-LEITURA. 0=Chave, 1=Valor, 2=Ações (lápis/lixeira inline —
// ver ParameterEditorWidget::rebuildTable). Não há coluna "Secreta": o modo
// secreto (enableSecretColumn) só habilita máscara/olho no valor, sem uma
// coluna Sim/— própria (removida a pedido do usuário).
constexpr int kKvColKey = 0;
constexpr int kKvColValue = 1;

// ============================================================================
// FORMULÁRIO CONTEXTUAL de par chave-valor.
// Campo de VALOR EXPANSÍVEL (QPlainTextEdit) — valores de env costumam ser
// longos (URLs, tokens, JSON). No modo secreto, um toggle "mascarar" e o valor
// nasce oculto. Botão de "revelar" para conferir sem sair do form.
// ============================================================================
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

        // Valor EXPANSÍVEL (multi-linha): acomoda URLs, tokens e JSON longos.
        m_value = new QPlainTextEdit(value, this);
        m_value->setPlaceholderText(utils::tr(QStringLiteral("keyvalue.value.placeholder")));
        m_value->setMinimumHeight(utils::tokens::space(20));
        form->addRow(valueLabel, m_value);

        if (hasSecret) {
            auto *secretRow = new QHBoxLayout();
            m_secret = new QCheckBox(utils::tr(QStringLiteral("keyvalue.secret_field")), this);
            // Toggle de formulário (dentro do diálogo de edição de uma
            // linha, não uma checkbox de tabela densa) — varredura de
            // consistência (Parte 3): kaiRole="switch".
            m_secret->setProperty("kaiRole", QStringLiteral("switch"));
            m_secret->setChecked(secret);
            secretRow->addWidget(m_secret);
            secretRow->addStretch();
            auto *w = new QWidget(this);
            w->setLayout(secretRow);
            form->addRow(QString(), w);
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
    // Só a coluna "chave" (o nome) fica visível por padrão — a edição de
    // valor/secreto é pelo formulário (feedback do usuário).
    m_table->setColumnHidden(kKvColValue, true);
    m_table->horizontalHeader()->setSectionResizeMode(kKvColKey, QHeaderView::Stretch);
    // A CHAVE é a coluna elástica (Stretch) e a de AÇÕES tem largura FIXA
    // própria: sem isto (com stretchLastSection ligado pelo configureTable),
    // a última coluna — ações — tentava esticar e a soma das larguras
    // estourava a viewport, empurrando os botões inline (lápis/lixeira/olho)
    // para FORA da área visível (relatado). Com a chave absorvendo a sobra,
    // a coluna de ações mantém sua largura e os botões sempre cabem.
    m_table->horizontalHeader()->setStretchLastSection(false);
    // Sem header nem coluna de seleção (mesmo padrão de
    // ParameterEditorWidget/OutputRespondersEditorWidget): só a coluna de
    // dado + ações inline.
    m_table->horizontalHeader()->setVisible(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    // Altura mínima de 2 linhas (era 3 + folga): com 1 variável a tabela
    // não precisa reservar espaço de 3 linhas — o excesso, somado à barra,
    // fazia aparecer scroll vertical à toa (relatado). O corpo cresce com
    // as linhas.
    m_table->setMinimumHeight(2 * standardRowHeight());
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

    // Só o "+" continua na barra inferior (compacto, à direita) — editar/
    // excluir viraram ícones inline por linha. Mantido aqui (em vez de só
    // no cabeçalho de um card, como Params/Responders) porque este widget
    // também é usado SEM card em volta (Folder/Environment) — o botão
    // próprio garante que "adicionar" continua acessível nesses casos.
    m_buttonsRow = new QWidget(this);
    auto *buttonsLayout = new QHBoxLayout(m_buttonsRow);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->addStretch();
    m_addButton = makeAddButton(m_buttonsRow, utils::tr(QStringLiteral("keyvalue.add")));
    connect(m_addButton, &QToolButton::clicked, this, &KeyValueEditorWidget::handleAddRowClicked);
    buttonsLayout->addWidget(m_addButton);

    layout->addWidget(m_table);
    layout->addWidget(m_buttonsRow);

    // --- Linha de "adição rápida" (opt-in, ver setInlineAddRowEnabled) ---
    // Criada aqui mas escondida por padrão: nasce junto pra já existir se o
    // dono ligar a opção depois de setValues(), sem depender de ordem de
    // chamada.
    m_quickAddRow = new QWidget(this);
    m_quickAddRow->setObjectName(QStringLiteral("kvQuickAddRow"));
    m_quickAddRow->setAttribute(Qt::WA_StyledBackground, true);
    // Borda TRACEJADA na cor de accent (mockup do usuário) — qualificada por
    // objectName, nunca um setStyleSheet cru (ver nota em dialog-utils.h
    // sobre o bug de cascata em descendentes).
    m_quickAddRow->setStyleSheet(QStringLiteral(
        "QWidget#kvQuickAddRow { border: 1px dashed %1; border-radius: %2px; }")
        .arg(utils::tokens::accent()).arg(utils::tokens::radiusMd()));
    auto *quickAddLayout = new QHBoxLayout(m_quickAddRow);
    quickAddLayout->setContentsMargins(utils::tokens::space(2), utils::tokens::space(1),
                                       utils::tokens::space(2), utils::tokens::space(1));
    m_quickAddKey = new QLineEdit(m_quickAddRow);
    m_quickAddKey->setPlaceholderText(utils::tr(QStringLiteral("keyvalue.quick_add.key_placeholder")));
    m_quickAddValue = new QLineEdit(m_quickAddRow);
    m_quickAddValue->setPlaceholderText(utils::tr(QStringLiteral("keyvalue.quick_add.value_placeholder")));
    m_quickAddSave = new QPushButton(utils::tr(QStringLiteral("keyvalue.quick_add.save")), m_quickAddRow);
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
    // A CHAVE (nome da variável) é a coluna elástica (Stretch) — o nome tem
    // prioridade de espaço (relatado: nome ficando apertado). O VALOR fica
    // Interactive (o usuário arrasta se quiser mais/menos). A coluna de
    // AÇÕES é ResizeToContents (updateActionsColumnWidth), então nunca
    // rouba espaço nem corta os ícones.
    m_table->horizontalHeader()->setSectionResizeMode(kKvColKey, QHeaderView::Stretch);
    if (visible) {
        m_table->horizontalHeader()->setSectionResizeMode(kKvColValue, QHeaderView::Interactive);
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
    // A coluna de ações dimensiona pelo CONTEÚDO REAL do cellWidget
    // (ResizeToContents): o Qt mede o host de ícones (2 ou 3 conforme a
    // linha) e dá a largura exata — nunca corta o último ícone (lixeira) e
    // não reserva espaço demais (que roubava o nome da variável). Substitui
    // o cálculo manual de largura, que era frágil e ora cortava, ora comia
    // o espaço do nome (relatado nas duas direções).
    m_table->horizontalHeader()->setSectionResizeMode(actionsColumn(), QHeaderView::ResizeToContents);
}

void KeyValueEditorWidget::setShowOwnAddButton(bool show)
{
    m_buttonsRow->setVisible(show);
}

void KeyValueEditorWidget::rebuildTable()
{
    m_table->setRowCount(m_rows.size());
    QFont monoFont(utils::tokens::monoFamily());
    for (int row = 0; row < m_rows.size(); ++row) {
        const Row &r = m_rows.at(row);
        auto makeItem = [this](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (m_monospaceFont) {
                item->setFont(QFont(utils::tokens::monoFamily()));
            }
            return item;
        };
        m_table->setItem(row, kKvColKey, makeItem(r.key));
        // Valores secretos aparecem mascarados na tabela por padrão; com
        // setSecretRevealEnabled(true), o usuário pode revelar linha a linha
        // (m_revealedKeys) sem abrir o formulário contextual.
        const bool revealed = m_secretRevealEnabled && m_revealedKeys.contains(r.key);
        const QString shownValue = (m_hasSecretColumn && r.secret && !revealed)
            ? QString(qMax(1, r.value.size()), QChar(0x2022))
            : r.value;
        m_table->setItem(row, kKvColValue, makeItem(shownValue));

        if (m_secretRevealEnabled && m_hasSecretColumn && r.secret) {
            // Cabe o terceiro ícone (olho) além de lápis/lixeira — host
            // próprio em vez de makeRowActionsCell (que só monta dois).
            auto *host = new QWidget(m_table);
            host->setObjectName(QStringLiteral("tableCellHost"));
            host->setAttribute(Qt::WA_TranslucentBackground);
            host->setStyleSheet(QStringLiteral("background: transparent;"));
            auto *hostLayout = new QHBoxLayout(host);
            // Margem à DIREITA: o último ícone (lixeira) encostava na borda
            // da coluna e saía cortado (relatado com print). O respiro aqui
            // garante a folga mesmo que a largura da coluna varie.
            hostLayout->setContentsMargins(0, 0, utils::tokens::space(1), 0);
            hostLayout->setSpacing(utils::tokens::space(1));
            const QString key = r.key;
            auto *eyeButton = makeIconButton(host, revealed ? QStringLiteral("eye-off") : QStringLiteral("eye"),
                utils::tr(revealed ? QStringLiteral("keyvalue.secret.hide") : QStringLiteral("keyvalue.secret.reveal")),
                QColor(utils::tokens::mutedFg()));
            connect(eyeButton, &QToolButton::clicked, this, [this, key]() {
                if (m_revealedKeys.contains(key)) {
                    m_revealedKeys.remove(key);
                } else {
                    m_revealedKeys.insert(key);
                }
                rebuildTable();
            });
            hostLayout->addWidget(eyeButton, 0, Qt::AlignCenter);
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
    m_revealedKeys.clear(); // troca de conteúdo: não vaza reveal de outro dono/pacote
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
    // "Modo secreto" ligado: as linhas PODEM ser marcadas como secretas
    // (valor mascarado na tabela + toggle no formulário + botão de olho).
    // NÃO cria mais uma coluna "Secreta" (Sim/—) na tabela — removida a
    // pedido do usuário; o estado secreto já é visível pelo mascaramento
    // do valor e pelo ícone de olho na coluna de ações.
    m_hasSecretColumn = true;
    m_secretHeader = secretHeader;
    updateActionsColumnWidth();
    rebuildTable();
}

void KeyValueEditorWidget::setValuesWithSecrets(const QMap<QString, QString> &values,
                                                const QSet<QString> &secretKeys)
{
    m_rows.clear();
    m_revealedKeys.clear(); // troca de pacote/ambiente: não vaza reveal do anterior
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        m_rows.append({it.key(), it.value(), secretKeys.contains(it.key())});
    }
    rebuildTable();
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
