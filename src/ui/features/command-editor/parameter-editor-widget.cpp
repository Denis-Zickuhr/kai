#include "ui/features/command-editor/parameter-editor-widget.h"

#include "ui/shared/table-utils.h"
#include "ui/shared/draggable-table-widget.h"
#include "ui/shared/dialog-utils.h"
#include "ui/features/environments/env-var-autocomplete.h"
#include "core/config-manager.h"
#include "core/date-param-format.h"
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
#include <QComboBox>
#include <QCompleter>
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
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QFont>

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
// Coluna de indicativo visual de arrastar (pedido do usuário: "a
// ordenação de params ficou bem bugada, coloque um indicativo visual que
// dá pra reordenar") — ver makeDragHandleCell em table-utils.
constexpr int kColDragHandle = 0;
constexpr int kColName = 1;
constexpr int kColActions = 2;
constexpr int kColumnCount = 3;

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
                       const QStringList &existingGroups,
                       QWidget *parent)
        : QDialog(parent)
        , m_collections(collections)
    {
        setWindowTitle(utils::tr(QStringLiteral("params.edit_row")));
        setSizeGripEnabled(true);

        auto *outer = new QVBoxLayout(this);

        // Rolagem interna + separação em seções (achado real, com print:
        // "ela TEM MUITA informação, pode separar algumas coisas em
        // grupo... preciso a mesmo tratamento de espaço, responsividade e
        // tamanho da tela de inclusão de params" — a mesma diretriz já
        // aplicada em ParameterFormDialog). 19 campos empilhados sem
        // nenhuma respiração viravam uma parede só; aqui não dá pra usar
        // CollapsibleSectionCard igual lá (os campos específicos de tipo já
        // têm sua PRÓPRIA lógica de mostrar/esconder via applyTypeVisibility,
        // que manipula os widgets diretamente — duplicar isso num card
        // colapsável seria complexidade desnecessária pra um formulário de
        // edição só, usado pelo autor do comando, não repetido feito o de
        // preenchimento). Em vez disso: legendas de seção simples, sempre
        // visíveis, dentro da MESMA QFormLayout.
        auto *scrollContent = new QWidget(this);
        scrollContent->setObjectName(QStringLiteral("paramRowScrollHost"));
        scrollContent->setStyleSheet(QStringLiteral(
            "QWidget#paramRowScrollHost { background: transparent; }"));
        auto *contentLayout = new QVBoxLayout(scrollContent);
        contentLayout->setContentsMargins(0, 0, 0, 0);

        auto *form = new QFormLayout();
        form->setVerticalSpacing(utils::tokens::space(3));
        contentLayout->addLayout(form);

        auto *scrollArea = new QScrollArea(this);
        scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
        scrollArea->setWidget(scrollContent);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Mesmo achado do form de preenchimento: um "background: transparent"
        // CRU (sem seletor) no viewport quebra a cascata de QSS pra todo
        // campo descendente (ver comentário em ParameterFormDialog::setupUi)
        // — qualificado por objectName aqui também.
        scrollArea->viewport()->setObjectName(QStringLiteral("paramRowScrollViewport"));
        scrollArea->viewport()->setStyleSheet(QStringLiteral(
            "QWidget#paramRowScrollViewport { background: transparent; }"));
        outer->addWidget(scrollArea, 1);

        // Legenda de seção — QLabel discreto (maiúsculas, cor muted),
        // ocupando a linha inteira do form.
        auto addSectionCaption = [&](const QString &text) {
            auto *caption = new QLabel(text.toUpper(), this);
            QFont f = caption->font();
            f.setBold(true);
            f.setPointSize(qMax(f.pointSize() - 1, 7));
            caption->setStyleSheet(QStringLiteral("color: %1; letter-spacing: 1px;")
                .arg(utils::tokens::mutedFg()));
            form->addRow(caption);
            return caption;
        };

        m_name = new QLineEdit(param.name, this);
        m_name->setPlaceholderText(utils::tr(QStringLiteral("params.name.placeholder")));
        form->addRow(utils::tr(QStringLiteral("field.label.name")), m_name);

        m_label = new QLineEdit(param.label, this);
        form->addRow(utils::tr(QStringLiteral("field.label.label")), m_label);

        addSectionCaption(utils::tr(QStringLiteral("params.section.organization")));

        // GRUPO (pedido do usuário: "função opcional para agrupar
        // parâmetros... basta dar um nome, os com o mesmo nome são
        // carregados dentro da própria caixinha colapsada por default").
        // Opcional — vazio (padrão) renderiza o parâmetro direto no form,
        // como sempre. Combo EDITÁVEL (feedback do usuário: "ficou muito
        // solto... um select livre ia ser perfeito, aceita texto livre, mas
        // permite escolher entre as opções já usadas naquele cmd") — texto
        // livre pra criar um grupo novo, com os nomes já usados nOS OUTROS
        // parâmetros deste MESMO comando como sugestões (evita o typo
        // clássico de agrupamento por nome: "Acesso" numa linha e "acesso"
        // ou "Aceso" noutra viram DOIS grupos sem querer).
        m_group = new QComboBox(this);
        m_group->setEditable(true);
        m_group->setInsertPolicy(QComboBox::NoInsert);
        m_group->addItem(QString()); // opção vazia = sem grupo
        m_group->addItems(existingGroups);
        if (auto *completer = m_group->completer()) {
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            completer->setCompletionMode(QCompleter::PopupCompletion);
        }
        m_group->setCurrentText(param.group);
        m_group->lineEdit()->setPlaceholderText(utils::tr(QStringLiteral("params.group.placeholder")));
        m_group->setToolTip(utils::tr(QStringLiteral("params.group.tip")));
        form->addRow(utils::tr(QStringLiteral("params.group.label")), m_group);

        // DESCRIÇÃO (feature CLI Paths): não aparece em lugar nenhum da GUI
        // (o `label` já basta ali) — só existe pro `--help` de um CLI Path
        // ter mais contexto do que um rótulo de 2 palavras dá.
        m_description = new QLineEdit(param.description, this);
        m_description->setPlaceholderText(utils::tr(QStringLiteral("params.description.placeholder")));
        m_description->setToolTip(utils::tr(QStringLiteral("params.description.tip")));
        form->addRow(utils::tr(QStringLiteral("params.description.label")), m_description);

        addSectionCaption(utils::tr(QStringLiteral("params.section.type_and_value")));

        m_type = new QComboBox(this);
        m_type->addItems({QStringLiteral("text"), QStringLiteral("select"),
                          QStringLiteral("bool"), QStringLiteral("file"),
                          QStringLiteral("number"), QStringLiteral("textarea"),
                          QStringLiteral("json"), QStringLiteral("date")});
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

        QLabel *typeConfigCaption = addSectionCaption(utils::tr(QStringLiteral("params.section.type_config")));

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

        // MODO DE SELEÇÃO (feedback do usuário: "não gostei da cfg pick as
        // folder, queria tipo um select com modo de seleção... arquivo,
        // pastas ou ambos") — substitui o antigo checkbox binário
        // "pick_folder" por um combo de 3 opções; "Ambos" deixa a escolha
        // arquivo-ou-pasta pro momento em que o usuário clica em procurar
        // no formulário de execução (ver ParameterFormDialog).
        m_pickMode = new QComboBox(this);
        m_pickMode->addItem(utils::tr(QStringLiteral("params.pick_mode.file")), QStringLiteral("file"));
        m_pickMode->addItem(utils::tr(QStringLiteral("params.pick_mode.folder")), QStringLiteral("folder"));
        m_pickMode->addItem(utils::tr(QStringLiteral("params.pick_mode.both")), QStringLiteral("both"));
        m_pickMode->setToolTip(utils::tr(QStringLiteral("params.pick_mode.tip")));
        {
            const int idx = m_pickMode->findData(param.pickMode.isEmpty() ? QStringLiteral("file") : param.pickMode);
            m_pickMode->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        m_pickModeLabel = new QLabel(utils::tr(QStringLiteral("params.pick_mode.label")), this);
        form->addRow(m_pickModeLabel, m_pickMode);

        // --- Campos de type == date (pedido do usuário: "opções de janela
        // de formatado de data, se é hora ou só data ou data hora, se é
        // range, além de formatador de paste no CMD, select com formatos e
        // opção custom") ---
        m_dateMode = new QComboBox(this);
        m_dateMode->addItem(utils::tr(QStringLiteral("params.date_mode.date")), QStringLiteral("date"));
        m_dateMode->addItem(utils::tr(QStringLiteral("params.date_mode.time")), QStringLiteral("time"));
        m_dateMode->addItem(utils::tr(QStringLiteral("params.date_mode.datetime")), QStringLiteral("datetime"));
        {
            const int idx = m_dateMode->findData(param.dateMode.isEmpty() ? QStringLiteral("date") : param.dateMode);
            m_dateMode->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        m_dateModeLabel = new QLabel(utils::tr(QStringLiteral("params.date_mode.label")), this);
        form->addRow(m_dateModeLabel, m_dateMode);

        m_dateRange = new QCheckBox(utils::tr(QStringLiteral("params.date_range")), this);
        m_dateRange->setProperty("kaiRole", QStringLiteral("switch"));
        m_dateRange->setToolTip(utils::tr(QStringLiteral("params.date_range.tip")));
        m_dateRange->setChecked(param.dateRange);
        m_dateRangeLabel = new QLabel(QString(), this);
        form->addRow(m_dateRangeLabel, m_dateRange);

        m_dateFormat = new QComboBox(this);
        for (const QString &key : core::dateFormatPresetKeys()) {
            m_dateFormat->addItem(core::dateFormatPresetLabel(key), key);
        }
        m_dateFormat->setToolTip(utils::tr(QStringLiteral("params.date_format.tip")));
        {
            const int idx = m_dateFormat->findData(param.dateFormat.isEmpty() ? QStringLiteral("iso_date") : param.dateFormat);
            m_dateFormat->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        m_dateFormatLabel = new QLabel(utils::tr(QStringLiteral("params.date_format.label")), this);
        form->addRow(m_dateFormatLabel, m_dateFormat);

        m_dateFormatCustom = new QLineEdit(param.dateFormatCustom, this);
        m_dateFormatCustom->setPlaceholderText(utils::tr(QStringLiteral("params.date_format_custom.placeholder")));
        m_dateFormatCustom->setToolTip(utils::tr(QStringLiteral("params.date_format_custom.tip")));
        m_dateFormatCustomLabel = new QLabel(utils::tr(QStringLiteral("params.date_format_custom.label")), this);
        form->addRow(m_dateFormatCustomLabel, m_dateFormatCustom);

        addSectionCaption(utils::tr(QStringLiteral("params.section.behavior")));

        // OPCIONAL (pedido do usuário): vale pra QUALQUER tipo de
        // parâmetro (não só File, por isso fora de applyTypeVisibility) —
        // no form de execução, o campo nasce escondido atrás de uma
        // checkbox "Informar <label>?" (ver ParameterFormDialog::setupUi).
        m_optional = new QCheckBox(utils::tr(QStringLiteral("params.optional")), this);
        m_optional->setProperty("kaiRole", QStringLiteral("switch"));
        m_optional->setToolTip(utils::tr(QStringLiteral("params.optional.tip")));
        m_optional->setChecked(param.optional);
        form->addRow(new QLabel(QString(), this), m_optional);

        // OBRIGATÓRIO (achado real: "campos obrigatórios por padrão, não
        // ficou legal... apenas diante seleção de flag pro parâmetro") —
        // opt-in, independente de tipo, igual ao Opcional acima. Liga o
        // asterisco + bloqueio do OK + contagem no badge de grupo no form
        // de execução (ver ParameterFormDialog). Sem efeito se Opcional
        // também estiver marcado (campo escondido nunca bloqueia).
        m_required = new QCheckBox(utils::tr(QStringLiteral("params.required")), this);
        m_required->setProperty("kaiRole", QStringLiteral("switch"));
        m_required->setToolTip(utils::tr(QStringLiteral("params.required.tip")));
        m_required->setChecked(param.required);
        form->addRow(new QLabel(QString(), this), m_required);

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
            if (!current.isEmpty()) {
                m_displayCombo->setCurrentText(current);
            } else if (it != m_collections.constEnd()) {
                // Bug real corrigido ("tava renderizando o id"): sem
                // seleção prévia, o QComboBox nasce no índice 0 — que no
                // schema padrão [Key, Value] É a Key (um identificador,
                // nunca uma boa exibição). Pré-seleciona o mesmo campo
                // "amigável" que o form de execução escolheria sozinho
                // (ver core::resolveCollectionDisplayField), então o
                // usuário já vê o campo certo sem precisar mexer neste
                // combo manualmente.
                m_displayCombo->setCurrentText(core::resolveCollectionDisplayField(*it, QString()));
            }
        };
        refreshDisplayFields();
        if (!param.collectionDisplayField.isEmpty()) {
            m_displayCombo->setCurrentText(param.collectionDisplayField);
        }
        connect(m_collectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [refreshDisplayFields](int) { refreshDisplayFields(); });

        auto applyTypeVisibility = [this, typeConfigCaption]() {
            const QString t = m_type->currentText();
            const bool isSelect = (t == QStringLiteral("select"));
            const bool isFile = (t == QStringLiteral("file"));
            const bool isDateType = (t == QStringLiteral("date"));
            // Legenda "Configurações específicas do tipo" (achado real: com
            // "text"/"bool"/"number"/etc, sem NENHUM campo desse grupo
            // visível, ela ficava colada direto na legenda seguinte
            // ("Comportamento"), sem nada no meio — esconde junto.
            typeConfigCaption->setVisible(isSelect || isFile || isDateType);
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
            m_pickModeLabel->setVisible(isFile);
            m_pickMode->setVisible(isFile);
            m_dateModeLabel->setVisible(isDateType);
            m_dateMode->setVisible(isDateType);
            m_dateRangeLabel->setVisible(isDateType);
            m_dateRange->setVisible(isDateType);
            m_dateFormatLabel->setVisible(isDateType);
            m_dateFormat->setVisible(isDateType);
            const bool isDateCustom = isDateType && m_dateFormat->currentData().toString() == QStringLiteral("custom");
            m_dateFormatCustomLabel->setVisible(isDateCustom);
            m_dateFormatCustom->setVisible(isDateCustom);
        };
        applyTypeVisibility();
        connect(m_type, &QComboBox::currentTextChanged, this, [applyTypeVisibility](const QString &) {
            applyTypeVisibility();
        });
        connect(m_collectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [applyTypeVisibility](int) { applyTypeVisibility(); });
        connect(m_dateFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [applyTypeVisibility](int) { applyTypeVisibility(); });

        // Tamanho maior e responsivo (mesma diretriz aplicada em
        // ParameterFormDialog — achado real: "preciso a mesmo tratamento de
        // espaço, responsividade e tamanho da tela de inclusão de params").
        // Mais estreito que o form de preenchimento de propósito: aqui é
        // sempre rótulo+campo numa coluna só, não precisa da largura extra
        // pro layout de 2 colunas de lá.
        int maxW = 720;
        int maxH = 820;
        if (const QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            if (avail.width() > 400) { maxW = static_cast<int>(avail.width() * 0.6); }
            if (avail.height() > 400) { maxH = static_cast<int>(avail.height() * 0.88); }
        }
        resize(qMin(620, maxW), qMin(700, maxH));

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
        p.pickMode = m_pickMode->currentData().toString();
        p.pickFolder = (p.pickMode == QStringLiteral("folder")); // compat kai.json antigo
        p.optional = m_optional->isChecked();
        p.required = m_required->isChecked();
        p.dateMode = m_dateMode->currentData().toString();
        p.dateRange = m_dateRange->isChecked();
        p.dateFormat = m_dateFormat->currentData().toString();
        p.dateFormatCustom = m_dateFormatCustom->text();
        p.group = m_group->currentText().trimmed();
        p.description = m_description->text().trimmed();
        return p;
    }

private:
    // Envolve um layout de linha (campo + botão) num QWidget para o QFormLayout.
    QWidget *wrapRow(QLayout *inner)
    {
        auto *w = new QWidget(this);
        // TRANSPARENTE: sem isto, este QWidget herda a regra GLOBAL
        // "QWidget { background-color: bg }" e pinta um retângulo QUADRADO
        // atrás do campo (que já tem seu próprio arredondamento) - achado
        // real, reportado: "campo de file pick... com borda quadrada ao
        // invés de preferência". Mesmo padrão já usado em wrapWithLabel/
        // makeFlagsSection pro mesmo tipo de bug.
        w->setObjectName(QStringLiteral("paramEditorRowWrap"));
        w->setStyleSheet(QStringLiteral("QWidget#paramEditorRowWrap { background: transparent; }"));
        inner->setContentsMargins(0, 0, 0, 0);
        w->setLayout(inner);
        return w;
    }

    QVector<core::Collection> m_collections;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_label = nullptr;
    QComboBox *m_group = nullptr;
    QLineEdit *m_description = nullptr;
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
    QComboBox *m_pickMode = nullptr;
    QLabel *m_pickModeLabel = nullptr;
    QComboBox *m_dateMode = nullptr;
    QLabel *m_dateModeLabel = nullptr;
    QCheckBox *m_dateRange = nullptr;
    QLabel *m_dateRangeLabel = nullptr;
    QComboBox *m_dateFormat = nullptr;
    QLabel *m_dateFormatLabel = nullptr;
    QLineEdit *m_dateFormatCustom = nullptr;
    QLabel *m_dateFormatCustomLabel = nullptr;
    QCheckBox *m_optional = nullptr;
    QCheckBox *m_required = nullptr;
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

    // DraggableTableWidget (pedido do usuário: "parâmetros dinâmicos são
    // fortamente posicionais, preciso de uma estratégia de reorder") —
    // arrastar uma linha reordena m_params de verdade (ver
    // handleRowMoved), refletindo a ordem real com que {{param}} aparece
    // no --help/na tela de preenchimento.
    auto *draggableTable = new DraggableTableWidget(0, kColumnCount, this);
    connect(draggableTable, &DraggableTableWidget::rowMoved, this, &ParameterEditorWidget::handleRowMoved);
    m_table = draggableTable;
    configureTable(m_table, {
        {QString(), dragHandleColumnWidth(), false},
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
    // Piso de altura vem do configureTable() acima (6 linhas, achado real:
    // "aumentar tamanho default... permitir mais linhas") — sem override
    // aqui; o antigo valor fixo de 120px (~3 linhas) encolhia de volta o que
    // configureTable já tinha reservado.
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

        m_table->setCellWidget(row, kColDragHandle, makeDragHandleCell(m_table));

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

void ParameterEditorWidget::handleRowMoved(int fromRow, int toRow)
{
    if (fromRow < 0 || fromRow >= m_params.size() || toRow < 0) {
        return;
    }
    // `toRow` é a posição no modelo ORIGINAL (antes de remover `fromRow`) —
    // ajuste de índice de praxe ao remover-e-reinserir: se o destino vinha
    // DEPOIS da origem, ele "desce" uma posição assim que a origem sai.
    int insertAt = (toRow > fromRow) ? toRow - 1 : toRow;
    insertAt = qBound(0, insertAt, m_params.size() - 1);
    if (insertAt == fromRow) {
        return; // soltou no mesmo lugar — nada a fazer
    }
    const core::Parameter moved = m_params.takeAt(fromRow);
    m_params.insert(insertAt, moved);
    rebuildTable(); // já emite changed() sozinho
    // Mantém a linha movida selecionada, pra o usuário não perder o rastro
    // dela depois do reorder.
    m_table->setCurrentCell(insertAt, 0);
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
    // Grupos já usados por OUTROS parâmetros deste mesmo comando (ordem de
    // primeira aparição, sem repetir) — vira sugestão no combo editável de
    // Grupo, pra não reescrever o mesmo nome de grupo com uma variação sutil
    // à toa (ver comentário no ParameterRowDialog).
    QStringList existingGroups;
    for (int i = 0; i < m_params.size(); ++i) {
        if (i == row) { continue; }
        const QString g = m_params.at(i).group.trimmed();
        if (!g.isEmpty() && !existingGroups.contains(g)) {
            existingGroups << g;
        }
    }
    ParameterRowDialog dialog(m_params.at(row), m_collections, existingGroups, this);
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
