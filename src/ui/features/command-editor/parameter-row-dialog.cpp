#include "ui/features/command-editor/parameter-row-dialog.h"

#include "core/config-manager.h"
#include "core/date-param-format.h"
#include "core/grid-param.h"
#include "ui/features/environments/env-var-autocomplete.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/file-dialogs.h"
#include "ui/shared/inline-code-field.h"
#include "ui/shared/row-edit-dialog.h"
#include "ui/shared/parameter-field-factory.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFile>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace kai::ui {

namespace {
namespace tk = kai::utils::tokens;

// Variáveis do ambiente ativo, oferecidas pelo autocomplete de {{VAR}} nos campos de texto.
QStringList activeEnvVarNames()
{
    const core::SettingsData settings = core::ConfigManager().loadSettings();
    for (const core::Environment &env : settings.environments) {
        if (env.id == settings.activeEnvironmentId) {
            return env.vars.keys();
        }
    }
    return {};
}

// Uma opção por linha OU separadas por vírgula (o formato antigo do campo). Itens vazios somem.
QStringList splitOptions(const QString &text)
{
    static const QRegularExpression separator(QStringLiteral("[,\\n]"));
    QStringList out;
    for (const QString &item : text.split(separator, Qt::SkipEmptyParts)) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty()) {
            out << trimmed;
        }
    }
    return out;
}

// O nome vira {{nome}} no comando: letras, dígitos e _, sem começar por dígito.
bool isValidParamName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return pattern.match(name).hasMatch();
}

// Carrega opções de um CSV para o texto "rótulo:valor" / "valor", um por linha.
QString loadOptionsCsv(QWidget *parent)
{
    const QString path = pickOpenFile(
        parent, utils::tr(QStringLiteral("params.csv_dialog.title")),
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
    return normalized.join(QLatin1Char('\n'));
}

// Contêiner sem fundo próprio (senão herda o fundo global e pinta um retângulo quadrado atrás dos campos).
QWidget *makeTransparentHost(QWidget *parent, const QString &objectName)
{
    auto *host = new QWidget(parent);
    host->setObjectName(objectName);
    host->setStyleSheet(QStringLiteral("QWidget#%1 { background: transparent; }").arg(objectName));
    return host;
}

QLabel *makeErrorLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    label->setVisible(false);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(tk::errorFg()));
    return label;
}

QLabel *makeMutedLabel(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(tk::mutedFg()));
    return label;
}

QCheckBox *makeSwitch(QWidget *parent, const QString &text, const QString &tip, bool checked)
{
    auto *box = new QCheckBox(text, parent);
    box->setProperty("kaiRole", QStringLiteral("switch"));
    box->setToolTip(tip);
    box->setChecked(checked);
    return box;
}

// Rótulo empilhado acima do campo + (opcional) mensagem de erro abaixo.
QWidget *makeFieldBlock(QWidget *parent, const QString &label, QWidget *field, bool required = false,
                        QLabel *error = nullptr)
{
    auto *host = makeTransparentHost(parent, QStringLiteral("paramRowBlock"));
    auto *vbox = new QVBoxLayout(host);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(tk::space(1));
    vbox->addWidget(fields::wrapWithLabel(host, label, field, required));
    if (error) {
        error->setParent(host);
        vbox->addWidget(error);
    }
    return host;
}

} // namespace

