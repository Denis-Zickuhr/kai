#include "ui/parameter-form-dialog.h"
#include "ui/dialog-utils.h"
#include "ui/collection-selector-dialog.h"
#include "ui/table-utils.h"
#include "ui/lucide-icons.h"
#include "ui/inline-code-field.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "utils/path-format.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QListWidget>
#include <QSpinBox>
#include <QCompleter>
#include <QCheckBox>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QFont>
#include <QTextDocument>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageBox>
#include <algorithm>
#include <limits>

#include "utils/translation-manager.h"

namespace kai::ui {

namespace {

// Rótulo empilhado ACIMA do campo (Top Label) — diretriz de arquitetura
// visual do usuário: evita o "label na esquerda empurra o campo" que
// desalinha quando os tipos de campo mudam de altura/largura (texto vs
// toggle vs lista checkable). Mesma ideia do wrapWithLabel de
// command-editor-dialog.cpp, mas em peso normal (não caixa alta/muted) —
// aqui o rótulo é o nome do parâmetro, não uma legenda de seção.
QWidget *wrapWithLabel(QWidget *parent, const QString &labelText, QWidget *field)
{
    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("paramFieldWrap"));
    // Transparente, mas QUALIFICADO por objectName: uma regra crua
    // ("background: transparent;" sem seletor) CASCATEIA para os filhos e
    // deixava o campo (QLineEdit do tipo Text) SEM fundo escuro — herdava o
    // transparent em vez do bg() do QSS global (relatado; File/Select não
    // sofriam porque têm um container de fundo próprio). Restrito a #objectName,
    // só o wrap fica transparente; os campos mantêm seu bg.
    container->setStyleSheet(QStringLiteral(
        "QWidget#paramFieldWrap { background: transparent; }"));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(1));
    auto *labelWidget = new QLabel(labelText, container);
    QFont labelFont = labelWidget->font();
    labelFont.setBold(true);
    labelWidget->setFont(labelFont);
    labelWidget->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::fg()));
    layout->addWidget(labelWidget);
    field->setParent(container);
    layout->addWidget(field);
    return container;
}

// Grade de 1 ou 2 colunas (diretriz do usuário): campos LONGOS (texto livre,
// múltipla seleção, textarea, json) ocupam a linha inteira; campos COMPACTOS
// (booleano, número, lookup de coleção) fluem automaticamente em 2 colunas,
// sem gerar rolagem vertical desnecessária. Não existe "lookup" como
// ParameterType próprio neste código — é Select com collectionId preenchido —
// mas visual e funcionalmente é exatamente o "lookup rápido" da diretriz.
bool isCompactParam(const core::Parameter &param)
{
    switch (param.type) {
    case core::ParameterType::Bool:
    case core::ParameterType::Number:
    case core::ParameterType::File:
        return true;
    case core::ParameterType::Select:
        // Lookup de coleção: campo compacto (rótulo + botão de busca).
        // Select comum e multi-select (ambos podem ter textos longos nas
        // opções) continuam em largura cheia.
        return !param.collectionId.isEmpty();
    case core::ParameterType::Text:
        return false;
    case core::ParameterType::Textarea:
        // Multi-linha com expansão (pedido explícito do usuário: "campo de
        // texto com expansão") — precisa da largura cheia pra não ficar
        // espremida numa meia-coluna de 280px.
        return false;
    case core::ParameterType::Json:
        // "Mini campo" (palavra do próprio usuário) mas ainda assim
        // estruturado — na dúvida, largura cheia é a escolha mais segura
        // pra um snippet JSON (mesmo pequeno, chaves/colchetes/indentação
        // pedem um pouco de respiro horizontal).
        return false;
    }
    return false;
}

} // namespace

ParameterFormDialog::ParameterFormDialog(const QVector<core::Parameter> &params, QWidget *parent,
                                          const QMap<QString, QString> &lastValues,
                                          const QMap<QString, QStringList> &usageHistory,
                                          const QVector<core::Collection> &collections,
                                          const QString &description)
    : QDialog(parent)
    , m_params(params)
    , m_lastValues(lastValues)
    , m_usageHistory(usageHistory)
    , m_collections(collections)
    , m_description(description)
{
    setWindowTitle(utils::tr(QStringLiteral("params.title")));
    setSizeGripEnabled(true);
    setupUi(params);
    // Tamanho confortável (feedback do usuário: o form estava minúsculo).
    // Largura mínima garante que labels + campos apareçam bem; a altura
    // acompanha o conteúdo mas com um piso razoável. Um pouco mais larga que
    // antes (era 460) porque a grade de 2 colunas (booleano/número/lookup
    // lado a lado) precisa de espaço para não ficar espremida.
    setMinimumWidth(560);
    adjustSize();
    if (height() < 180) {
        resize(width(), 180);
    }
    centerOnParent(this);
}

