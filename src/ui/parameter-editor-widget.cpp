#include "ui/parameter-editor-widget.h"

#include "ui/table-utils.h"
#include "ui/dialog-utils.h"
#include "ui/env-var-autocomplete.h"
#include "core/config-manager.h"
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
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QColor>
#include <QCheckBox>

#include <algorithm>

namespace kai::ui {

namespace {
// A tabela é SOMENTE-LEITURA: exibe os parâmetros. SÓ 2 colunas — 0=Nome
// (puro, sem resumo extra — feedback do usuário: "deve apenas exibir o
// nome"), 1=Ações (lápis/lixeira inline). Label/Tipo/Default/Fonte NUNCA
// foram lidos de volta de lugar nenhum (editParameter() lê de m_params,
// não da tabela) — eram só colunas ESCONDIDAS sem função real, removidas.
// HISTÓRICO DO BUG (ícones duplicados, 3 rodadas de print): 1ª tentativa
// só desligou stretchLastSection; 2ª reordenou as colunas ocultas pra
// ficarem adjacentes; nenhuma bastou. A causa real nem era a ordem —
// era ter configuração de resize DIFERENTE de EnvExtractorsEditorWidget
// (que nunca teve o bug): esta tabela mexia explicitamente em
// setStretchLastSection/ResizeToContents da coluna de Ações, enquanto lá
// só a coluna de dado tem resize mode explícito (Ações fica no Interactive
// + largura fixa que o próprio configureTable já aplica). Copiado
// byte a byte agora — mesma configuração, mesmo resultado sem bug.
constexpr int kColName = 0;
constexpr int kColActions = 1;
constexpr int kColumnCount = 2;

// Carrega opções de um CSV para a string "rótulo:valor, valor, ...".
QString loadOptionsCsv(QWidget *parent)
{
    const QString path = QFileDialog::getOpenFileName(
        parent, utils::tr(QStringLiteral("params.csv_dialog.title")), QString(),
        utils::tr(QStringLiteral("params.csv_dialog.filter")));
    if (path.isEmpty()) {
        return QString();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream in(&file);
    QStringList normalized;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.contains(QLatin1Char(':'))) {
            const int sep = line.indexOf(QLatin1Char(':'));
            const QString label = line.left(sep).trimmed();
            const QString value = line.mid(sep + 1).trimmed();
            if (!value.isEmpty()) normalized << QStringLiteral("%1:%2").arg(label, value);
        } else {
            const QStringList cells = line.split(QLatin1Char(','));
            if (cells.size() == 2) {
                const QString label = cells.at(0).trimmed();
                const QString value = cells.at(1).trimmed();
                if (!value.isEmpty()) normalized << QStringLiteral("%1:%2").arg(label, value);
            } else {
                for (const QString &cell : cells) {
                    const QString key = cell.trimmed();
                    if (!key.isEmpty()) normalized << key;
                }
            }
        }
    }
    file.close();
    return normalized.join(QStringLiteral(", "));
}

// ============================================================================
// FORMULÁRIO CONTEXTUAL DE PARÂMETRO
// ----------------------------------------------------------------------------
// Traz TODOS os controles ricos que antes ficavam espremidos nas células:
//  - Nome, Label, Tipo, Default;
//  - Opções (com botão de importar CSV) — para tipo 'select';
//  - Fonte: Coleção + Campo exibido — liga um Select a uma coleção;
//  - Diretório inicial + botão de PASTA (file picker) — para tipo 'file'.
// Os grupos aparecem/desaparecem conforme o tipo escolhido.
// ============================================================================
class ParameterRowDialog : public QDialog {
public:
    ParameterRowDialog(const core::Parameter &param,
                       const QVector<core::Collection> &collections,
                       QWidget *parent)
        : QDialog(parent)
        , m_collections(collections)
    {
        setWindowTitle(utils::tr(QStringLiteral("params.edit_row")));
        setSizeGripEnabled(true);
        resize(480, 420);

        auto *outer = new QVBoxLayout(this);
        auto *form = new QFormLayout();
        outer->addLayout(form);

        m_name = new QLineEdit(param.name, this);
        m_name->setPlaceholderText(utils::tr(QStringLiteral("params.name.placeholder")));
        form->addRow(utils::tr(QStringLiteral("field.label.name")), m_name);

        m_label = new QLineEdit(param.label, this);
        form->addRow(utils::tr(QStringLiteral("field.label.label")), m_label);

        m_type = new QComboBox(this);
        m_type->addItems({QStringLiteral("text"), QStringLiteral("select"),
                          QStringLiteral("bool"), QStringLiteral("file"),
                          QStringLiteral("number"), QStringLiteral("textarea"),
                          QStringLiteral("json")});
        m_type->setCurrentText(core::parameterTypeToString(param.type));
        form->addRow(utils::tr(QStringLiteral("field.label.type")), m_type);

        m_default = new QLineEdit(param.defaultValue, this);
        form->addRow(utils::tr(QStringLiteral("field.label.default")), m_default);
        // Autocomplete de {{var}} (pedido do usuário) — QLineEdit simples
        // aqui (não InlineCodeField/QPlainTextEdit), único campo deste
        // diálogo onde faz sentido: o valor default pode referenciar uma
        // variável do environment ativo (ex: "{{API_URL}}/health").
        attachEnvVarAutocomplete(m_default, []() -> QStringList {
            const core::SettingsData settings = core::ConfigManager().loadSettings();
            for (const core::Environment &env : settings.environments) {
                if (env.id == settings.activeEnvironmentId) {
                    return env.vars.keys();
                }
            }
            return {};
        });

        // --- Opções (select) com importar CSV ---
        auto *optionsRow = new QHBoxLayout();
        m_options = new QLineEdit(param.options.join(QStringLiteral(", ")), this);
        m_options->setPlaceholderText(utils::tr(QStringLiteral("params.options.placeholder")));
        optionsRow->addWidget(m_options, 1);
        auto *csvButton = makeIconButton(this, QStringLiteral("file-code"),
            utils::tr(QStringLiteral("params.options.load_csv.tip")), QColor(utils::tokens::accent()));
        connect(csvButton, &QToolButton::clicked, this, [this]() {
            const QString loaded = loadOptionsCsv(this);
            if (!loaded.isEmpty()) m_options->setText(loaded);
        });
        optionsRow->addWidget(csvButton);
        m_optionsLabel = new QLabel(utils::tr(QStringLiteral("params.options.label")), this);
        form->addRow(m_optionsLabel, wrapRow(optionsRow));

        // MULTI-SELECT (feedback do usuário): permite marcar várias opções
        // fixas no form de run. Só faz sentido para Select de opções fixas
        // (sem coleção — a coleção já tem sua própria tela multi-select).
        m_multiSelect = new QCheckBox(utils::tr(QStringLiteral("params.multi_select")), this);
        // Toggle de formulário (liga/desliga multi-seleção pro parâmetro),
        // não item de checklist — varredura de consistência (Parte 3):
        // kaiRole="switch". Não confundir com a LISTA de opções marcáveis
        // do form de execução (ParameterFormDialog, multi-select em si):
        // aquela É um checklist "selecione vários itens" e continua
        // QCheckBox comum, propositalmente.
        m_multiSelect->setProperty("kaiRole", QStringLiteral("switch"));
        m_multiSelect->setToolTip(utils::tr(QStringLiteral("params.multi_select.tip")));
        m_multiSelect->setChecked(param.multiSelect);
        m_multiSelectLabel = new QLabel(QString(), this);
        form->addRow(m_multiSelectLabel, m_multiSelect);

        // --- Fonte: Coleção + Campo exibido ---
        m_collectionCombo = new QComboBox(this);
        m_collectionCombo->addItem(utils::tr(QStringLiteral("params.collection.none")), QString());
        for (const core::Collection &c : m_collections) {
            m_collectionCombo->addItem(c.name, c.id);
        }
        {
            const int idx = m_collectionCombo->findData(param.collectionId);
            m_collectionCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        makeSearchableCombo(m_collectionCombo);
        m_collectionLabel = new QLabel(utils::tr(QStringLiteral("params.collection.source_label")), this);
        form->addRow(m_collectionLabel, m_collectionCombo);

        m_displayCombo = new QComboBox(this);
        m_displayCombo->setEditable(true);
        m_displayField = new QLabel(utils::tr(QStringLiteral("params.display_field.label")), this);
        form->addRow(m_displayField, m_displayCombo);

        // --- Diretório inicial + PASTA (file picker) ---
        auto *dirRow = new QHBoxLayout();
        m_initialDir = new QLineEdit(param.initialDir, this);
        m_initialDir->setPlaceholderText(utils::tr(QStringLiteral("params.initial_dir.placeholder")));
        m_initialDir->setToolTip(utils::tr(QStringLiteral("params.initial_dir.tip")));
        dirRow->addWidget(m_initialDir, 1);
        auto *browseButton = makeIconButton(this, QStringLiteral("folder-open"),
            utils::tr(QStringLiteral("params.initial_dir.browse")), QColor(utils::tokens::accent()));
        connect(browseButton, &QToolButton::clicked, this, [this]() {
            const QString chosen = QFileDialog::getExistingDirectory(
                this, utils::tr(QStringLiteral("params.initial_dir.browse")), m_initialDir->text());
            if (!chosen.isEmpty()) m_initialDir->setText(chosen);
        });
        dirRow->addWidget(browseButton);
        m_dirLabel = new QLabel(utils::tr(QStringLiteral("params.col.initial_dir")), this);
        form->addRow(m_dirLabel, wrapRow(dirRow));

        // FORMATO DO PATH devolvido (pedido do usuário: "flag dinâmica pro
        // usuário escolher o tipo de path que ele quer que o filepick
        // retorne ao terminal, ex: windows, linux") — o diálogo nativo
        // devolve no formato do SO do Kai, que sob WSL/WSLg costuma ser um
        // path Windows inútil pra colar direto num comando bash do lado
        // Linux.
        m_filePathFormat = new QComboBox(this);
        m_filePathFormat->addItem(utils::tr(QStringLiteral("params.path_format.native")), QStringLiteral("native"));
        m_filePathFormat->addItem(utils::tr(QStringLiteral("params.path_format.posix")), QStringLiteral("posix"));
        m_filePathFormat->addItem(utils::tr(QStringLiteral("params.path_format.windows")), QStringLiteral("windows"));
        {
            const int idx = m_filePathFormat->findData(
                param.filePathFormat.isEmpty() ? QStringLiteral("native") : param.filePathFormat);
            m_filePathFormat->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        m_filePathFormatLabel = new QLabel(utils::tr(QStringLiteral("params.path_format.label")), this);
        form->addRow(m_filePathFormatLabel, m_filePathFormat);

        // PASTA em vez de arquivo (feedback do usuário: "às vezes o param é
        // uma pasta") — troca o seletor de arquivo por getExistingDirectory
        // no formulário de execução, sem virar um ParameterType novo.
        m_pickFolder = new QCheckBox(utils::tr(QStringLiteral("params.pick_folder")), this);
        m_pickFolder->setProperty("kaiRole", QStringLiteral("switch"));
        m_pickFolder->setToolTip(utils::tr(QStringLiteral("params.pick_folder.tip")));
        m_pickFolder->setChecked(param.pickFolder);
        m_pickFolderLabel = new QLabel(QString(), this);
        form->addRow(m_pickFolderLabel, m_pickFolder);

        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        stripDialogButtonIcons(box);
        connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
        outer->addWidget(box);

        auto refreshDisplayFields = [this]() {
            const QString colId = m_collectionCombo->currentData().toString();
            const QString current = m_displayCombo->currentText();
            m_displayCombo->clear();
            m_displayCombo->setEnabled(!colId.isEmpty());
            const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
                [&colId](const core::Collection &c) { return c.id == colId; });
            if (it != m_collections.constEnd()) {
                for (const core::CollectionField &f : it->schema) m_displayCombo->addItem(f.name);
            }
            if (!current.isEmpty()) m_displayCombo->setCurrentText(current);
        };
        refreshDisplayFields();
        if (!param.collectionDisplayField.isEmpty()) {
            m_displayCombo->setCurrentText(param.collectionDisplayField);
        }
        connect(m_collectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [refreshDisplayFields](int) { refreshDisplayFields(); });

        auto applyTypeVisibility = [this]() {
            const QString t = m_type->currentText();
            const bool isSelect = (t == QStringLiteral("select"));
            const bool isFile = (t == QStringLiteral("file"));
            m_optionsLabel->setVisible(isSelect);
            m_options->parentWidget()->setVisible(isSelect);
            m_collectionLabel->setVisible(isSelect);
            m_collectionCombo->setVisible(isSelect);
            m_displayField->setVisible(isSelect);
            m_displayCombo->setVisible(isSelect);
            // Multi-select: só para Select de opções fixas (sem coleção).
            const bool hasCollection = !m_collectionCombo->currentData().toString().isEmpty();
            m_multiSelect->setVisible(isSelect && !hasCollection);
            m_multiSelectLabel->setVisible(isSelect && !hasCollection);
            m_dirLabel->setVisible(isFile);
            m_initialDir->parentWidget()->setVisible(isFile);
            m_filePathFormatLabel->setVisible(isFile);
            m_filePathFormat->setVisible(isFile);
            m_pickFolderLabel->setVisible(isFile);
            m_pickFolder->setVisible(isFile);
        };
        applyTypeVisibility();
        connect(m_type, &QComboBox::currentTextChanged, this, [applyTypeVisibility](const QString &) {
            applyTypeVisibility();
        });
        connect(m_collectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [applyTypeVisibility](int) { applyTypeVisibility(); });

        centerOnParent(this);
    }

    core::Parameter result() const
    {
        core::Parameter p;
        p.name = m_name->text().trimmed();
        p.label = m_label->text();
        p.type = core::parameterTypeFromString(m_type->currentText());
        p.defaultValue = m_default->text();
        for (const QString &opt : m_options->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            p.options << opt.trimmed();
        }
        p.collectionId = m_collectionCombo->currentData().toString();
        p.collectionDisplayField = m_displayCombo->currentText().trimmed();
        // Multi-select só vale para Select de opções fixas (sem coleção).
        p.multiSelect = (p.type == core::ParameterType::Select)
                        && p.collectionId.isEmpty()
                        && m_multiSelect->isChecked();
        p.initialDir = m_initialDir->text().trimmed();
        p.filePathFormat = m_filePathFormat->currentData().toString();
        p.pickFolder = m_pickFolder->isChecked();
        return p;
    }

private:
    // Envolve um layout de linha (campo + botão) num QWidget para o QFormLayout.
    QWidget *wrapRow(QLayout *inner)
    {
        auto *w = new QWidget(this);
        inner->setContentsMargins(0, 0, 0, 0);
        w->setLayout(inner);
        return w;
    }

    QVector<core::Collection> m_collections;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_label = nullptr;
    QComboBox *m_type = nullptr;
    QLineEdit *m_default = nullptr;
    QLineEdit *m_options = nullptr;
    QLabel *m_optionsLabel = nullptr;
    QComboBox *m_collectionCombo = nullptr;
    QLabel *m_collectionLabel = nullptr;
    QComboBox *m_displayCombo = nullptr;
    QLabel *m_displayField = nullptr;
    QLineEdit *m_initialDir = nullptr;
    QLabel *m_dirLabel = nullptr;
    QComboBox *m_filePathFormat = nullptr;
    QLabel *m_filePathFormatLabel = nullptr;
    QCheckBox *m_multiSelect = nullptr;
    QLabel *m_multiSelectLabel = nullptr;
    QCheckBox *m_pickFolder = nullptr;
    QLabel *m_pickFolderLabel = nullptr;
};

} // namespace

ParameterEditorWidget::ParameterEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ParameterEditorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(0, kColumnCount, this);
    configureTable(m_table, {
        {utils::tr(QStringLiteral("field.label.name")), 320, true},
        {QString(), rowActionsColumnWidth(), false},
    });
    // MESMA configuração, byte a byte, de EnvExtractorsEditorWidget/
    // ExecutionConditionsEditorWidget (widgets sem o bug de ícone
    // duplicado, confirmado no print) — só a coluna de dado em Stretch;
    // Ações fica no Interactive+largura fixa que o próprio configureTable
    // já aplica, sem setStretchLastSection nem ResizeToContents extras
    // (2 tentativas anteriores mexendo nisso não resolveram; esta 3ª
    // elimina a diferença em vez de ajustar mais parâmetros de resize).
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setVisible(false);
    // stretchLastSection (ligado por configureTable) + Stretch explícito no
    // Nome faziam as DUAS colunas competirem pelo espaço sobrando — Qt
    // dividia ~metade/metade em vez de Ações ficar pequena (confirmado
    // com screenshot do widget renderizado isolado, sem tema: a coluna de
    // Ações ocupava metade da largura, com o pencil/trash centralizados
    // longe da borda). Desligar aqui faz só o Nome esticar de verdade.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    m_table->setMinimumHeight(120);
    // Duplo-clique numa linha abre o formulário daquela linha.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_params.size() && editParameter(row)) {
            rebuildTable();
        }
    });

    layout->addWidget(m_table);
}