ParameterRowDialog::ParameterRowDialog(const core::Parameter &param, const QVector<core::Collection> &collections,
                                       const QStringList &existingGroups, const QStringList &otherParamNames,
                                       QWidget *parent)
    : QDialog(parent)
    , m_collections(collections)
    , m_otherNames(otherParamNames.begin(), otherParamNames.end())
{
    setWindowTitle(utils::tr(QStringLiteral("params.edit_row")));
    setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(7), tk::space(6), tk::space(7), tk::space(5));
    outer->setSpacing(tk::space(4));
    outer->addWidget(layout_helpers::makeHintBanner(this, utils::tr(QStringLiteral("params.edit.hint"))));

    // Conteúdo rolável: as seções crescem à vontade, a janela não.
    auto *scrollContent = makeTransparentHost(this, QStringLiteral("paramRowScrollHost"));
    auto *contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setContentsMargins(0, 0, tk::space(1), 0);
    contentLayout->setSpacing(tk::space(4));

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    scrollArea->setWidget(scrollContent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Qualificado por objectName: um "background: transparent" cru no viewport quebra a cascata de QSS dos campos.
    scrollArea->viewport()->setObjectName(QStringLiteral("paramRowScrollViewport"));
    scrollArea->viewport()->setStyleSheet(QStringLiteral(
        "QWidget#paramRowScrollViewport { background: transparent; }"));
    outer->addWidget(scrollArea, 1);

    // Cada seção é um CollapsibleSectionCard (o mesmo componente do editor de comando).
    auto addSection = [&](const QString &title, QWidget *body) {
        auto *card = new CollapsibleSectionCard(title, this);
        card->setAlwaysShowBody(true);
        card->setShowCountBadge(false);
        card->setBody(body);
        card->setExpanded(true, false);
        contentLayout->addWidget(card);
        return card;
    };
    auto makeGrid = [&](QWidget *host) {
        auto *grid = new QGridLayout(host);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(tk::space(5));
        grid->setVerticalSpacing(tk::space(4));
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        return grid;
    };

    {
        auto *body = makeTransparentHost(this, QStringLiteral("paramRowBody"));
        auto *grid = makeGrid(body);

        m_name = new QLineEdit(param.name, this);
        m_name->setObjectName(QStringLiteral("paramNameField"));
        m_name->setPlaceholderText(utils::tr(QStringLiteral("params.name.placeholder")));
        m_nameError = makeErrorLabel(this);
        grid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("field.label.name")), m_name, true, m_nameError), 0, 0);

        m_label = new QLineEdit(param.label, this);
        m_label->setObjectName(QStringLiteral("paramLabelField"));
        grid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("field.label.label")), m_label), 0, 1);
        addSection(utils::tr(QStringLiteral("params.section.identity")), body);
    }

    {
        auto *body = makeTransparentHost(this, QStringLiteral("paramRowBody"));
        auto *grid = makeGrid(body);

        // Combo EDITÁVEL: texto livre cria um grupo novo, e os nomes já usados nos outros parâmetros do comando
        // aparecem como sugestão (evita "Acesso" numa linha e "acesso" noutra virarem dois grupos).
        m_group = new QComboBox(this);
        m_group->setObjectName(QStringLiteral("paramGroupCombo"));
        m_group->setEditable(true);
        m_group->setInsertPolicy(QComboBox::NoInsert);
        m_group->addItem(QString());
        m_group->addItems(existingGroups);
        if (auto *completer = m_group->completer()) {
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            completer->setCompletionMode(QCompleter::PopupCompletion);
        }
        m_group->setCurrentText(param.group);
        m_group->lineEdit()->setPlaceholderText(utils::tr(QStringLiteral("params.group.placeholder")));
        m_group->setToolTip(utils::tr(QStringLiteral("params.group.tip")));
        grid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.group.label")), m_group), 0, 0);

        m_step = new QComboBox(this);
        m_step->setObjectName(QStringLiteral("paramStepCombo"));
        m_step->setEditable(true);
        m_step->setInsertPolicy(QComboBox::NoInsert);
        m_step->addItem(QString());
        m_step->setCurrentText(param.step);
        m_step->lineEdit()->setPlaceholderText(utils::tr(QStringLiteral("params.step.placeholder")));
        m_step->setToolTip(utils::tr(QStringLiteral("params.step.tip")));
        grid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.step.label")), m_step), 1, 0);

        // Só aparece no `--help` de um CLI Path; o `label` já basta no formulário da GUI.
        m_description = new QLineEdit(param.description, this);
        m_description->setObjectName(QStringLiteral("paramDescriptionField"));
        m_description->setPlaceholderText(utils::tr(QStringLiteral("params.description.placeholder")));
        m_description->setToolTip(utils::tr(QStringLiteral("params.description.tip")));
        grid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.description.label")), m_description), 0, 1);
        addSection(utils::tr(QStringLiteral("params.section.organization")), body);
    }

    {
        auto *body = makeTransparentHost(this, QStringLiteral("paramRowBody"));
        auto *grid = makeGrid(body);

        m_type = new QComboBox(this);
        m_type->setObjectName(QStringLiteral("paramTypeCombo"));
        m_type->addItems({QStringLiteral("text"), QStringLiteral("select"), QStringLiteral("bool"),
                          QStringLiteral("file"), QStringLiteral("number"), QStringLiteral("textarea"),
                          QStringLiteral("json"), QStringLiteral("date"), QStringLiteral("grid")});
        m_type->setCurrentText(core::parameterTypeToString(param.type));
        grid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("field.label.type")), m_type), 0, 0);

        // O padrão pode referenciar uma variável do ambiente ativo (ex.: "{{API_URL}}/health").
        m_default = new QLineEdit(param.defaultValue, this);
        m_default->setObjectName(QStringLiteral("paramDefaultField"));
        attachEnvVarAutocomplete(m_default, activeEnvVarNames);
        m_defaultError = makeErrorLabel(this);
        grid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("field.label.default")), m_default, false, m_defaultError), 0, 1);
        addSection(utils::tr(QStringLiteral("params.section.type_and_value")), body);
    }

    {
        auto *body = makeTransparentHost(this, QStringLiteral("paramRowBody"));
        auto *vbox = new QVBoxLayout(body);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(tk::space(4));

        // SELECT: fonte das opções.
        m_selectBox = makeTransparentHost(body, QStringLiteral("paramRowSelectBox"));
        auto *selectLayout = new QVBoxLayout(m_selectBox);
        selectLayout->setContentsMargins(0, 0, 0, 0);
        selectLayout->setSpacing(tk::space(4));

        m_source = new QComboBox(this);
        m_source->setObjectName(QStringLiteral("paramSourceCombo"));
        m_source->addItem(utils::tr(QStringLiteral("params.source.fixed")), QStringLiteral("fixed"));
        m_source->addItem(utils::tr(QStringLiteral("params.source.collection")), QStringLiteral("collection"));
        m_source->addItem(utils::tr(QStringLiteral("params.source.script")), QStringLiteral("script"));
        {
            const bool fromCollection = !param.collectionId.isEmpty();
            const bool fromScript = !fromCollection && !param.optionsScript.isEmpty();
            m_source->setCurrentIndex(fromCollection ? 1 : (fromScript ? 2 : 0));
        }
        selectLayout->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.source.label")), m_source));

        // Lista fixa: expansível, uma opção por linha (ou separadas por vírgula, como antes).
        m_fixedBox = makeTransparentHost(m_selectBox, QStringLiteral("paramRowFixedBox"));
        auto *fixedLayout = new QVBoxLayout(m_fixedBox);
        fixedLayout->setContentsMargins(0, 0, 0, 0);
        fixedLayout->setSpacing(tk::space(1));
        m_options = new InlineCodeField(this);
        m_options->setObjectName(QStringLiteral("paramOptionsField"));
        m_options->setPlainField(true);
        m_options->setEditorTitle(utils::tr(QStringLiteral("params.options.editor_title")));
        m_options->setPlaceholderText(utils::tr(QStringLiteral("params.options.placeholder")));
        m_options->setLineRange(4, 12);
        m_options->setPlainText(param.options.join(QLatin1Char('\n')));
        m_optionsError = makeErrorLabel(this);
        fixedLayout->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.options.label")), m_options, true, m_optionsError));
        auto *optionsFooter = new QHBoxLayout();
        optionsFooter->setSpacing(tk::space(2));
        m_optionsCount = makeMutedLabel(this, QString());
        optionsFooter->addWidget(m_optionsCount, 1);
        auto *csvButton = makeIconButton(this, QStringLiteral("file-code"),
            utils::tr(QStringLiteral("params.options.load_csv.tip")), QColor(tk::accent()));
        connect(csvButton, &QToolButton::clicked, this, [this]() {
            const QString loaded = loadOptionsCsv(this);
            if (!loaded.isEmpty()) {
                m_options->setPlainText(loaded);
            }
        });
        optionsFooter->addWidget(csvButton);
        fixedLayout->addLayout(optionsFooter);
        fixedLayout->addWidget(makeMutedLabel(this, utils::tr(QStringLiteral("params.options.hint"))));
        selectLayout->addWidget(m_fixedBox);

        // Script: expansível, com o mesmo editor grande dos comandos.
        m_scriptBox = makeTransparentHost(m_selectBox, QStringLiteral("paramRowScriptBox"));
        auto *scriptLayout = new QVBoxLayout(m_scriptBox);
        scriptLayout->setContentsMargins(0, 0, 0, 0);
        scriptLayout->setSpacing(tk::space(1));
        m_script = new InlineCodeField(this);
        m_script->setObjectName(QStringLiteral("paramScriptField"));
        m_script->setEditorTitle(utils::tr(QStringLiteral("params.script.editor_title")));
        m_script->setPlaceholderText(utils::tr(QStringLiteral("params.script.placeholder")));
        m_script->setLineRange(5, 16);
        m_script->setPlainText(param.optionsScript);
        m_script->setToolTip(utils::tr(QStringLiteral("params.script.tip")));
        attachEnvVarAutocomplete(m_script->editor(), activeEnvVarNames);
        m_scriptError = makeErrorLabel(this);
        scriptLayout->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.script.label")), m_script, true, m_scriptError));
        scriptLayout->addWidget(makeMutedLabel(this, utils::tr(QStringLiteral("params.script.hint"))));
        selectLayout->addWidget(m_scriptBox);

        // Coleção: a coleção e o campo do schema exibido no seletor.
        m_collectionBox = makeTransparentHost(m_selectBox, QStringLiteral("paramRowCollectionBox"));
        auto *collectionGrid = makeGrid(m_collectionBox);
        m_collectionCombo = new QComboBox(this);
        m_collectionCombo->setObjectName(QStringLiteral("paramCollectionCombo"));
        m_collectionCombo->addItem(utils::tr(QStringLiteral("params.collection.none")), QString());
        for (const core::Collection &c : m_collections) {
            m_collectionCombo->addItem(c.name, c.id);
        }
        {
            const int idx = m_collectionCombo->findData(param.collectionId);
            m_collectionCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        makeSearchableCombo(m_collectionCombo);
        m_collectionError = makeErrorLabel(this);
        collectionGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.collection.source_label")),
                                                 m_collectionCombo, true, m_collectionError), 0, 0);
        m_displayCombo = new QComboBox(this);
        m_displayCombo->setObjectName(QStringLiteral("paramDisplayFieldCombo"));
        m_displayCombo->setEditable(true);
        collectionGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.display_field.label")), m_displayCombo), 0, 1);
        selectLayout->addWidget(m_collectionBox);

        // Seleção múltipla: lista fixa ou script (a coleção tem a própria tela multi-seleção).
        m_multiSelect = makeSwitch(this, utils::tr(QStringLiteral("params.multi_select")),
                                   utils::tr(QStringLiteral("params.multi_select.tip")), param.multiSelect);
        m_multiSelect->setObjectName(QStringLiteral("paramMultiSelectSwitch"));
        m_multiSelectBlock = m_multiSelect;
        selectLayout->addWidget(m_multiSelect);
        vbox->addWidget(m_selectBox);

        m_fileBox = makeTransparentHost(body, QStringLiteral("paramRowFileBox"));
        auto *fileGrid = makeGrid(m_fileBox);
        m_filePathFormat = new QComboBox(this);
        m_filePathFormat->addItem(utils::tr(QStringLiteral("params.path_format.native")), QStringLiteral("native"));
        m_filePathFormat->addItem(utils::tr(QStringLiteral("params.path_format.posix")), QStringLiteral("posix"));
        m_filePathFormat->addItem(utils::tr(QStringLiteral("params.path_format.windows")), QStringLiteral("windows"));
        {
            const int idx = m_filePathFormat->findData(
                param.filePathFormat.isEmpty() ? QStringLiteral("native") : param.filePathFormat);
            m_filePathFormat->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        fileGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.path_format.label")), m_filePathFormat), 0, 0);
        m_pickMode = new QComboBox(this);
        m_pickMode->addItem(utils::tr(QStringLiteral("params.pick_mode.file")), QStringLiteral("file"));
        m_pickMode->addItem(utils::tr(QStringLiteral("params.pick_mode.folder")), QStringLiteral("folder"));
        m_pickMode->addItem(utils::tr(QStringLiteral("params.pick_mode.both")), QStringLiteral("both"));
        m_pickMode->setToolTip(utils::tr(QStringLiteral("params.pick_mode.tip")));
        {
            const int idx = m_pickMode->findData(param.pickMode.isEmpty() ? QStringLiteral("file") : param.pickMode);
            m_pickMode->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        fileGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.pick_mode.label")), m_pickMode), 0, 1);
        vbox->addWidget(m_fileBox);

        m_dateBox = makeTransparentHost(body, QStringLiteral("paramRowDateBox"));
        auto *dateGrid = makeGrid(m_dateBox);
        m_dateMode = new QComboBox(this);
        m_dateMode->addItem(utils::tr(QStringLiteral("params.date_mode.date")), QStringLiteral("date"));
        m_dateMode->addItem(utils::tr(QStringLiteral("params.date_mode.time")), QStringLiteral("time"));
        m_dateMode->addItem(utils::tr(QStringLiteral("params.date_mode.datetime")), QStringLiteral("datetime"));
        {
            const int idx = m_dateMode->findData(param.dateMode.isEmpty() ? QStringLiteral("date") : param.dateMode);
            m_dateMode->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        dateGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.date_mode.label")), m_dateMode), 0, 0);
        m_dateFormat = new QComboBox(this);
        for (const QString &key : core::dateFormatPresetKeys()) {
            m_dateFormat->addItem(core::dateFormatPresetLabel(key), key);
        }
        m_dateFormat->setObjectName(QStringLiteral("paramDateFormatCombo"));
        m_dateFormat->setToolTip(utils::tr(QStringLiteral("params.date_format.tip")));
        {
            const int idx = m_dateFormat->findData(param.dateFormat.isEmpty() ? QStringLiteral("iso_date") : param.dateFormat);
            m_dateFormat->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        dateGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.date_format.label")), m_dateFormat), 0, 1);
        m_dateRange = makeSwitch(this, utils::tr(QStringLiteral("params.date_range")),
                                 utils::tr(QStringLiteral("params.date_range.tip")), param.dateRange);
        dateGrid->addWidget(m_dateRange, 1, 0, 1, 2);
        m_dateCustomBox = makeTransparentHost(m_dateBox, QStringLiteral("paramRowDateCustomBox"));
        auto *customLayout = new QVBoxLayout(m_dateCustomBox);
        customLayout->setContentsMargins(0, 0, 0, 0);
        m_dateFormatCustom = new QLineEdit(param.dateFormatCustom, this);
        m_dateFormatCustom->setObjectName(QStringLiteral("paramDateFormatCustomField"));
        m_dateFormatCustom->setPlaceholderText(utils::tr(QStringLiteral("params.date_format_custom.placeholder")));
        m_dateFormatCustom->setToolTip(utils::tr(QStringLiteral("params.date_format_custom.tip")));
        m_dateFormatError = makeErrorLabel(this);
        customLayout->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.date_format_custom.label")),
                                               m_dateFormatCustom, true, m_dateFormatError));
        dateGrid->addWidget(m_dateCustomBox, 2, 0, 1, 2);
        vbox->addWidget(m_dateBox);

        // GRID: as colunas (lista de nomes + lápis/lixeira, como as demais) e o modelo de linha.
        m_gridBox = makeTransparentHost(body, QStringLiteral("paramRowGridBox"));
        auto *gridLayout = new QVBoxLayout(m_gridBox);
        gridLayout->setContentsMargins(0, 0, 0, 0);
        gridLayout->setSpacing(tk::space(2));
        m_gridColumns = param.gridColumns;
        {
            auto *header = new QHBoxLayout();
            auto *title = new QLabel(utils::tr(QStringLiteral("params.grid.columns")).toUpper(), m_gridBox);
            title->setProperty("kaiRole", QStringLiteral("caption"));
            header->addWidget(title);
            header->addStretch(1);
            QPushButton *add = makeAccentAddButton(m_gridBox, utils::tr(QStringLiteral("keyvalue.add")));
            connect(add, &QPushButton::clicked, this, [this]() {
                m_gridColumns.append(core::GridColumn{});
                if (editGridColumn(int(m_gridColumns.size()) - 1)) {
                    refreshGridColumns();
                    validate();
                } else {
                    m_gridColumns.removeLast();
                }
            });
            header->addWidget(add);
            gridLayout->addLayout(header);
        }
        m_gridTable = new QTableWidget(0, 2, m_gridBox);
        m_gridTable->setObjectName(QStringLiteral("paramGridColumnsTable"));
        configureTable(m_gridTable, {{QString(), 240, true}, {QString(), rowActionsColumnWidth(), false}});
        m_gridTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_gridTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_gridTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_gridTable->horizontalHeader()->setVisible(false);
        m_gridTable->horizontalHeader()->setStretchLastSection(false);
        m_gridTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        connect(m_gridTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            if (editGridColumn(row)) {
                refreshGridColumns();
                validate();
            }
        });
        gridLayout->addWidget(m_gridTable);
        m_gridError = makeErrorLabel(this);
        gridLayout->addWidget(m_gridError);

        auto *templateGrid = new QGridLayout();
        templateGrid->setHorizontalSpacing(tk::space(5));
        templateGrid->setColumnStretch(0, 3);
        templateGrid->setColumnStretch(1, 1);
        m_gridTemplate = new QLineEdit(param.gridRowTemplate, this);
        m_gridTemplate->setObjectName(QStringLiteral("paramGridTemplateField"));
        m_gridTemplate->setPlaceholderText(QStringLiteral("{\"sku\":{{sku|json}}}"));
        templateGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.grid.row_template")), m_gridTemplate), 0, 0);
        m_gridSeparator = new QLineEdit(param.gridRowSeparator, this);
        m_gridSeparator->setObjectName(QStringLiteral("paramGridSeparatorField"));
        templateGrid->addWidget(makeFieldBlock(this, utils::tr(QStringLiteral("params.grid.row_separator")), m_gridSeparator), 0, 1);
        gridLayout->addLayout(templateGrid);
        vbox->addWidget(m_gridBox);
        refreshGridColumns();

        m_typeConfigCard = addSection(utils::tr(QStringLiteral("params.section.type_config")), body);
    }

    {
        auto *body = makeTransparentHost(this, QStringLiteral("paramRowBody"));
        auto *grid = makeGrid(body);
        // OPCIONAL: o campo nasce escondido atrás de "Informar <label>?" no formulário de execução.
        m_optional = makeSwitch(this, utils::tr(QStringLiteral("params.optional")),
                                utils::tr(QStringLiteral("params.optional.tip")), param.optional);
        m_optional->setObjectName(QStringLiteral("paramOptionalSwitch"));
        grid->addWidget(m_optional, 0, 0);
        // OBRIGATÓRIO: marca com asterisco e bloqueia o OK do formulário até preencher. Não combina com Opcional.
        m_required = makeSwitch(this, utils::tr(QStringLiteral("params.required")),
                                utils::tr(QStringLiteral("params.required.tip")), param.required && !param.optional);
        m_required->setObjectName(QStringLiteral("paramRequiredSwitch"));
        grid->addWidget(m_required, 0, 1);
        addSection(utils::tr(QStringLiteral("params.section.behavior")), body);
    }
    contentLayout->addStretch(1);

    auto *footer = new QHBoxLayout();
    m_summary = makeErrorLabel(this);
    footer->addWidget(m_summary, 1);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(m_buttons);
    outer->addLayout(footer);

    refreshDisplayFields();
    if (!param.collectionDisplayField.isEmpty()) {
        m_displayCombo->setCurrentText(param.collectionDisplayField);
    }
    refreshOptionsCount();

    connect(m_collectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshDisplayFields();
        validate();
    });
    connect(m_collectionCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { m_touched.insert(QStringLiteral("collection")); validate(); });
    connect(m_type, &QComboBox::currentTextChanged, this, [this](const QString &) { applyVisibility(); validate(); });
    connect(m_source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        // Trocar de fonte é uma escolha ativa: a pendência do campo novo já aparece.
        m_touched.insert(QStringLiteral("options"));
        m_touched.insert(QStringLiteral("script"));
        m_touched.insert(QStringLiteral("collection"));
        applyVisibility();
        validate();
    });
    connect(m_dateFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyVisibility(); validate(); });
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &) { m_touched.insert(QStringLiteral("name")); validate(); });
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &) { validate(); });
    connect(m_name, &QLineEdit::editingFinished, this, [this]() { m_touched.insert(QStringLiteral("name")); validate(); });
    connect(m_default, &QLineEdit::textChanged, this, [this](const QString &) { validate(); });
    connect(m_options, &InlineCodeField::textChanged, this, [this]() {
        m_touched.insert(QStringLiteral("options"));
        refreshOptionsCount();
        validate();
    });
    connect(m_script, &InlineCodeField::textChanged, this, [this]() { m_touched.insert(QStringLiteral("script")); validate(); });
    connect(m_dateFormatCustom, &QLineEdit::textEdited, this, [this](const QString &) { m_touched.insert(QStringLiteral("date")); validate(); });
    connect(m_dateFormatCustom, &QLineEdit::textChanged, this, [this](const QString &) { validate(); });
    connect(m_optional, &QCheckBox::toggled, this, [this](bool) { applyVisibility(); validate(); });

    applyVisibility();
    validate();

    // Tamanho contido (o conteúdo rola por dentro): mais largo que o formulário antigo, mas sem tomar a tela.
    int width = 720;
    int height = 640;
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        if (avail.width() > 400) {
            width = qBound(640, static_cast<int>(avail.width() * 0.42), 780);
        }
        if (avail.height() > 400) {
            height = qBound(520, static_cast<int>(avail.height() * 0.68), 680);
            height = qMin(height, static_cast<int>(avail.height() * 0.92));
        }
        width = qMin(width, static_cast<int>(avail.width() * 0.92));
    }
    setMinimumSize(qMin(560, width), qMin(460, height));
    resize(width, height);
    centerOnParent(this);
}