void ParameterFormDialog::setupUi(const QVector<core::Parameter> &params)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 16);
    mainLayout->setSpacing(14);

    // Título + subtítulo (mockup enviado pelo usuário): o subtítulo explica
    // o propósito da tela numa cor secundária, acima da grade de campos.
    // Topo do form: um HINT banner (mesmo padrão dos hints das Configurações
    // — pedido do usuário) com o DETALHAMENTO do comando quando preenchido;
    // se vazio, cai no texto genérico padrão ("insira os valores...").
    const QString hintText = m_description.trimmed().isEmpty()
        ? utils::tr(QStringLiteral("params.subtitle"))
        : m_description;
    mainLayout->addWidget(layout_helpers::makeHintBanner(this, hintText));

    // Grade de 1 ou 2 colunas (diretriz do usuário — ver isCompactParam):
    // campos longos ocupam as duas colunas; campos compactos fluem lado a
    // lado, otimizando o aproveitamento vertical do diálogo.
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(utils::tokens::space(4));
    grid->setVerticalSpacing(utils::tokens::space(4));
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    int gridRow = 0;
    // Campo compacto "pendente": um compacto só é colocado na grade quando
    // sabemos se vai ter um par ao lado ou não. Sem isso, um compacto que
    // acaba sozinho na linha (único parâmetro do form, ou o último de uma
    // lista ímpar) ficava preso na coluna 0 com a coluna 1 vazia — bug
    // reportado ("quando só tem um parâmetro, o select fica ocupando só
    // uma célula, estranho"). Resolvido: guarda o compacto até o próximo
    // campo decidir seu destino (pareia com outro compacto, ou — se vier
    // um full ou o form acabar — vira full-width sozinho também.
    QWidget *pendingCompact = nullptr;
    auto flushPendingCompact = [&]() {
        if (pendingCompact) {
            grid->addWidget(pendingCompact, gridRow, 0, 1, 2);
            pendingCompact = nullptr;
            ++gridRow;
        }
    };
    auto addFull = [&](QWidget *w) {
        flushPendingCompact();
        grid->addWidget(w, gridRow, 0, 1, 2);
        ++gridRow;
    };
    auto addCompact = [&](QWidget *w) {
        if (pendingCompact) {
            grid->addWidget(pendingCompact, gridRow, 0);
            grid->addWidget(w, gridRow, 1);
            pendingCompact = nullptr;
            ++gridRow;
        } else {
            pendingCompact = w;
        }
    };
    auto addField = [&](const core::Parameter &param, QWidget *w) {
        if (isCompactParam(param)) {
            addCompact(w);
        } else {
            addFull(w);
        }
    };

    for (const core::Parameter &param : params) {
        const QString label = param.label.isEmpty() ? param.name : param.label;
        // Valor inicial: último valor informado (se houver) tem prioridade
        // sobre o defaultValue do parâmetro (salvar últimos params).
        const QString initialValue = m_lastValues.contains(param.name)
            ? m_lastValues.value(param.name)
            : param.defaultValue;

        switch (param.type) {
        case core::ParameterType::Text: {
            auto *field = new QLineEdit(initialValue, this);
            addField(param, wrapWithLabel(this, label, field));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Select: {
            // Parâmetro ligado a uma COLEÇÃO: em vez de um combo
            // com autocomplete (ruim para muitos registros), abrimos uma
            // TELA DE SELEÇÃO dedicada (readonly, filtros, paginação,
            // multi-select) — feedback do usuário. O campo mostra o(s)
            // valor(es) escolhido(s); o botão abre a tela.
            if (!param.collectionId.isEmpty()) {
                const auto colIt = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
                    [&param](const core::Collection &c) { return c.id == param.collectionId; });

                // "Lookup rápido" (diretriz do usuário): campo + botão de
                // busca fundidos num único bloco (input-group-addon), com UMA
                // borda em volta dos dois — não dois controles soltos lado a
                // lado. O bloco desenha a borda/fundo/raio; campo e botão
                // ficam sem borda própria (transparentes) para não duplicar.
                auto *rowWidget = new QWidget(this);
                rowWidget->setObjectName(QStringLiteral("lookupInputGroup"));
                rowWidget->setStyleSheet(QStringLiteral(
                    "QWidget#lookupInputGroup { background-color: %1; border: 1px solid %2;"
                    " border-radius: %3px; }")
                    .arg(utils::tokens::bg(), utils::tokens::borderColor())
                    .arg(utils::tokens::radiusMd()));
                auto *rowLayout = new QHBoxLayout(rowWidget);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                rowLayout->setSpacing(0);
                auto *display = new QLineEdit(rowWidget);
                display->setReadOnly(true);
                display->setPlaceholderText(utils::tr(QStringLiteral("params.lookup.no_value")));
                display->setStyleSheet(QStringLiteral(
                    "QLineEdit { background: transparent; color: %1; border: none;"
                    " padding: %2px %3px; }")
                    .arg(utils::tokens::fg())
                    .arg(utils::tokens::space(2)).arg(utils::tokens::space(3)));
                rowLayout->addWidget(display, 1);
                auto *pickButton = makeIconButton(rowWidget, QStringLiteral("search"),
                    utils::tr(QStringLiteral("params.collection.pick")), QColor(utils::tokens::accent()));
                pickButton->setStyleSheet(QStringLiteral(
                    "QToolButton { background: transparent; border: none; border-left: 1px solid %1;"
                    " border-radius: 0px; }")
                    .arg(utils::tokens::borderColor()));
                rowLayout->addWidget(pickButton);

                const QString paramName = param.name;
                const QString collectionId = param.collectionId;
                const QString displayField = (!param.collectionDisplayField.isEmpty())
                    ? param.collectionDisplayField
                    : (colIt != m_collections.constEnd() && !colIt->schema.isEmpty()
                        ? colIt->schema.first().name : QString());

                // Pré-seleção pelo último valor (id da entrada).
                if (colIt != m_collections.constEnd() && !initialValue.isEmpty()) {
                    const auto eit = std::find_if(colIt->entries.constBegin(), colIt->entries.constEnd(),
                        [&initialValue](const core::CollectionEntry &e) { return e.id == initialValue; });
                    if (eit != colIt->entries.constEnd()) {
                        m_collectionSelectionByParam[paramName] = {initialValue};
                        display->setText(eit->values.value(displayField));
                        display->setCursorPosition(0); // mostra o INÍCIO do texto, não o fim
                    }
                }

                connect(pickButton, &QToolButton::clicked, this,
                    [this, paramName, collectionId, displayField, display]() {
                        const auto cit = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
                            [&collectionId](const core::Collection &c) { return c.id == collectionId; });
                        if (cit == m_collections.constEnd()) {
                            return;
                        }
                        const QStringList history = m_usageHistory.value(paramName);
                        CollectionSelectorDialog dialog(*cit, history, /*multiSelect=*/true, this);
                        if (dialog.exec() != QDialog::Accepted) {
                            return;
                        }
                        // Persiste toggles de favorito feitos na tela de
                        // seleção: atualiza a coleção local e marca dirty.
                        if (dialog.favoritesChanged()) {
                            const core::Collection updated = dialog.updatedCollection();
                            for (core::Collection &c : m_collections) {
                                if (c.id == updated.id) { c = updated; break; }
                            }
                            m_collectionsChanged = true;
                        }
                        const QVector<core::CollectionEntry> chosen = dialog.selectedEntries();
                        QStringList ids;
                        QStringList labels;
                        for (const core::CollectionEntry &e : chosen) {
                            ids << e.id;
                            const QString lbl = e.values.value(displayField);
                            labels << (lbl.isEmpty() ? e.id : lbl);
                        }
                        m_collectionSelectionByParam[paramName] = ids;
                        display->setText(labels.join(QStringLiteral(", ")));
                        display->setCursorPosition(0); // mostra o INÍCIO do texto
                    });

                addField(param, wrapWithLabel(this, label, rowWidget));
                m_fieldByParamName[param.name] = display;
                break;
            }
            // MULTI-SELECT de opções fixas (feedback do usuário): lista
            // checkable em vez de combo de escolha única. Os valores marcados
            // são juntados por vírgula em values(). Pré-marca pelos valores do
            // último uso (initialValue = "a,b,c").
            if (param.multiSelect) {
                auto *container = new QWidget(this);
                container->setObjectName(QStringLiteral("paramMultiSelectWrap"));
                container->setStyleSheet(QStringLiteral(
                    "QWidget#paramMultiSelectWrap { background: transparent; }"));
                auto *vbox = new QVBoxLayout(container);
                vbox->setContentsMargins(0, 0, 0, 0);
                vbox->setSpacing(4);

                // Caixa de pesquisa: filtro simples em memória sobre as opções.
                // Lupa embutida à esquerda (ícone leading), igual à cara de um
                // campo de busca do sistema.
                auto *search = new QLineEdit(container);
                search->setPlaceholderText(utils::tr(QStringLiteral("params.multi_search.placeholder")));
                search->setClearButtonEnabled(true);
                // Mesmo fundo escuro (bg) e raio dos demais campos.
                search->setStyleSheet(QStringLiteral(
                    "QLineEdit { background-color: %1; color: %2; border: 1px solid %3;"
                    " border-radius: %4px; padding: %5px %6px; }")
                    .arg(utils::tokens::bg(), utils::tokens::fg(), utils::tokens::borderColor())
                    .arg(utils::tokens::radiusMd())
                    .arg(utils::tokens::space(2)).arg(utils::tokens::space(3)));
                search->addAction(LucideIcons::icon(QStringLiteral("search"),
                    QColor(utils::tokens::mutedFg()), 16), QLineEdit::LeadingPosition);
                vbox->addWidget(search);

                auto *list = new QListWidget(container);
                list->setSelectionMode(QAbstractItemView::NoSelection);
                list->setFrameShape(QFrame::NoFrame);
                // O viewport interno pintava um quadrado escuro POR TRÁS do
                // frame arredondado (o "duplo fundo" relatado). Deixamos o
                // viewport transparente; só o QListWidget desenha o fundo, com
                // o raio do tema (preferência de borda).
                list->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
                list->setStyleSheet(QStringLiteral(
                    "QListWidget { background-color: %1; border: 1px solid %2;"
                    " border-radius: %3px; padding: %4px; }"
                    "QListWidget::item { background: transparent; }")
                    .arg(utils::tokens::bg(), utils::tokens::borderColor())
                    .arg(utils::tokens::radiusMd())
                    .arg(utils::tokens::space(1)));
                const QStringList preset = initialValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
                for (const QString &opt : param.options) {
                    const int sep = opt.indexOf(QLatin1Char(':'));
                    const QString lbl = (sep > 0) ? opt.left(sep) : opt;
                    const QString val = (sep > 0) ? opt.mid(sep + 1) : opt;
                    auto *item = new QListWidgetItem(lbl, list);
                    item->setData(Qt::UserRole, val);
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setCheckState(preset.contains(val) ? Qt::Checked : Qt::Unchecked);
                }
                list->setMaximumHeight(utils::tokens::space(40));
                // Clicar em QUALQUER lugar da linha alterna o check.
                connect(list, &QListWidget::itemClicked, list, [](QListWidgetItem *it) {
                    if (!it) return;
                    it->setCheckState(it->checkState() == Qt::Checked
                                          ? Qt::Unchecked : Qt::Checked);
                });
                // Filtro in-mem: esconde itens cujo rótulo não contém o texto
                // (case-insensitive). Não altera o estado marcado dos ocultos —
                // o valor final continua vindo de todos os itens marcados.
                connect(search, &QLineEdit::textChanged, list, [list](const QString &text) {
                    const QString needle = text.trimmed().toLower();
                    for (int i = 0; i < list->count(); ++i) {
                        QListWidgetItem *it = list->item(i);
                        const bool match = needle.isEmpty()
                            || it->text().toLower().contains(needle);
                        it->setHidden(!match);
                    }
                });
                vbox->addWidget(list);

                addField(param, wrapWithLabel(this, label, container));
                m_fieldByParamName[param.name] = list; // values() lê a lista
                break;
            }
            auto *field = new QComboBox(this);
            // Cada option pode ser "label:value" (rótulo exibido != valor
            // injetado) ou apenas "value" (rótulo == valor).
            struct Opt { QString label; QString value; };
            QVector<Opt> opts;
            for (const QString &opt : param.options) {
                const int sep = opt.indexOf(QLatin1Char(':'));
                opts.push_back({(sep > 0) ? opt.left(sep) : opt,
                                (sep > 0) ? opt.mid(sep + 1) : opt});
            }

            // Ordenação por HISTÓRICO DE USO (feedback do usuário): os
            // valores usados mais recentemente vêm primeiro; o resto
            // mantém a ordem original de definição. stable_sort preserva a
            // ordem relativa dentro de cada grupo.
            const QStringList history = m_usageHistory.value(param.name);
            if (!history.isEmpty()) {
                std::stable_sort(opts.begin(), opts.end(), [&history](const Opt &a, const Opt &b) {
                    const int ia = history.indexOf(a.value);
                    const int ib = history.indexOf(b.value);
                    const int ra = (ia < 0) ? std::numeric_limits<int>::max() : ia;
                    const int rb = (ib < 0) ? std::numeric_limits<int>::max() : ib;
                    return ra < rb;
                });
            }

            for (const Opt &o : opts) {
                field->addItem(o.label, o.value);
            }

            // Busca avançada (feedback do usuário: listas de opções sem
            // pesquisa): combo editável com completer que filtra por
            // "contém" (não só prefixo), case-insensitive. O completer usa
            // o modelo do próprio combo, então respeita a ordem por
            // histórico. Como é editável, garantimos no values() que o
            // valor retornado corresponde a uma opção válida.
            field->setEditable(true);
            field->setInsertPolicy(QComboBox::NoInsert);
            if (auto *completer = field->completer()) {
                completer->setCaseSensitivity(Qt::CaseInsensitive);
                completer->setFilterMode(Qt::MatchContains);
                completer->setCompletionMode(QCompleter::PopupCompletion);
            }

            const int defaultIndex = field->findData(initialValue);
            if (defaultIndex >= 0) {
                field->setCurrentIndex(defaultIndex);
            } else {
                field->setCurrentIndex(0);
            }
            addField(param, wrapWithLabel(this, label, field));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Bool: {
            // Toggle switch com rótulo explícito (diretriz do usuário: troca
            // o checkbox isolado por um "pill switch" — reaproveita o MESMO
            // QCheckBox/kaiRole="switch" já usado no Settings, só troca a
            // pele) em vez de uma caixinha sem contexto.
            auto *field = new QCheckBox(utils::tr(QStringLiteral("params.bool.enable")), this);
            field->setProperty("kaiRole", QStringLiteral("switch"));
            field->setChecked(initialValue == QStringLiteral("true"));
            addField(param, wrapWithLabel(this, label, field));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Number: {
            // Parâmetro numérico: QSpinBox (inteiro) com faixa ampla. O valor
            // inicial vem do último uso/default. Fonte monoespaçada
            // (diretriz do usuário: "fonte monospace para leitura clara") —
            // os botões ▲/▼ já ganham destaque via QSS global do app.
            auto *field = new QSpinBox(this);
            field->setRange(-1000000000, 1000000000);
            QFont monoFont(utils::tokens::monoFamily());
            monoFont.setPointSize(field->font().pointSize());
            field->setFont(monoFont);
            bool ok = false;
            const int v = initialValue.toInt(&ok);
            field->setValue(ok ? v : 0);
            addField(param, wrapWithLabel(this, label, field));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::File: {
            // "Arquivo / Expressão" (diretriz do usuário): o botão [...] vira
            // uma EXTENSÃO do input (input-group-addon) — uma única borda em
            // volta do conjunto, sem gap/raio destoando entre campo e botão
            // (mesmo espírito do fix anterior no botão de procurar arquivo,
            // agora levado a um bloco fundido de verdade).
            auto *container = new QWidget(this);
            container->setObjectName(QStringLiteral("fileInputGroup"));
            container->setStyleSheet(QStringLiteral(
                "QWidget#fileInputGroup { background-color: %1; border: 1px solid %2;"
                " border-radius: %3px; }")
                .arg(utils::tokens::bg(), utils::tokens::borderColor())
                .arg(utils::tokens::radiusMd()));
            auto *rowLayout = new QHBoxLayout(container);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(0);

            auto *field = new QLineEdit(initialValue, container);
            field->setStyleSheet(QStringLiteral(
                "QLineEdit { background: transparent; color: %1; border: none;"
                " padding: %2px %3px; }")
                .arg(utils::tokens::fg())
                .arg(utils::tokens::space(2)).arg(utils::tokens::space(3)));
            // Botão de ícone (mesmo padrão do "pickButton" da Coleção acima e
            // do browseButton de parameter-editor-widget.cpp) — antes era um
            // QToolButton cru com texto "..." e chrome padrão do SO, destoando
            // da borda arredondada/tema do QLineEdit ao lado (bug relatado).
            // Agora colado ao campo, com só um separador fino entre os dois.
            // Ícone/tooltip refletem PASTA quando param.pickFolder (feedback
            // do usuário: "às vezes o param é uma pasta").
            auto *browseButton = makeIconButton(container,
                param.pickFolder ? QStringLiteral("folder") : QStringLiteral("file"),
                utils::tr(param.pickFolder ? QStringLiteral("params.folder.browse")
                                            : QStringLiteral("params.file.browse")),
                QColor(utils::tokens::accent()));
            browseButton->setStyleSheet(QStringLiteral(
                "QToolButton { background: transparent; border: none; border-left: 1px solid %1;"
                " border-radius: 0px; }")
                .arg(utils::tokens::borderColor()));

            // Pasta inicial configurada no parâmetro (campo "Pasta inicial" do
            // editor). Sem ela o QFileDialog abre no último diretório que o
            // processo visitou — na primeira vez, a pasta de instalação do Kai.
            const QString startDir = param.initialDir;
            const QString pathFormat = param.filePathFormat;
            const bool pickFolder = param.pickFolder;
            connect(browseButton, &QToolButton::clicked, this, [this, field, startDir, pathFormat, pickFolder]() {
                handleBrowseFileClicked(field, startDir, pathFormat, pickFolder);
            });

            rowLayout->addWidget(field, 1);
            rowLayout->addWidget(browseButton);

            addField(param, wrapWithLabel(this, label, container));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Textarea: {
            // TEXTAREA COM EXPANSÃO (pedido explícito do usuário, e depois
            // reforçado: "Text area campo precisa de um botão pra
            // expandir"). Reaproveita InlineCodeField — o MESMO widget
            // "campo compacto que cresce sozinho + botão de expandir para
            // um editor grande" já usado pelo Shell COMMAND e pelo HTTP
            // BODY em CommandEditorDialog — em vez da implementação de
            // auto-grow manual que existia aqui antes (calculava altura via
            // blockCount()/fontMetrics à mão): o app já tinha resolvido
            // exatamente este problema uma vez, reusar evita divergência de
            // comportamento entre os dois "campos de texto compactos" do
            // app. word-wrap (WidgetWidth, via editor() — texto livre, não
            // código) em vez do NoWrap padrão do widget (pensado pra
            // comando/JSON de uma linha lógica).
            auto *field = new InlineCodeField(this);
            field->setEditorTitle(label);
            field->setLineRange(3, 9); // mesmo range mínimo/máximo de antes
            field->setPlainField(true); // fundo de campo normal (não editor de código)
            field->editor()->setLineWrapMode(QPlainTextEdit::WidgetWidth);
            if (!initialValue.isEmpty()) {
                field->setPlainText(initialValue);
            }

            addField(param, wrapWithLabel(this, label, field));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Json: {
            // MINI EDITOR JSON (pedido do usuário: campo pra montar um
            // snippet que ele referencia via {{param}} dentro do Body/
            // Command). Também reaproveita InlineCodeField (mesmo motivo do
            // Textarea acima), com setJsonSyntax(true) para o realce —
            // ganha de graça o botão de expandir (2º pedido do usuário:
            // "JSON mesmo coisa [textarea, ou seja, botão de expandir]
            // além de ter botões de minify e etc."). O antigo
            // FoldableJsonView (com dobra/fold) foi trocado: numa altura de
            // 3-6 linhas o fold não tem serventia real, e ganhar expandir +
            // formatar/minificar de graça vale mais que manter a dobra
            // neste "mini campo" (palavra do próprio usuário).
            auto *field = new InlineCodeField(this);
            field->setEditorTitle(label);
            field->setJsonSyntax(true);
            field->setLineRange(3, 6);
            if (!initialValue.trimmed().isEmpty()) {
                field->setPlainText(initialValue);
            }

            // Rótulo + Formatar/Minificar na MESMA linha (mesmo padrão de
            // "COMMAND SCRIPT"+engrenagem / "BODY"+formatar do
            // CommandEditorDialog): os ícones operam sobre o texto do
            // próprio field, mesma lógica de parse+reserializar do
            // JsonEditorDialog::handleFormat/handleMinify (json.dialog.*).
            auto *labelRow = new QHBoxLayout();
            labelRow->setContentsMargins(0, 0, 0, 0);
            labelRow->setSpacing(utils::tokens::space(1));
            auto *jsonLabel = new QLabel(label, this);
            QFont jsonLabelFont = jsonLabel->font();
            jsonLabelFont.setBold(true);
            jsonLabel->setFont(jsonLabelFont);
            jsonLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::fg()));
            labelRow->addWidget(jsonLabel);
            labelRow->addStretch(1);
            auto *formatBtn = makeHeaderIconButton(this, QStringLiteral("braces"),
                utils::tr(QStringLiteral("json.dialog.format_title")));
            auto *minifyBtn = makeHeaderIconButton(this, QStringLiteral("shrink"),
                utils::tr(QStringLiteral("json.dialog.minify_title")));
            connect(formatBtn, &QToolButton::clicked, this, [this, field]() {
                const QString raw = field->toPlainText();
                if (raw.trimmed().isEmpty()) {
                    return;
                }
                QJsonParseError e;
                const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &e);
                if (e.error != QJsonParseError::NoError) {
                    QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.format_title")),
                        utils::tr(QStringLiteral("json.error.invalid_format")).arg(e.errorString()));
                    return;
                }
                field->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
            });
            connect(minifyBtn, &QToolButton::clicked, this, [this, field]() {
                const QString raw = field->toPlainText();
                if (raw.trimmed().isEmpty()) {
                    return;
                }
                QJsonParseError e;
                const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &e);
                if (e.error != QJsonParseError::NoError) {
                    QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.minify_title")),
                        utils::tr(QStringLiteral("json.error.invalid_format")).arg(e.errorString()));
                    return;
                }
                field->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
            });
            labelRow->addWidget(formatBtn);
            labelRow->addWidget(minifyBtn);

            auto *container = new QWidget(this);
            container->setObjectName(QStringLiteral("paramJsonWrap"));
            container->setStyleSheet(QStringLiteral(
                "QWidget#paramJsonWrap { background: transparent; }"));
            auto *vbox = new QVBoxLayout(container);
            vbox->setContentsMargins(0, 0, 0, 0);
            vbox->setSpacing(utils::tokens::space(1));
            vbox->addLayout(labelRow);
            vbox->addWidget(field);

            addField(param, container);
            m_fieldByParamName[param.name] = field;
            break;
        }
        }
    }
    flushPendingCompact();

    mainLayout->addLayout(grid);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);
}