void ParameterEditorWidget::rebuildTable()
{
    // ZERA antes de repopular (causa real do ícone fantasma, confirmada
    // reproduzindo o widget isolado offscreen): rebuildTable() é chamado
    // MAIS DE UMA VEZ pro mesmo widget vivo (CommandEditorDialog chama
    // setParameters() no construtor E de novo em setAvailableCollections).
    // Com a contagem de linhas IGUAL nas duas vezes, setRowCount(N) não
    // muda nada e o setCellWidget() da 2ª chamada, embora devesse
    // substituir o widget antigo, deixava um "fantasma" dele sobrando —
    // setRowCount(0) força o Qt a soltar de vez os cell widgets de toda
    // linha antes de recriá-las do zero.
    m_table->setRowCount(0);
    m_table->setRowCount(m_params.size());
    for (int row = 0; row < m_params.size(); ++row) {
        const core::Parameter &p = m_params.at(row);

        // SÓ o nome (feedback do usuário: "deve apenas exibir o nome") —
        // sem resumo extra embutido.
        auto *nameItem = new QTableWidgetItem(p.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColName, nameItem);

        // Ações inline (lápis/lixeira) — cada linha já sabe seu próprio
        // índice, sem precisar de seleção prévia (mockup enviado pelo
        // usuário).
        m_table->setCellWidget(row, kColActions, makeRowActionsCell(m_table,
            [this, row]() { if (editParameter(row)) rebuildTable(); },
            [this, row]() { removeParameterAt(row); }));
    }
    emit changed();
}