QString ParameterRowDialog::currentSource() const
{
    return m_source->currentData().toString();
}

void ParameterRowDialog::refreshDisplayFields()
{
    const QString colId = m_collectionCombo->currentData().toString();
    const QString current = m_displayCombo->currentText();
    m_displayCombo->clear();
    m_displayCombo->setEnabled(!colId.isEmpty());
    const auto it = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
        [&colId](const core::Collection &c) { return c.id == colId; });
    if (it != m_collections.constEnd()) {
        for (const core::CollectionField &f : it->schema) {
            m_displayCombo->addItem(f.name);
        }
    }
    if (!current.isEmpty()) {
        m_displayCombo->setCurrentText(current);
    } else if (it != m_collections.constEnd()) {
        // O índice 0 do schema padrão [Key, Value] é a Key (um identificador); pré-seleciona o campo "amigável"
        // que o formulário de execução escolheria sozinho.
        m_displayCombo->setCurrentText(core::resolveCollectionDisplayField(*it, QString()));
    }
}

void ParameterRowDialog::refreshGridColumns()
{
    // Zera antes de repopular: setCellWidget não solta os widgets antigos quando a contagem de linhas não muda.
    m_gridTable->setRowCount(0);
    m_gridTable->setRowCount(int(m_gridColumns.size()));
    for (int row = 0; row < m_gridColumns.size(); ++row) {
        const core::GridColumn &column = m_gridColumns.at(row);
        const QString shown = column.label.isEmpty() || column.label == column.name
            ? column.name
            : QStringLiteral("%1  (%2)").arg(column.label, column.name);
        auto *item = new QTableWidgetItem(QStringLiteral("%1 · %2").arg(shown, core::parameterTypeToString(column.type)));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_gridTable->setItem(row, 0, item);
        m_gridTable->setCellWidget(row, 1, makeRowActionsCell(m_gridTable,
            [this, row]() {
                if (editGridColumn(row)) {
                    refreshGridColumns();
                    validate();
                }
            },
            [this, row]() {
                m_gridColumns.remove(row);
                refreshGridColumns();
                validate();
            }));
    }
    m_gridTable->setFixedHeight(qMax(1, int(m_gridColumns.size())) * standardRowHeight() + 2 * tk::space(1) + 4);
}