void ParameterFormDialog::handleBrowseFileClicked(QLineEdit *targetField, const QString &initialDir,
                                                  const QString &pathFormat, bool pickFolder)
{
    // ONDE ABRIR. Ordem de precedência:
    //  1) a pasta do arquivo JÁ escolhido no campo (continuar de onde parou);
    //  2) a "Pasta inicial" configurada no parâmetro, com variáveis
    //     interpoladas ({{PROJECT_PATH}} etc.);
    //  3) nada — aí o Qt usa o último diretório do processo (comportamento
    //     anterior), que na primeira vez é a pasta de instalação do Kai.
    QString startDir;
    const QString current = targetField ? targetField->text().trimmed() : QString();
    if (!current.isEmpty()) {
        const QFileInfo info(current);
        if (info.exists()) {
            startDir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        }
    }
    if (startDir.isEmpty() && !initialDir.trimmed().isEmpty()) {
        // Só usa se EXISTIR: apontar para caminho inválido faz o diálogo nativo
        // abrir em lugar imprevisível, sem avisar. (Este diálogo não tem acesso
        // ao EnvironmentManager, então a pasta inicial é usada literalmente —
        // variáveis como {{PROJECT_PATH}} não são resolvidas aqui.)
        const QString resolved = initialDir.trimmed();
        if (QFileInfo::exists(resolved)) {
            startDir = resolved;
        }
    }

    // Sempre o diálogo nativo do sistema operacional (feedback
    // do usuário): QFileDialog::getOpenFileName/getExistingDirectory sem a
    // opção QFileDialog::DontUseNativeDialog usa o backend nativo por padrão.
    // PASTA em vez de arquivo (feedback do usuário: "às vezes o param é uma
    // pasta") — mesmo startDir/pathFormat, só troca o seletor.
    const QString path = pickFolder
        ? QFileDialog::getExistingDirectory(
              this, utils::tr(QStringLiteral("params.select_folder")), startDir)
        : QFileDialog::getOpenFileName(
              this, utils::tr(QStringLiteral("params.select_file")), startDir);
    if (!path.isEmpty()) {
        // Formato do path pedido no parâmetro (pedido do usuário: o
        // diálogo nativo devolve no formato do SO do Kai — sob WSLg isso
        // costuma ser um path Windows, inútil colado direto num comando
        // bash do lado Linux; "posix"/"windows" convertem, "native" não
        // mexe em nada).
        targetField->setText(utils::convertFilePathFormat(path, pathFormat));
    }
}