void ParameterEditorWidget::removeParameterAt(int row)
{
    if (row < 0 || row >= m_params.size()) {
        return;
    }
    m_params.remove(row);
    rebuildTable();
}

void ParameterEditorWidget::setAvailableCollections(const QVector<core::Collection> &collections)
{
    m_collections = collections;
}

void ParameterEditorWidget::setParameters(const QVector<core::Parameter> &params)
{
    m_params = params;
    rebuildTable();
}

QVector<core::Parameter> ParameterEditorWidget::parameters() const
{
    // Fonte de verdade é o modelo; ignora entradas sem nome (não referenciáveis).
    QVector<core::Parameter> result;
    for (const core::Parameter &p : m_params) {
        if (!p.name.trimmed().isEmpty()) {
            result << p;
        }
    }
    return result;
}

bool ParameterEditorWidget::editParameter(int row)
{
    if (row < 0 || row >= m_params.size()) {
        return false;
    }
    ParameterRowDialog dialog(m_params.at(row), m_collections, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    m_params[row] = dialog.result();
    return true;
}

void ParameterEditorWidget::handleAddRowClicked()
{
    // Adiciona um parâmetro e abre o formulário direto para preenchê-lo.
    m_params.append(core::Parameter{});
    const int row = m_params.size() - 1;
    if (editParameter(row)) {
        rebuildTable();
    } else {
        // Cancelou a criação: remove a linha vazia.
        m_params.remove(row);
    }
}

void ParameterEditorWidget::handleEditRowClicked()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    if (editParameter(row)) {
        rebuildTable();
    }
}

void ParameterEditorWidget::handleRemoveRowClicked()
{
    // Sem uso interno desde que a exclusão virou um ícone inline por linha
    // (ver rebuildTable/removeParameterAt) — mantido público só por
    // compatibilidade de API; remove a linha atualmente selecionada, se
    // houver.
    removeParameterAt(m_table->currentRow());
}

} // namespace kai::ui