bool ParameterRowDialog::editGridColumn(int index)
{
    if (index < 0 || index >= m_gridColumns.size()) {
        return false;
    }
    const core::GridColumn &column = m_gridColumns.at(index);
    const QStringList types = {QStringLiteral("text"), QStringLiteral("number"), QStringLiteral("bool"),
                               QStringLiteral("select"), QStringLiteral("date")};
    const QStringList dateModes = {QStringLiteral("date"), QStringLiteral("time"), QStringLiteral("datetime")};
    QVector<RowEditDialog::FieldSpec> fields;
    fields.append({QStringLiteral("name"), utils::tr(QStringLiteral("field.label.name")), RowEditDialog::FieldType::Text,
                   column.name, {}, QString(), false, QString()});
    fields.append({QStringLiteral("label"), utils::tr(QStringLiteral("field.label.label")), RowEditDialog::FieldType::Text,
                   column.label, {}, QString(), false, QString()});
    fields.append({QStringLiteral("type"), utils::tr(QStringLiteral("field.label.type")), RowEditDialog::FieldType::Combo,
                   core::parameterTypeToString(core::gridColumnType(column)), types, QString(), false, QString()});
    fields.append({QStringLiteral("default"), utils::tr(QStringLiteral("field.label.default")), RowEditDialog::FieldType::Text,
                   column.defaultValue, {}, QString(), false, QString()});
    fields.append({QStringLiteral("options"), utils::tr(QStringLiteral("params.options.label")), RowEditDialog::FieldType::MultiLine,
                   column.options.join(QLatin1Char('\n')), {}, utils::tr(QStringLiteral("params.options.placeholder")), false, QString()});
    fields.append({QStringLiteral("date_mode"), utils::tr(QStringLiteral("params.date_mode.label")), RowEditDialog::FieldType::Combo,
                   column.dateMode, dateModes, QString(), false, QString()});

    RowEditDialog dialog(utils::tr(QStringLiteral("params.grid.edit_column")), fields, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    core::GridColumn updated;
    updated.name = dialog.value(QStringLiteral("name")).trimmed();
    updated.label = dialog.value(QStringLiteral("label")).trimmed();
    updated.type = core::parameterTypeFromString(dialog.value(QStringLiteral("type")));
    updated.defaultValue = dialog.value(QStringLiteral("default"));
    updated.options = splitOptions(dialog.value(QStringLiteral("options")));
    updated.dateMode = dialog.value(QStringLiteral("date_mode"));
    if (updated.type != core::ParameterType::Select) {
        updated.options.clear();
    }

    QString problem;
    if (!isValidParamName(updated.name)) {
        problem = utils::tr(QStringLiteral("params.error.name_invalid"));
    } else {
        for (int i = 0; i < m_gridColumns.size(); ++i) {
            if (i != index && m_gridColumns.at(i).name == updated.name) {
                problem = utils::tr(QStringLiteral("params.error.name_duplicate"));
            }
        }
    }
    if (updated.type == core::ParameterType::Select && updated.options.isEmpty()) {
        problem = utils::tr(QStringLiteral("params.error.options_required"));
    }
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("params.grid.edit_column")), problem);
        return false;
    }
    m_gridColumns[index] = updated;
    return true;
}