QMap<QString, QString> ParameterFormDialog::values() const
{
    QMap<QString, QString> result;

    for (const core::Parameter &param : m_params) {
        QWidget *field = m_fieldByParamName.value(param.name);
        if (!field) {
            continue;
        }

        switch (param.type) {
        case core::ParameterType::Text:
        case core::ParameterType::File: {
            auto *lineEdit = qobject_cast<QLineEdit *>(field);
            result[param.name] = lineEdit ? lineEdit->text() : QString();
            break;
        }
        case core::ParameterType::Select: {
            // Fonte de coleção: o valor é o(s) id(s) da(s) entrada(s)
            // escolhida(s) na tela de seleção dedicada. Para múltiplos,
            // junta por vírgula (o MainWindow expande os campos).
            if (!param.collectionId.isEmpty()) {
                result[param.name] = m_collectionSelectionByParam.value(param.name).join(QLatin1Char(','));
                break;
            }
            // Multi-select de opções fixas: junta os values marcados.
            if (param.multiSelect) {
                if (auto *list = qobject_cast<QListWidget *>(field)) {
                    QStringList chosen;
                    for (int i = 0; i < list->count(); ++i) {
                        QListWidgetItem *item = list->item(i);
                        if (item->checkState() == Qt::Checked) {
                            chosen << item->data(Qt::UserRole).toString();
                        }
                    }
                    result[param.name] = chosen.join(QLatin1Char(','));
                } else {
                    result[param.name] = QString();
                }
                break;
            }
            auto *comboBox = qobject_cast<QComboBox *>(field);
            if (!comboBox) {
                result[param.name] = QString();
                break;
            }
            // Combo editável (busca): o texto atual pode ser um rótulo
            // digitado. Resolve para o VALUE correspondente: 1) se o texto
            // casa exatamente com o rótulo de algum item, usa o value dele;
            // 2) senão, se casa com algum value, usa esse value; 3) senão,
            // cai no currentData (item selecionado) e, por fim, no texto.
            const QString text = comboBox->currentText();
            int idx = comboBox->findText(text);
            if (idx >= 0) {
                result[param.name] = comboBox->itemData(idx).toString();
            } else {
                idx = comboBox->findData(text);
                if (idx >= 0) {
                    result[param.name] = text;
                } else {
                    const QString data = comboBox->currentData().toString();
                    result[param.name] = data.isEmpty() ? text : data;
                }
            }
            break;
        }
        case core::ParameterType::Bool: {
            auto *checkBox = qobject_cast<QCheckBox *>(field);
            result[param.name] = (checkBox && checkBox->isChecked()) ? QStringLiteral("true") : QStringLiteral("false");
            break;
        }
        case core::ParameterType::Number: {
            auto *spin = qobject_cast<QSpinBox *>(field);
            result[param.name] = spin ? QString::number(spin->value()) : QStringLiteral("0");
            break;
        }
        case core::ParameterType::Textarea:
        case core::ParameterType::Json: {
            // Ambos são InlineCodeField agora (ver setupUi) — um único cast
            // serve pros dois tipos.
            auto *codeField = qobject_cast<InlineCodeField *>(field);
            result[param.name] = codeField ? codeField->toPlainText() : QString();
            break;
        }
        }
    }

    return result;
}

QMap<QString, QStringList> ParameterFormDialog::updatedUsageHistory() const
{
    // Parte do histórico existente e promove, para cada parâmetro, o valor
    // recém-escolhido ao topo (mais recente), sem duplicatas. Limita o
    // tamanho para não crescer sem controle. Aplica a todos os tipos, mas
    // é especialmente útil para Select (ordena as opções na próxima vez).
    constexpr int kMaxHistory = 50;
    QMap<QString, QStringList> updated = m_usageHistory;
    const QMap<QString, QString> chosen = values();
    for (auto it = chosen.constBegin(); it != chosen.constEnd(); ++it) {
        const QString &name = it.key();
        const QString &value = it.value();
        if (value.isEmpty()) {
            continue;
        }
        QStringList list = updated.value(name);
        list.removeAll(value);       // remove ocorrência anterior (se houver)
        list.prepend(value);         // mais recente no topo
        while (list.size() > kMaxHistory) {
            list.removeLast();
        }
        updated[name] = list;
    }
    return updated;
}

} // namespace kai::ui