void ParameterRowDialog::refreshOptionsCount()
{
    const int count = splitOptions(m_options->toPlainText()).size();
    m_optionsCount->setText(count == 0 ? QString() : utils::tr(QStringLiteral("params.options.count")).arg(count));
}

void ParameterRowDialog::applyVisibility()
{
    const QString type = m_type->currentText();
    const bool isSelect = (type == QStringLiteral("select"));
    const bool isFile = (type == QStringLiteral("file"));
    const bool isDate = (type == QStringLiteral("date"));
    const bool isGrid = (type == QStringLiteral("grid"));
    const QString source = currentSource();

    m_typeConfigCard->setVisible(isSelect || isFile || isDate || isGrid);
    m_gridBox->setVisible(isGrid);
    m_default->setEnabled(!isGrid);
    m_selectBox->setVisible(isSelect);
    m_fixedBox->setVisible(isSelect && source == QStringLiteral("fixed"));
    m_scriptBox->setVisible(isSelect && source == QStringLiteral("script"));
    m_collectionBox->setVisible(isSelect && source == QStringLiteral("collection"));
    m_multiSelectBlock->setVisible(isSelect && source != QStringLiteral("collection"));
    m_fileBox->setVisible(isFile);
    m_dateBox->setVisible(isDate);
    m_dateCustomBox->setVisible(isDate && m_dateFormat->currentData().toString() == QStringLiteral("custom"));

    // Um campo opcional fica escondido até o usuário pedir, então "obrigatório" não faz sentido junto.
    m_required->setEnabled(!m_optional->isChecked());
    if (m_optional->isChecked()) {
        m_required->setChecked(false);
    }
    m_required->setToolTip(m_optional->isChecked() ? utils::tr(QStringLiteral("params.required.disabled_tip"))
                                                   : utils::tr(QStringLiteral("params.required.tip")));
}

void ParameterRowDialog::validate()
{
    const QString type = m_type->currentText();
    const bool isSelect = (type == QStringLiteral("select"));
    const QString source = currentSource();

    int issues = 0;
    int shown = 0;
    // `needsTouch`: o problema é "campo vazio" — só reclama depois que o usuário mexeu no campo.
    auto report = [&](QLabel *label, const QString &touchKey, const QString &message, bool needsTouch) {
        const bool show = !message.isEmpty() && (!needsTouch || m_touched.contains(touchKey));
        label->setText(message);
        label->setVisible(show);
        if (!message.isEmpty()) ++issues;
        if (show) ++shown;
    };

    const QString name = m_name->text().trimmed();
    QString nameMessage;
    if (name.isEmpty()) {
        nameMessage = utils::tr(QStringLiteral("params.error.name_required"));
    } else if (!isValidParamName(name)) {
        nameMessage = utils::tr(QStringLiteral("params.error.name_invalid"));
    } else if (m_otherNames.contains(name)) {
        nameMessage = utils::tr(QStringLiteral("params.error.name_duplicate"));
    }
    report(m_nameError, QStringLiteral("name"), nameMessage, name.isEmpty());

    QString defaultMessage;
    if (type == QStringLiteral("number") && !m_default->text().trimmed().isEmpty()) {
        bool ok = false;
        m_default->text().trimmed().toLongLong(&ok);
        if (!ok) {
            defaultMessage = utils::tr(QStringLiteral("params.error.default_not_number"));
        }
    }
    report(m_defaultError, QStringLiteral("default"), defaultMessage, false);

    const bool needsOptions = isSelect && source == QStringLiteral("fixed") && splitOptions(m_options->toPlainText()).isEmpty();
    report(m_optionsError, QStringLiteral("options"),
           needsOptions ? utils::tr(QStringLiteral("params.error.options_required")) : QString(), true);

    const bool needsScript = isSelect && source == QStringLiteral("script") && m_script->toPlainText().trimmed().isEmpty();
    report(m_scriptError, QStringLiteral("script"),
           needsScript ? utils::tr(QStringLiteral("params.error.script_required")) : QString(), true);

    const bool needsCollection = isSelect && source == QStringLiteral("collection")
        && m_collectionCombo->currentData().toString().isEmpty();
    report(m_collectionError, QStringLiteral("collection"),
           needsCollection ? utils::tr(QStringLiteral("params.error.collection_required")) : QString(), true);

    const bool needsDateFormat = type == QStringLiteral("date")
        && m_dateFormat->currentData().toString() == QStringLiteral("custom")
        && m_dateFormatCustom->text().trimmed().isEmpty();
    report(m_dateFormatError, QStringLiteral("date"),
           needsDateFormat ? utils::tr(QStringLiteral("params.error.date_format_required")) : QString(), true);

    report(m_gridError, QStringLiteral("grid"),
           type == QStringLiteral("grid") && m_gridColumns.isEmpty() ? utils::tr(QStringLiteral("params.error.grid_columns")) : QString(),
           false);

    m_valid = (issues == 0);
    if (auto *ok = m_buttons->button(QDialogButtonBox::Ok)) {
        ok->setEnabled(m_valid);
        ok->setToolTip(m_valid ? QString() : utils::tr(QStringLiteral("params.ok_disabled.tip")));
    }
    m_summary->setText(utils::tr(QStringLiteral("params.error.summary")).arg(issues));
    m_summary->setVisible(shown > 0);
}

void ParameterRowDialog::setStepSuggestions(const QStringList &steps)
{
    const QString current = m_step->currentText();
    for (const QString &step : steps) {
        if (m_step->findText(step) < 0) {
            m_step->addItem(step);
        }
    }
    if (auto *completer = m_step->completer()) {
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
    }
    m_step->setCurrentText(current);
}

core::Parameter ParameterRowDialog::result() const
{
    core::Parameter p;
    p.name = m_name->text().trimmed();
    p.label = m_label->text();
    p.type = core::parameterTypeFromString(m_type->currentText());
    p.defaultValue = m_default->text();

    // Só a fonte escolhida é gravada; as outras ficam vazias para não voltarem a valer ao trocar de fonte.
    if (p.type == core::ParameterType::Select) {
        const QString source = currentSource();
        if (source == QStringLiteral("fixed")) {
            p.options = splitOptions(m_options->toPlainText());
        } else if (source == QStringLiteral("script")) {
            p.optionsScript = m_script->toPlainText().trimmed();
        } else {
            p.collectionId = m_collectionCombo->currentData().toString();
            p.collectionDisplayField = m_displayCombo->currentText().trimmed();
        }
    }
    p.multiSelect = (p.type == core::ParameterType::Select) && p.collectionId.isEmpty() && m_multiSelect->isChecked();
    p.filePathFormat = m_filePathFormat->currentData().toString();
    p.pickMode = m_pickMode->currentData().toString();
    p.pickFolder = (p.pickMode == QStringLiteral("folder")); // compat com kai.yml antigo
    p.optional = m_optional->isChecked();
    p.required = !p.optional && m_required->isChecked();
    p.dateMode = m_dateMode->currentData().toString();
    p.dateRange = m_dateRange->isChecked();
    p.dateFormat = m_dateFormat->currentData().toString();
    p.dateFormatCustom = m_dateFormatCustom->text().trimmed();
    p.group = m_group->currentText().trimmed();
    p.step = m_step->currentText().trimmed();
    p.description = m_description->text().trimmed();
    if (p.type == core::ParameterType::Grid) {
        p.gridColumns = m_gridColumns;
        p.gridRowTemplate = m_gridTemplate->text();
        p.gridRowSeparator = m_gridSeparator->text();
        p.defaultValue.clear();
    }
    return p;
}

} // namespace kai::ui
