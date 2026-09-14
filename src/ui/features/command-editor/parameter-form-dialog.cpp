#include "ui/features/command-editor/parameter-form-dialog.h"
#include "ui/shared/dialog-utils.h"
#include "ui/features/collections/collection-selector-dialog.h"
#include "ui/shared/collection-chip-picker.h"
#include "ui/shared/date-picker-dialog.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/inline-code-field.h"
#include "ui/shared/parameter-field-factory.h"
#include "ui/shared/grid-param-field.h"
#include "core/grid-param.h"
#include "core/date-param-format.h"
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
#include <QPushButton>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QFont>
#include <QTextDocument>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QPoint>
#include <QTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QFrame>
#include <algorithm>
#include <memory>
#include <vector>
#include <limits>

#include "utils/translation-manager.h"

namespace kai::ui {

namespace {

// Prefixo BEM improvável de colidir com um nome de parâmetro de verdade —
// a checkbox "Informar <label>?" de um Parameter::optional pede pra
// LEMBRAR se estava marcada da última vez (pedido do usuário), e o jeito
// mais simples de persistir isso sem tocar em Command/MainWindow é reusar
// o MESMO mapa lastParamValues que já persiste os valores dos campos —
// esta chave sintética anda junto, sem exigir um campo novo no modelo.
QString optionalEnabledKey(const QString &paramName)
{
    return QStringLiteral("__kai_optional_enabled__%1").arg(paramName);
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
    case core::ParameterType::Grid:
        // Uma tabela precisa da largura toda.
        return false;
    case core::ParameterType::Date:
        // Largura cheia: um range formatado ("2024-01-15  —  2024-01-20")
        // não cabe confortavelmente numa meia-coluna de 280px.
        return false;
    }
    return false;
}

// Select cujas opções vêm de um script (Parameter::optionsScript) em vez de `options` fixas.
bool isScriptSelect(const core::Parameter &param)
{
    return param.type == core::ParameterType::Select && param.collectionId.isEmpty()
        && !param.optionsScript.trimmed().isEmpty();
}

// Esvazia o slot de um parâmetro-script (o widget antigo some no próximo ciclo do event loop).
void clearSlot(QWidget *slot)
{
    while (QLayoutItem *item = slot->layout()->takeAt(0)) {
        if (QWidget *old = item->widget()) {
            old->hide();
            old->deleteLater();
        }
        delete item;
    }
}

} // namespace

ParameterFormDialog::ParameterFormDialog(const QVector<core::Parameter> &params, QWidget *parent,
                                          const QMap<QString, QString> &lastValues,
                                          const QMap<QString, QStringList> &usageHistory,
                                          const QVector<core::Collection> &collections,
                                          const QString &description,
                                          const QMap<QString, core::CollectionFilterState> &collectionFilters)
    : QDialog(parent)
    , m_params(params)
    , m_lastValues(lastValues)
    , m_usageHistory(usageHistory)
    , m_collections(collections)
    , m_description(description)
    , m_collectionFilters(collectionFilters)
{
    setWindowTitle(utils::tr(QStringLiteral("params.title")));
    setSizeGripEnabled(true);
    setupUi(params);
    // O pedido sai depois do construtor: quem criou o diálogo só consegue conectar o sinal depois dele.
    QTimer::singleShot(0, this, [this]() {
        for (const core::Parameter &p : m_params) {
            if (isScriptSelect(p)) {
                requestScriptOptions(p.name);
            }
        }
    });
    // Tela CERNE do sistema (pedido explícito do usuário: "PENSE MUITO no
    // visual e responsividade, deixe ela padronizada, bem espaçosa e
    // grande") — largura mínima bem maior que antes: o layout de 2 colunas
    // (booleano/número/lookup lado a lado) ficava cramped num diálogo
    // estreito ("essa lógica de dois campos dividir a mesma linha não ficou
    // tão legal... o form tem pouco espaço horizontal" — a resposta é MAIS
    // espaço, não abandonar o layout de 2 colunas). Altura também mais
    // generosa. Ambos limitados pela tela disponível (responsividade: não
    // pode nascer maior que a tela em monitores pequenos).
    const bool spacious = !params.isEmpty() && params.size() <= 5;
    int maxW = 900;
    int maxH = 820;
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        if (avail.width() > 400) { maxW = static_cast<int>(avail.width() * 0.92); }
        if (avail.height() > 400) { maxH = static_cast<int>(avail.height() * 0.88); }
    }
    setMinimumWidth(qMin(spacious ? 860 : 800, maxW));
    adjustSize();
    // Piso de altura mais generoso (achado real, com foto: "por default
    // ficou com pouco espaço vertical, mal cabe na tela" — o campo de
    // chips de coleção, com busca+lupa em linha própria + separador +
    // área de chips, ocupa mais altura que o antigo campo readonly de uma
    // linha só; adjustSize() sozinho, através de um QScrollArea, tende a
    // subestimar a altura de verdade que um widget com wrap dinâmico
    // (FlowLayout) precisa antes de ser exibido pela primeira vez).
    if (height() < 500) {
        resize(width(), qMin(500, maxH));
    }
    // TETO de altura (pedido do usuário: "deixe ele menor, com scroll
    // interno") — acima disso, o QScrollArea da grade absorve o excesso em
    // vez do diálogo continuar crescendo com o conteúdo.
    if (height() > maxH) {
        resize(width(), maxH);
    }
    if (width() > maxW) {
        resize(maxW, height());
    }
    centerOnParent(this);
}

void ParameterFormDialog::setupUi(const QVector<core::Parameter> &params)
{
    // MODO ESPAÇOSO vs. COMPACTO (pedido do usuário: "o diálogo de
    // parâmetros está MUITO compacto... se tiver pouco campos, se tiver
    // mais de 5 usa aquele modo compacto") — poucos campos (≤5) ganham
    // margens/espaçamento maiores, aproveitando melhor um diálogo que
    // sobraria vazio; a partir de 6 mantém a densidade de sempre (o modo
    // que já existia, pensado pra caber muitos campos sem rolar demais).
    const bool spacious = !params.isEmpty() && params.size() <= 5;

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(spacious ? 34 : 26, spacious ? 32 : 26,
                                    spacious ? 34 : 26, spacious ? 28 : 22);
    mainLayout->setSpacing(spacious ? 24 : 18);

    // Título + subtítulo (mockup enviado pelo usuário): o subtítulo explica
    // o propósito da tela numa cor secundária, acima da grade de campos.
    // Topo do form: um HINT banner (mesmo padrão dos hints das Configurações
    // — pedido do usuário) com o DETALHAMENTO do comando quando preenchido;
    // se vazio, cai no texto genérico padrão ("insira os valores...").
    const QString hintText = m_description.trimmed().isEmpty()
        ? utils::tr(QStringLiteral("params.subtitle"))
        : m_description;
    // Cabeçalho: dica + etapas formam um bloco só (espaço curto entre eles); a dica é discreta para não competir
    // com os campos.
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("paramHeader"));
    header->setStyleSheet(QStringLiteral("QWidget#paramHeader { background: transparent; }"));
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(utils::tokens::space(3));
    QLabel *hint = layout_helpers::makeHintBanner(this, hintText);
    {
        QColor tint(utils::tokens::accent());
        tint.setAlphaF(0.10f);
        hint->setContentsMargins(utils::tokens::space(3), utils::tokens::space(2), utils::tokens::space(3), utils::tokens::space(2));
        hint->setStyleSheet(QStringLiteral(
            "QLabel#kaiHintBanner { background-color: rgba(%1, %2, %3, %4); border: none; border-radius: %5px;"
            " color: %6; font-size: %7pt; }")
            .arg(tint.red()).arg(tint.green()).arg(tint.blue()).arg(tint.alpha())
            .arg(utils::tokens::radiusMd()).arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    }
    headerLayout->addWidget(hint);
    mainLayout->addWidget(header);

    // Grade de 1 ou 2 colunas (diretriz do usuário — ver isCompactParam):
    // campos longos ocupam as duas colunas; campos compactos fluem lado a
    // lado, otimizando o aproveitamento vertical do diálogo.
    //
    // AGRUPAMENTO opcional (pedido do usuário: "possibilidade de criar
    // grupo de dados, o que irá começar colapsados", depois corrigido:
    // "ordem dos grupos ainda respeitar ordem dos params") — cada
    // parâmetro com um `group` não-vazio ganha sua PRÓPRIA grade,
    // embrulhada depois num CollapsibleSectionCard; parâmetros sem grupo
    // caem numa grade "solta". A ORDEM final segue a ordem de `params`:
    // cada `Segment` nasce na posição do primeiro parâmetro que o usa —
    // um grupo cujos parâmetros aparecem intercalados com soltos (ex:
    // solto, grupo A, solto de novo) faz o segmento solto ser "cortado"
    // em dois pedaços consecutivos em vez de um só, mas a ORDEM visual
    // continua fiel à declaração. `GridTarget` é o estado (grade/linha/
    // compacto pendente) que as closures abaixo manipulam — trocado a
    // cada parâmetro conforme seu grupo (ver `currentTarget` no laço).
    struct GridTarget {
        QGridLayout *grid;
        int row = 0;
        QWidget *pendingCompact = nullptr;
    };
    struct Segment {
        bool isGroup;
        QString groupName; // só quando isGroup
        GridTarget *target;
    };
    auto makeGrid = [&]() {
        auto *g = new QGridLayout();
        g->setHorizontalSpacing(utils::tokens::space(spacious ? 8 : 6));
        g->setVerticalSpacing(utils::tokens::space(spacious ? 8 : 6));
        g->setColumnStretch(0, 1);
        g->setColumnStretch(1, 1);
        return g;
    };

    // ETAPAS: cada etapa (Parameter::step) é uma página com os SEUS segmentos soltos/grupos. Sem nenhuma etapa
    // declarada há uma página só, igual a antes. Parâmetro sem etapa vai para a primeira página.
    for (const core::Parameter &p : params) {
        const QString step = p.step.trimmed();
        if (!step.isEmpty() && !m_stepNames.contains(step)) {
            m_stepNames << step;
        }
    }
    const int stepCount = qMax(1, int(m_stepNames.size()));
    for (const core::Parameter &p : params) {
        m_stepByParamName[p.name] = qMax(0, int(m_stepNames.indexOf(p.step.trimmed())));
    }

    std::vector<std::unique_ptr<GridTarget>> allTargets;
    QVector<QVector<Segment>> segmentsByStep(stepCount);
    QVector<QMap<QString, GridTarget *>> groupTargetsByStep(stepCount);
    QVector<GridTarget *> looseTargets(stepCount, nullptr); // segmento solto "aberto" em cada etapa
    GridTarget *currentTarget = nullptr;

    // Campo compacto "pendente": um compacto só é colocado na grade quando
    // sabemos se vai ter um par ao lado ou não. Sem isso, um compacto que
    // acaba sozinho na linha (único parâmetro do form, ou o último de uma
    // lista ímpar) ficava preso na coluna 0 com a coluna 1 vazia — bug
    // reportado ("quando só tem um parâmetro, o select fica ocupando só
    // uma célula, estranho"). Resolvido: guarda o compacto até o próximo
    // campo decidir seu destino (pareia com outro compacto, ou — se vier
    // um full ou o form acabar — vira full-width sozinho também.
    auto flushPendingCompact = [&]() {
        if (currentTarget->pendingCompact) {
            currentTarget->grid->addWidget(currentTarget->pendingCompact, currentTarget->row, 0, 1, 2);
            currentTarget->pendingCompact = nullptr;
            ++currentTarget->row;
        }
    };
    auto addFull = [&](QWidget *w) {
        flushPendingCompact();
        currentTarget->grid->addWidget(w, currentTarget->row, 0, 1, 2);
        ++currentTarget->row;
    };
    auto addCompact = [&](QWidget *w) {
        if (currentTarget->pendingCompact) {
            currentTarget->grid->addWidget(currentTarget->pendingCompact, currentTarget->row, 0);
            currentTarget->grid->addWidget(w, currentTarget->row, 1);
            currentTarget->pendingCompact = nullptr;
            ++currentTarget->row;
        } else {
            currentTarget->pendingCompact = w;
        }
    };
    auto addField = [&](const core::Parameter &param, QWidget *w) {
        if (param.optional) {
            // OPCIONAL (pedido do usuário): o campo de verdade nasce
            // ESCONDIDO atrás de uma checkbox "Informar <label>?" — só
            // aparece quando o usuário marca que quer preenchê-lo. Sempre
            // full-width (a altura de `w` varia ao aparecer/sumir, o que
            // bagunçaria o pareamento lado-a-lado dos campos compactos).
            auto *container = new QWidget(this);
            // Transparente, QUALIFICADO por objectName (bug relatado: "o
            // fundo do parâmetro opcional ficou visível, não deveria ter
            // fundo" — a regra global "QWidget { background-color: bg }"
            // do tema pinta QUALQUER QWidget puro sem isto; mesma técnica
            // do wrapWithLabel logo acima, restrita ao próprio wrap pra não
            // vazar transparência aos filhos — ver comentário lá).
            container->setObjectName(QStringLiteral("paramOptionalWrap"));
            container->setStyleSheet(QStringLiteral(
                "QWidget#paramOptionalWrap { background: transparent; }"));
            auto *vbox = new QVBoxLayout(container);
            vbox->setContentsMargins(0, 0, 0, 0);
            vbox->setSpacing(utils::tokens::space(1));
            const QString label = param.label.isEmpty() ? param.name : param.label;
            auto *checkbox = new QCheckBox(
                utils::tr(QStringLiteral("params.optional.ask")).arg(label), container);
            checkbox->setProperty("kaiRole", QStringLiteral("switch"));
            // Lembra se estava marcada da última vez (pedido do usuário) —
            // mesma fonte (lastValues) que já pré-preenche o resto do form.
            checkbox->setChecked(m_lastValues.value(optionalEnabledKey(param.name)) == QStringLiteral("1"));
            vbox->addWidget(checkbox);
            w->setParent(container);
            w->setVisible(checkbox->isChecked());
            vbox->addWidget(w);
            // Ao expandir, o diálogo cresce sozinho (Qt propaga o sizeHint
            // maior pro layout automaticamente) — mas ao encolher de volta
            // NÃO, o diálogo fica "esticado" mesmo com o campo escondido de
            // novo (achado real: "ao expandir, e encolher, o form não
            // volta ao tamanho original"). QTimer::singleShot(0, ...)
            // porque o layout só recalcula o sizeHint DEPOIS deste evento
            // de toggle; resize() só na ALTURA preserva uma largura que o
            // usuário tenha ajustado manualmente (setSizeGripEnabled).
            connect(checkbox, &QCheckBox::toggled, this, [this, w](bool checked) {
                w->setVisible(checked);
                QTimer::singleShot(0, this, [this]() {
                    resize(width(), sizeHint().height());
                });
            });
            m_optionalCheckboxByParamName[param.name] = checkbox;
            addFull(container);
            return;
        }
        if (isCompactParam(param)) {
            addCompact(w);
        } else {
            addFull(w);
        }
    };

    for (const core::Parameter &param : params) {
        const int stepIdx = m_stepByParamName.value(param.name, 0);
        GridTarget *&looseTarget = looseTargets[stepIdx];
        QMap<QString, GridTarget *> &groupTargetByName = groupTargetsByStep[stepIdx];
        QVector<Segment> &segments = segmentsByStep[stepIdx];
        const QString groupName = param.group.trimmed();
        if (groupName.isEmpty()) {
            if (!looseTarget) {
                // Novo segmento solto — ou é o primeiro, ou o anterior foi
                // "interrompido" por um grupo entre um solto e outro (ver
                // comentário acima do struct Segment).
                allTargets.push_back(std::make_unique<GridTarget>(GridTarget{makeGrid()}));
                looseTarget = allTargets.back().get();
                segments.push_back(Segment{false, QString(), looseTarget});
            }
            currentTarget = looseTarget;
        } else {
            looseTarget = nullptr; // fecha o segmento solto corrente, se houver
            if (!groupTargetByName.contains(groupName)) {
                allTargets.push_back(std::make_unique<GridTarget>(GridTarget{makeGrid()}));
                groupTargetByName[groupName] = allTargets.back().get();
                segments.push_back(Segment{true, groupName, groupTargetByName.value(groupName)});
            }
            currentTarget = groupTargetByName.value(groupName);
        }

        const QString label = param.label.isEmpty() ? param.name : param.label;
        // Valor inicial: último valor informado (se houver) tem prioridade
        // sobre o defaultValue do parâmetro (salvar últimos params).
        const QString initialValue = m_lastValues.contains(param.name)
            ? m_lastValues.value(param.name)
            : param.defaultValue;

        switch (param.type) {
        case core::ParameterType::Text: {
            // Placeholder (diretriz da tela de Params: "use texto de
            // placeholder... pra guiar o usuário quando o campo estiver
            // vazio") — a Description do parâmetro (feature CLI Paths, só
            // usada até agora no --help) já é uma dica melhor que um
            // genérico "Digite aqui..." quando o autor do comando a
            // preencheu; cai pro texto genérico quando não há.
            auto *field = fields::makeTextField(this, initialValue, param.description.trimmed().isEmpty()
                ? utils::tr(QStringLiteral("params.text.placeholder"))
                : param.description.trimmed());
            addField(param, fields::wrapWithLabel(this, label, field, param.required && !param.optional));
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

                const QString paramName = param.name;
                const QString collectionId = param.collectionId;
                // Bug relatado: "ele ta renderizando o id, era pra
                // renderizar o nome" — ver core::resolveCollectionDisplayField
                // (um campo Key, mesmo CONFIGURADO explicitamente em
                // collectionDisplayField, nunca é uma exibição útil; o
                // editor de parâmetros pré-seleciona o 1º campo do schema
                // nesse combo, que no schema padrão [Key, Value] É a Key).
                const QString displayField = (colIt != m_collections.constEnd())
                    ? core::resolveCollectionDisplayField(*colIt, param.collectionDisplayField)
                    : param.collectionDisplayField;

                // Campo de chips com busca embutida (feedback do usuário:
                // "ao digitar no campo ele funciona tipo uma lista... vai
                // criando a chip, e posso ir removendo ou adicionar multi
                // sem nem abrir nada") — substitui o antigo campo readonly
                // que só funcionava clicando na lupa. O botão de lupa
                // continua disponível (pickButton()) pra quem quiser a tela
                // de seleção dedicada completa (favoritos, filtros por
                // campo, paginação).
                auto *chipPicker = new CollectionChipPickerWidget(this);
                if (colIt != m_collections.constEnd()) {
                    chipPicker->setEntries(colIt->entries, displayField);
                }

                // Pré-seleção pelo(s) último(s) valor(es) (id(s) da entrada,
                // separados por vírgula quando múltiplos — mesmo formato que
                // values() grava via ids.join(',') logo abaixo). Split ANTES
                // de comparar: bug real (reportado: "se eu executar um cmd de
                // coleções ele não lembra o ultimo valor executado, multiplas
                // entries") — comparar initialValue INTEIRO ("id1,id2") contra
                // um único e.id nunca batia pra mais de uma entrada
                // selecionada, deixando a seleção em branco ao reabrir. Mesmo
                // padrão de split já usado no campo multi-select comum,
                // algumas linhas abaixo.
                if (colIt != m_collections.constEnd() && !initialValue.isEmpty()) {
                    const QStringList preset = initialValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
                    QStringList ids;
                    for (const QString &entryId : preset) {
                        const auto eit = std::find_if(colIt->entries.constBegin(), colIt->entries.constEnd(),
                            [&entryId](const core::CollectionEntry &e) { return e.id == entryId; });
                        if (eit != colIt->entries.constEnd()) {
                            ids << eit->id;
                        }
                    }
                    if (!ids.isEmpty()) {
                        m_collectionSelectionByParam[paramName] = ids;
                        chipPicker->setSelectedIds(ids);
                    }
                }

                connect(chipPicker, &CollectionChipPickerWidget::selectionChanged, this,
                    [this, paramName, chipPicker]() {
                        m_collectionSelectionByParam[paramName] = chipPicker->selectedIds();
                    });
                connect(chipPicker, &CollectionChipPickerWidget::selectionChanged, this,
                    &ParameterFormDialog::refreshValidationState);

                connect(chipPicker->pickButton(), &QToolButton::clicked, this,
                    [this, paramName, collectionId, chipPicker]() {
                        const auto cit = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
                            [&collectionId](const core::Collection &c) { return c.id == collectionId; });
                        if (cit == m_collections.constEnd()) {
                            return;
                        }
                        const QStringList history = m_usageHistory.value(paramName);
                        CollectionSelectorDialog dialog(*cit, history, /*multiSelect=*/true, this,
                            m_collectionFilters.value(collectionId));
                        const int result = dialog.exec();
                        // Busca/favoritos-only persistem mesmo se o usuário
                        // CANCELAR a escolha — é conveniência de navegação da
                        // tela, não dado de seleção (pedido do usuário: "os
                        // filtros de coleções devem ser salvos").
                        m_collectionFilters[collectionId] = dialog.filterState();
                        if (result != QDialog::Accepted) {
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
                        for (const core::CollectionEntry &e : chosen) {
                            ids << e.id;
                        }
                        m_collectionSelectionByParam[paramName] = ids;
                        chipPicker->setSelectedIds(ids);
                        refreshValidationState();
                    });

                addField(param, fields::wrapWithLabel(this, label, chipPicker, param.required && !param.optional));
                m_fieldByParamName[param.name] = chipPicker;
                break;
            }
            // Opções vindas de um SCRIPT: o campo de verdade só nasce quando o script devolve as linhas
            // (ver setScriptOptions); até lá um combo desabilitado "Carregando…".
            if (isScriptSelect(param)) {
                auto *host = new QWidget(this);
                host->setObjectName(QStringLiteral("paramScriptWrap"));
                host->setStyleSheet(QStringLiteral("QWidget#paramScriptWrap { background: transparent; }"));
                auto *vbox = new QVBoxLayout(host);
                vbox->setContentsMargins(0, 0, 0, 0);
                vbox->setSpacing(utils::tokens::space(1));
                auto *row = new QHBoxLayout();
                row->setSpacing(utils::tokens::space(2));
                auto *slot = new QWidget(host);
                slot->setObjectName(QStringLiteral("paramScriptSlot"));
                slot->setStyleSheet(QStringLiteral("QWidget#paramScriptSlot { background: transparent; }"));
                auto *slotLayout = new QVBoxLayout(slot);
                slotLayout->setContentsMargins(0, 0, 0, 0);
                slotLayout->setSpacing(0);
                row->addWidget(slot, 1);
                auto *reload = makeIconButton(host, QStringLiteral("refresh-cw"),
                    utils::tr(QStringLiteral("params.script.reload.tip")), QColor(utils::tokens::accent()));
                connect(reload, &QToolButton::clicked, this, [this, name = param.name]() { requestScriptOptions(name); });
                row->addWidget(reload, 0, Qt::AlignTop);
                vbox->addLayout(row);
                auto *status = new QLabel(host);
                status->setWordWrap(true);
                status->setVisible(false);
                status->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                                          .arg(utils::tokens::errorFg()));
                vbox->addWidget(status);
                m_scriptSlotByParamName[param.name] = slot;
                m_scriptStatusByParamName[param.name] = status;
                showScriptPlaceholder(param.name, utils::tr(QStringLiteral("params.script.loading")));
                addField(param, fields::wrapWithLabel(this, label, host, param.required && !param.optional));
                break;
            }
            // MULTI-SELECT de opções fixas (feedback do usuário): lista
            // checkable em vez de combo de escolha única. Os valores marcados
            // são juntados por vírgula em values(). Pré-marca pelos valores do
            // último uso (initialValue = "a,b,c").
            if (param.multiSelect) {
                QVector<fields::ChoiceOption> options;
                for (const QString &opt : param.options) {
                    options.push_back(fields::parseParamOption(opt));
                }
                const fields::ChoiceListField multi = fields::makeCheckList(
                    this, options, initialValue.split(QLatin1Char(','), Qt::SkipEmptyParts), /*withFilter=*/true);
                m_multiSelectFilterByParamName[param.name] = multi.filter;

                addField(param, fields::wrapWithLabel(this, label, multi.container, param.required && !param.optional));
                m_fieldByParamName[param.name] = multi.list; // values() lê a lista
                break;
            }
            // Cada option pode ser "label:value" (rótulo exibido != valor
            // injetado) ou apenas "value" (rótulo == valor).
            QVector<fields::ChoiceOption> options;
            for (const QString &opt : param.options) {
                options.push_back(fields::parseParamOption(opt));
            }
            // Obrigatório sem default/último valor válido: abre em "nada escolhido" em vez de pré-selecionar a 1ª opção.
            const bool hasInitial = std::any_of(options.cbegin(), options.cend(),
                [&initialValue](const fields::ChoiceOption &o) { return o.value == initialValue; });
            const QString placeholder = param.required && !param.optional && !hasInitial
                ? utils::tr(QStringLiteral("params.error.choose")) : QString();
            auto *field = fields::makeSelectCombo(this, options, m_usageHistory.value(param.name), initialValue, placeholder);
            addField(param, fields::wrapWithLabel(this, label, field, param.required && !param.optional));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Bool: {
            auto *field = fields::makeSwitchField(this, utils::tr(QStringLiteral("params.bool.enable")),
                                                  initialValue == QStringLiteral("true"));
            addField(param, fields::wrapWithLabel(this, label, field, param.required && !param.optional));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Number: {
            auto *field = fields::makeSpinField(this, initialValue);
            addField(param, fields::wrapWithLabel(this, label, field, param.required && !param.optional));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::File: {
            const fields::PathPickField picked = fields::makePathPickField(
                this, initialValue, param.pickMode, QString(), param.filePathFormat);
            addField(param, fields::wrapWithLabel(this, label, picked.container, param.required && !param.optional));
            m_fieldByParamName[param.name] = picked.edit;
            break;
        }
        case core::ParameterType::Date: {
            const QString dateFormat = param.dateFormat;
            const QString dateFormatCustom = param.dateFormatCustom;
            const fields::DatePickField picked = fields::makeDatePickField(
                this, initialValue, param.dateMode, param.dateRange,
                [dateFormat, dateFormatCustom](const QDateTime &dt) {
                    return core::formatDateParamValue(dt, dateFormat, dateFormatCustom);
                });
            addField(param, fields::wrapWithLabel(this, label, picked.container, param.required && !param.optional));
            m_fieldByParamName[param.name] = picked.edit;
            break;
        }
        case core::ParameterType::Textarea: {
            // TEXTAREA COM EXPANSÃO (pedido explícito do usuário: "campo de
            // texto com expansão" + botão pra expandir).
            auto *field = fields::makeTextareaField(this, label, param.description.trimmed().isEmpty()
                ? utils::tr(QStringLiteral("params.text.placeholder"))
                : param.description.trimmed(), initialValue);
            addField(param, fields::wrapWithLabel(this, label, field, param.required && !param.optional));
            m_fieldByParamName[param.name] = field;
            break;
        }
        case core::ParameterType::Grid: {
            auto *grid = new GridParamField(param.gridColumns, this);
            const QString savedKey = QStringLiteral("%1__json").arg(param.name);
            if (m_lastValues.contains(savedKey)) {
                grid->setRows(core::gridRowsFromJson(m_lastValues.value(savedKey)));
            } else {
                grid->addRow();
            }
            addField(param, fields::wrapWithLabel(this, label, grid, param.required && !param.optional));
            m_fieldByParamName[param.name] = grid;
            break;
        }
        case core::ParameterType::Json: {
            // MINI EDITOR JSON (pedido do usuário: campo pra montar um
            // snippet que ele referencia via {{param}} dentro do Body/Command).
            const fields::JsonField json = fields::makeJsonField(this, label, initialValue);
            addField(param, json.container);
            m_fieldByParamName[param.name] = json.field;
            break;
        }
        }
    }
    // Fecha o compacto pendente de CADA grade (cada segmento solto e cada
    // grupo), não só da última usada no laço acima.
    for (const auto &owned : allTargets) {
        currentTarget = owned.get();
        flushPendingCompact();
    }

    // Diálogo "menor, com scroll interno" (pedido do usuário: form ficava
    // enorme/mal espaçado com muitos parâmetros) — o conteúdo (segmentos
    // soltos + um CollapsibleSectionCard colapsado por padrão por grupo,
    // montados na ORDEM de `segments` — ver comentário acima do struct
    // Segment) vive dentro de um QScrollArea; é a altura do VIEWPORT que é
    // limitada no construtor (ver ParameterFormDialog::ParameterFormDialog),
    // não a do conteúdo — ele pode crescer à vontade e rolar.
    auto buildPage = [&](const QVector<Segment> &segs) -> QScrollArea * {
        auto *contentHost = new QWidget(this);
        // Transparente (achado real: "fundo ficou visualmente feio" — o
        // QScrollArea/viewport/host sem isto usam o branco padrão da paleta,
        // que vaza como uma tarja clara nas bordas por cima do tema escuro;
        // mesma técnica de wrap transparente qualificado por objectName usada
        // pelos outros containers deste arquivo, ver comentário em paramOptionalWrap).
        contentHost->setObjectName(QStringLiteral("paramScrollHost"));
        contentHost->setStyleSheet(QStringLiteral(
            "QWidget#paramScrollHost { background: transparent; }"));
        auto *contentLayout = new QVBoxLayout(contentHost);
        // Respiro dentro da área rolável: os campos não encostam na barra de rolagem (à direita) e o anel de foco do
        // primeiro/último campo não é cortado. A esquerda fica rente ao cabeçalho e ao rodapé, para alinhar com eles.
        contentLayout->setContentsMargins(0, utils::tokens::space(1), utils::tokens::space(3), utils::tokens::space(2));
        contentLayout->setSpacing(spacious ? 24 : 18);
        for (const Segment &seg : segs) {
            if (!seg.isGroup) {
                if (seg.target->row > 0) {
                    contentLayout->addLayout(seg.target->grid);
                } else {
                    delete seg.target->grid;
                }
                continue;
            }
            // Achado real, com print: o corpo do card aparecia com um retângulo
            // QUADRADO por trás dos campos, sem seguir o raio de borda do tema
            // ("não segue a preferência de borda e está errado") — este
            // QWidget "cru" (sem objectName/stylesheet próprios) herdava a
            // regra global "QWidget { background-color: bg }" e pintava um
            // fundo chapado, retangular, por cima do fundo (corretamente
            // arredondado) do card por trás dele. Mesma técnica de wrap
            // transparente qualificado por objectName usada em todo o resto
            // deste arquivo (paramFieldWrap, paramOptionalWrap etc.).
            auto *body = new QWidget(this);
            body->setObjectName(QStringLiteral("paramGroupBody"));
            body->setStyleSheet(QStringLiteral("QWidget#paramGroupBody { background: transparent; }"));
            body->setLayout(seg.target->grid);
            auto *card = new kai::ui::CollapsibleSectionCard(seg.groupName, this);
            card->setAlwaysShowBody(true);
            // SEM badge de contagem (achado real: "esse contador que vc colocou
            // do lado do acesso... conta o que? REMOVA ISSO ALI, não conta
            // nada" — tentativa anterior mostrava obrigatórios pendentes do
            // grupo, mas isso raramente tem significado real pro usuário: a
            // maioria dos grupos nem tem nenhum parâmetro marcado `required`,
            // então o badge só mostrava "0" sem dizer nada útil).
            card->setShowCountBadge(false);
            card->setExpanded(false, false);
            card->setBody(body);
            // Foco automático no filtro do multi-select ao EXPANDIR o grupo que
            // o contém (diretriz da tela de Params — adaptada pro caso deste
            // form: o multi-select já vive sempre visível dentro do grupo, sem
            // popover próprio, então "abrir" aqui é abrir o grupo). Só o
            // PRIMEIRO multi-select do grupo recebe foco, se houver mais de um.
            for (const core::Parameter &p : m_params) {
                if (p.group.trimmed() != seg.groupName) { continue; }
                if (QLineEdit *filter = m_multiSelectFilterByParamName.value(p.name)) {
                    connect(card, &CollapsibleSectionCard::expandedChanged, this,
                        [filter](bool expanded) {
                            if (expanded) { filter->setFocus(Qt::OtherFocusReason); }
                        });
                    break;
                }
            }
            // NADA de override de cor aqui — duas tentativas anteriores
            // (bg, depois surface) tentaram "casar" o card com o fundo do
            // diálogo, mas isso destoava do padrão real do resto do app: o
            // MESMO componente (CollapsibleSectionCard) já aparece assim no
            // editor de Comando ("Variáveis exportáveis", "Parâmetros
            // Dinâmicos" etc.) usando a cor PRÓPRIA dele (surface2) — achado
            // real, com print comparando as duas telas: "a aba de acesso ainda
            // não está com a cor correta, como visto na tela de edição de cmd
            // em tabelas". O certo é deixar o estilo PADRÃO do componente,
            // sem override — surface2 + borda + raio, igual em toda parte.
            contentLayout->addWidget(card);
        }
        contentLayout->addStretch(1);

        auto *scrollArea = new QScrollArea(this);
        scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
        scrollArea->setWidget(contentHost);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // O VIEWPORT é um QWidget filho separado do scroll area (não pega o
        // "background: transparent" do seletor acima sozinho) — sem isto, ele
        // usa o branco padrão da paleta e vaza uma tarja clara nas bordas.
        //
        // BUG REAL sério encontrado aqui (print + investigação pixel-a-pixel,
        // comparando o QSS gerado com a cor de fato renderizada): uma regra SEM
        // seletor ("background: transparent;" cru) aplicada via setStyleSheet()
        // direto no viewport QUEBRA a cascata de QSS para TODO DESCENDENTE dele
        // — TODOS os campos do form (Par1, Par4 etc.) paravam de pegar a regra
        // global "QLineEdit/QSpinBox {background-color: bg}" e caíam de volta na
        // cor do PRÓPRIO DIÁLOGO (surface), mesmo sem nenhuma regra própria nos
        // campos — exatamente o "campo devia ter o fundo padrão, mas não tem"
        // relatado. QUALIFICAR a regra por objectName resolve — comprovado
        // isolando o bug num repro mínimo antes de mexer aqui: a versão crua
        // quebra a cascata, a versão qualificada não.
        scrollArea->viewport()->setObjectName(QStringLiteral("paramScrollViewport"));
        scrollArea->viewport()->setStyleSheet(QStringLiteral(
            "QWidget#paramScrollViewport { background: transparent; }"));
        return scrollArea;
    };

    if (stepCount > 1) {
        auto *stepper = new QWidget(this);
        stepper->setObjectName(QStringLiteral("paramStepper"));
        auto *stepperLayout = new QHBoxLayout(stepper);
        stepperLayout->setContentsMargins(0, 0, 0, 0);
        stepperLayout->setSpacing(utils::tokens::space(2));
        stepper->setStyleSheet(QStringLiteral(
            "QWidget#paramStepper { background: transparent; }"
            "QPushButton#paramStepChip { background: transparent; color: %1; border: 1px solid %2; border-radius: %3px; padding: %4px %5px; }"
            "QPushButton#paramStepChip:disabled { color: %1; }"
            "QPushButton#paramStepChip[current=\"true\"] { color: %6; border-color: %7; font-weight: 600; }")
            .arg(utils::tokens::mutedFg(), utils::tokens::borderColor()).arg(utils::tokens::radiusMd())
            .arg(utils::tokens::space(1)).arg(utils::tokens::space(3)).arg(utils::tokens::fg(), utils::tokens::accent()));
        for (int i = 0; i < m_stepNames.size(); ++i) {
            auto *chip = new QPushButton(QStringLiteral("%1. %2").arg(i + 1).arg(m_stepNames.at(i)), stepper);
            chip->setObjectName(QStringLiteral("paramStepChip"));
            chip->setCursor(Qt::PointingHandCursor);
            chip->setFocusPolicy(Qt::NoFocus);
            connect(chip, &QPushButton::clicked, this, [this, i]() { goToStep(i); });
            stepperLayout->addWidget(chip);
            m_stepChips.append(chip);
        }
        stepperLayout->addStretch(1);
        headerLayout->addWidget(stepper);

        m_stepStack = new QStackedWidget(this);
        m_stepStack->setObjectName(QStringLiteral("paramStepStack"));
        m_stepStack->setStyleSheet(QStringLiteral("QStackedWidget#paramStepStack { background: transparent; }"));
        for (int i = 0; i < stepCount; ++i) {
            m_pages.append(buildPage(segmentsByStep.at(i)));
            m_stepStack->addWidget(m_pages.last());
        }
        mainLayout->addWidget(m_stepStack, 1);
    } else {
        m_pages.append(buildPage(segmentsByStep.at(0)));
        mainLayout->addWidget(m_pages.first(), 1);
    }

    m_problemsLabel = new QLabel(this);
    m_problemsLabel->setObjectName(QStringLiteral("paramProblems"));
    m_problemsLabel->setTextFormat(Qt::RichText);
    m_problemsLabel->setWordWrap(true);
    m_problemsLabel->setStyleSheet(QStringLiteral(
        "QLabel#paramProblems { background-color: rgba(%5, %6, %7, 28); color: %1; border: 1px solid rgba(%5, %6, %7, 110);"
        " border-radius: %2px; padding: %3px %4px; }")
        .arg(utils::tokens::errorFg()).arg(utils::tokens::radiusMd()).arg(utils::tokens::space(2)).arg(utils::tokens::space(3))
        .arg(QColor(utils::tokens::errorFg()).red()).arg(QColor(utils::tokens::errorFg()).green())
        .arg(QColor(utils::tokens::errorFg()).blue()));
    connect(m_problemsLabel, &QLabel::linkActivated, this, &ParameterFormDialog::focusParam);
    mainLayout->addWidget(m_problemsLabel);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_okText = m_okButton->text();
    // Nas etapas o botão principal vira "Próximo" até a última página; só a última aceita o diálogo.
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_currentStep < int(m_stepNames.size()) - 1) {
            goToStep(m_currentStep + 1);
        } else {
            accept();
        }
    });

    // "Voltar" fica fora da caixa de botões (cuja ordem depende da plataforma): à esquerda, com OK/Cancelar à direita.
    auto *footer = new QHBoxLayout();
    if (stepCount > 1) {
        m_backButton = new QPushButton(utils::tr(QStringLiteral("params.step.back")), this);
        m_backButton->setObjectName(QStringLiteral("paramStepBack"));
        connect(m_backButton, &QPushButton::clicked, this, [this]() { goToStep(m_currentStep - 1); });
        footer->addWidget(m_backButton);
    }
    footer->addStretch(1);
    footer->addWidget(buttonBox);
    mainLayout->addLayout(footer);

    // Validação ao vivo (diretrizes da tela de Params, pedido do usuário):
    // o botão OK começa desabilitado e só liga quando todo parâmetro
    // obrigatório tem valor — conectado ao sinal de mudança de CADA campo,
    // qualquer que seja seu tipo, num laço só (em vez de espalhar um
    // connect() por case do switch acima). O badge de cada grupo também é
    // recalculado junto (ver refreshValidationState).
    for (auto it = m_fieldByParamName.constBegin(); it != m_fieldByParamName.constEnd(); ++it) {
        QWidget *w = it.value();
        if (auto *field = qobject_cast<InlineCodeField *>(w)) {
            connect(field, &InlineCodeField::textChanged, this, &ParameterFormDialog::refreshValidationState);
        } else if (auto *combo = qobject_cast<QComboBox *>(w)) {
            connect(combo, &QComboBox::currentIndexChanged, this, &ParameterFormDialog::refreshValidationState);
            connect(combo, &QComboBox::editTextChanged, this, &ParameterFormDialog::refreshValidationState);
        } else if (auto *list = qobject_cast<QListWidget *>(w)) {
            connect(list, &QListWidget::itemChanged, this, &ParameterFormDialog::refreshValidationState);
        } else if (auto *spin = qobject_cast<QSpinBox *>(w)) {
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ParameterFormDialog::refreshValidationState);
        } else if (auto *cb = qobject_cast<QCheckBox *>(w)) {
            connect(cb, &QCheckBox::toggled, this, &ParameterFormDialog::refreshValidationState);
        } else if (auto *edit = qobject_cast<QLineEdit *>(w)) {
            connect(edit, &QLineEdit::textChanged, this, &ParameterFormDialog::refreshValidationState);
        } else if (auto *grid = qobject_cast<GridParamField *>(w)) {
            connect(grid, &GridParamField::changed, this, &ParameterFormDialog::refreshValidationState);
        }
    }
    // Parâmetros OPCIONAIS revalidam ao marcar/desmarcar a checkbox "Informar
    // <label>?" também — ligar/desligar muda se o campo conta pra
    // obrigatoriedade (ele nunca conta, mas ligar pode revelar um campo cujo
    // PRÓPRIO estado interno já estava "vazio", então o badge/OK do grupo
    // que o contém — se algum dia um opcional entrar num grupo — também
    // deve refletir a mudança de visibilidade).
    for (auto it = m_optionalCheckboxByParamName.constBegin();
         it != m_optionalCheckboxByParamName.constEnd(); ++it) {
        connect(it.value(), &QCheckBox::toggled, this, &ParameterFormDialog::refreshValidationState);
    }

    refreshValidationState(); // estado inicial (antes de qualquer interação)
}

void ParameterFormDialog::showScriptPlaceholder(const QString &paramName, const QString &text)
{
    QWidget *slot = m_scriptSlotByParamName.value(paramName);
    if (!slot) {
        return;
    }
    m_fieldByParamName.remove(paramName);
    clearSlot(slot);
    auto *placeholder = new QComboBox(slot);
    placeholder->addItem(text);
    placeholder->setEnabled(false);
    slot->layout()->addWidget(placeholder);
}

void ParameterFormDialog::requestScriptOptions(const QString &paramName)
{
    const auto it = std::find_if(m_params.constBegin(), m_params.constEnd(),
        [&paramName](const core::Parameter &p) { return p.name == paramName; });
    if (it == m_params.constEnd()) {
        return;
    }
    if (QLabel *status = m_scriptStatusByParamName.value(paramName)) {
        status->setVisible(false);
    }
    showScriptPlaceholder(paramName, utils::tr(QStringLiteral("params.script.loading")));
    refreshValidationState();
    emit optionsScriptRequested(paramName, it->optionsScript);
}

void ParameterFormDialog::setScriptOptions(const QString &paramName, const QStringList &lines,
                                           const QString &errorMessage)
{
    const auto pit = std::find_if(m_params.constBegin(), m_params.constEnd(),
        [&paramName](const core::Parameter &p) { return p.name == paramName; });
    QWidget *slot = m_scriptSlotByParamName.value(paramName);
    QLabel *status = m_scriptStatusByParamName.value(paramName);
    if (pit == m_params.constEnd() || !slot || !status) {
        return;
    }
    const core::Parameter &param = *pit;

    QVector<fields::ChoiceOption> options;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (!line.isEmpty()) {
            options.push_back(fields::parseParamOption(line));
        }
    }
    if (!errorMessage.isEmpty() || options.isEmpty()) {
        showScriptPlaceholder(paramName, errorMessage.isEmpty() ? utils::tr(QStringLiteral("params.script.empty"))
                                                                : utils::tr(QStringLiteral("params.script.failed")));
        status->setText(errorMessage);
        status->setVisible(!errorMessage.isEmpty());
        refreshValidationState();
        return;
    }
    status->setVisible(false);

    m_fieldByParamName.remove(paramName);
    clearSlot(slot);

    const QString initialValue = m_lastValues.contains(param.name) ? m_lastValues.value(param.name) : param.defaultValue;
    if (param.multiSelect) {
        const fields::ChoiceListField multi = fields::makeCheckList(
            slot, options, initialValue.split(QLatin1Char(','), Qt::SkipEmptyParts), /*withFilter=*/true);
        m_multiSelectFilterByParamName[param.name] = multi.filter;
        slot->layout()->addWidget(multi.container);
        m_fieldByParamName[param.name] = multi.list;
        connect(multi.list, &QListWidget::itemChanged, this, &ParameterFormDialog::refreshValidationState);
    } else {
        const bool hasInitial = std::any_of(options.cbegin(), options.cend(),
            [&initialValue](const fields::ChoiceOption &o) { return o.value == initialValue; });
        const QString placeholder = param.required && !param.optional && !hasInitial
            ? utils::tr(QStringLiteral("params.error.choose")) : QString();
        auto *combo = fields::makeSelectCombo(slot, options, m_usageHistory.value(param.name), initialValue, placeholder);
        connect(combo, &QComboBox::currentIndexChanged, this, &ParameterFormDialog::refreshValidationState);
        connect(combo, &QComboBox::editTextChanged, this, &ParameterFormDialog::refreshValidationState);
        slot->layout()->addWidget(combo);
        m_fieldByParamName[param.name] = combo;
    }
    refreshValidationState();
}

bool ParameterFormDialog::isParamFilled(const core::Parameter &p, const QMap<QString, QString> &vals) const
{
    const QString v = vals.value(p.name);
    if (p.type == core::ParameterType::Grid) {
        return vals.value(QStringLiteral("%1__count").arg(p.name)).toInt() > 0;
    }
    return !v.trimmed().isEmpty();
}

bool ParameterFormDialog::allRequiredFieldsFilled() const
{
    return validationProblems().isEmpty();
}

QVector<ParameterFormDialog::Problem> ParameterFormDialog::validationProblems() const
{
    QVector<Problem> problems;
    const QMap<QString, QString> vals = values();
    for (const core::Parameter &p : m_params) {
        // OPT-IN (achado real: "campos obrigatórios por padrão, não ficou
        // legal... apenas diante seleção de flag") — só bloqueia quando o
        // autor do comando marcou `required` explicitamente no editor, e
        // nunca quando o campo está escondido atrás do "Informar <label>?"
        // (optional sempre vence).
        if (!p.required || p.optional) {
            continue;
        }
        if (isParamFilled(p, vals)) {
            continue;
        }
        Problem problem;
        problem.paramName = p.name;
        problem.step = m_stepByParamName.value(p.name, 0);
        problem.label = p.label.isEmpty() ? p.name : p.label;
        const QWidget *slot = m_scriptSlotByParamName.value(p.name);
        if (p.type == core::ParameterType::Grid) {
            problem.reason = utils::tr(QStringLiteral("params.error.grid_rows"));
        } else if (isScriptSelect(p) && !m_fieldByParamName.contains(p.name) && slot && slot->findChild<QComboBox *>()) {
            problem.reason = slot->findChild<QComboBox *>()->currentText();
        } else if (p.type == core::ParameterType::Select) {
            const bool several = p.multiSelect || !p.collectionId.isEmpty();
            problem.reason = utils::tr(several ? QStringLiteral("params.error.pick_some") : QStringLiteral("params.error.choose"));
        } else if (p.type == core::ParameterType::Date) {
            problem.reason = utils::tr(QStringLiteral("params.error.date"));
        } else if (p.type == core::ParameterType::File) {
            problem.reason = utils::tr(QStringLiteral("params.error.path"));
        } else {
            problem.reason = utils::tr(QStringLiteral("params.error.required"));
        }
        problems.append(problem);
    }
    return problems;
}

void ParameterFormDialog::focusParam(const QString &paramName)
{
    QWidget *target = m_fieldByParamName.value(paramName);
    if (!target) {
        target = m_scriptSlotByParamName.value(paramName);
    }
    if (!target) {
        return;
    }
    goToStep(m_stepByParamName.value(paramName, 0));
    // Um campo dentro de um grupo recolhido só aparece com o grupo aberto.
    for (QWidget *parent = target->parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto *card = qobject_cast<CollapsibleSectionCard *>(parent)) {
            card->setExpanded(true, false);
        }
    }
    if (QScrollArea *page = m_pages.value(m_stepByParamName.value(paramName, 0))) {
        page->ensureWidgetVisible(target, 0, 40);
    }
    target->setFocus(Qt::OtherFocusReason);
}

void ParameterFormDialog::refreshValidationState()
{
    const QVector<Problem> problems = validationProblems();
    const bool onLastStep = m_currentStep >= int(m_stepNames.size()) - 1;
    // Fora da última página "Próximo" só espera os obrigatórios da página atual; na última o OK espera tudo.
    QVector<Problem> relevant;
    for (const Problem &problem : problems) {
        if (onLastStep || problem.step == m_currentStep) {
            relevant.append(problem);
        }
    }
    if (m_okButton) {
        m_okButton->setEnabled(relevant.isEmpty());
        m_okButton->setText(onLastStep ? m_okText : utils::tr(QStringLiteral("params.step.next")));
    }
    if (m_backButton) {
        m_backButton->setEnabled(m_currentStep > 0);
    }
    for (int i = 0; i < m_stepChips.size(); ++i) {
        const bool reachable = std::none_of(problems.cbegin(), problems.cend(),
            [i](const Problem &problem) { return problem.step < i; });
        QPushButton *chip = m_stepChips.at(i);
        chip->setEnabled(reachable || i <= m_currentStep);
        const QString current = i == m_currentStep ? QStringLiteral("true") : QStringLiteral("false");
        if (chip->property("current").toString() != current) {
            chip->setProperty("current", current);
            chip->style()->unpolish(chip);
            chip->style()->polish(chip);
        }
    }
    if (!m_problemsLabel) {
        return;
    }
    constexpr int kMaxListed = 4;
    QStringList lines;
    for (int i = 0; i < relevant.size() && i < kMaxListed; ++i) {
        lines << QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none\"><b>%3</b></a> — %4")
                     .arg(relevant.at(i).paramName.toHtmlEscaped(), utils::tokens::errorFg(),
                          relevant.at(i).label.toHtmlEscaped(), relevant.at(i).reason.toHtmlEscaped());
    }
    if (relevant.size() > kMaxListed) {
        lines << utils::tr(QStringLiteral("params.error.more")).arg(relevant.size() - kMaxListed);
    }
    m_problemsLabel->setText(lines.join(QStringLiteral("<br>")));
    m_problemsLabel->setVisible(!relevant.isEmpty());
}

void ParameterFormDialog::goToStep(int index)
{
    if (!m_stepStack || index < 0 || index >= m_stepStack->count() || index == m_currentStep) {
        return;
    }
    m_currentStep = index;
    m_stepStack->setCurrentIndex(index);
    if (QScrollArea *page = m_pages.value(index)) {
        page->verticalScrollBar()->setValue(0);
    }
    refreshValidationState();
}

QMap<QString, QString> ParameterFormDialog::values() const
{
    QMap<QString, QString> result;

    for (const core::Parameter &param : m_params) {
        QWidget *field = m_fieldByParamName.value(param.name);
        if (!field) {
            if (isScriptSelect(param)) {
                result[param.name] = QString(); // lista ainda carregando, ou o script falhou/não devolveu nada
            }
            continue;
        }

        // Opcional e o usuário deixou "Informar <label>?" DESMARCADA:
        // "não informado" tem que valer de verdade, mesmo que o campo
        // escondido ainda tenha um valor antigo/default parado nele (não é
        // limpo ao esconder — mais simples, mas sem isto o valor "antigo"
        // seria enviado como se tivesse sido informado agora).
        if (param.optional) {
            QCheckBox *checkbox = m_optionalCheckboxByParamName.value(param.name);
            if (checkbox && !checkbox->isChecked()) {
                result[param.name] = QString();
                continue;
            }
        }

        switch (param.type) {
        case core::ParameterType::Text:
        case core::ParameterType::File: {
            auto *lineEdit = qobject_cast<QLineEdit *>(field);
            result[param.name] = lineEdit ? lineEdit->text() : QString();
            break;
        }
        case core::ParameterType::Date: {
            // NUNCA lê field->text() direto: pra um range ele mostra
            // "início — fim" (só pra leitura visual) — o valor de verdade
            // fica em "kaiDateStartFormatted" (ver o clique do botão de
            // escolher acima). Sem esta propriedade (usuário nunca abriu a
            // janelinha nesta sessão), cai pro texto do campo — que, se
            // veio de uma execução anterior, JÁ é o valor limpo (values()
            // nunca persiste a string combinada de range, só o início).
            auto *lineEdit = qobject_cast<QLineEdit *>(field);
            if (!lineEdit) {
                result[param.name] = QString();
                break;
            }
            const QVariant startFormatted = lineEdit->property(fields::kDatePropStartFormatted);
            result[param.name] = startFormatted.isValid() ? startFormatted.toString() : lineEdit->text();
            if (param.dateRange) {
                const QVariant endFormatted = lineEdit->property(fields::kDatePropEndFormatted);
                if (endFormatted.isValid()) {
                    result[QStringLiteral("%1.end").arg(param.name)] = endFormatted.toString();
                }
            }
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
            // Multi-select de opções fixas: junta os values marcados —
            // e, de quebra (pedido do usuário: "faça a injeção dos
            // rótulos do select"), também o CSV dos RÓTULOS em
            // {{nome__labels}} (útil quando as opções são "Rótulo:valor"
            // e o comando quer mostrar/logar o nome amigável, não o id).
            if (param.multiSelect) {
                if (auto *list = qobject_cast<QListWidget *>(field)) {
                    QStringList chosen;
                    QStringList chosenLabels;
                    for (int i = 0; i < list->count(); ++i) {
                        QListWidgetItem *item = list->item(i);
                        if (item->checkState() == Qt::Checked) {
                            chosen << item->data(Qt::UserRole).toString();
                            chosenLabels << item->text();
                        }
                    }
                    result[param.name] = chosen.join(QLatin1Char(','));
                    result[QStringLiteral("%1__labels").arg(param.name)] = chosenLabels.join(QLatin1Char(','));
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
            // Em qualquer um dos três casos, {{nome__label}} guarda o
            // RÓTULO exibido (mesmo pedido do multi-select acima).
            const QString text = comboBox->currentText();
            int idx = comboBox->findText(text);
            if (idx >= 0) {
                result[param.name] = comboBox->itemData(idx).toString();
                result[QStringLiteral("%1__label").arg(param.name)] = comboBox->itemText(idx);
            } else {
                idx = comboBox->findData(text);
                if (idx >= 0) {
                    result[param.name] = text;
                    result[QStringLiteral("%1__label").arg(param.name)] = comboBox->itemText(idx);
                } else {
                    const QString data = comboBox->currentData().toString();
                    result[param.name] = data.isEmpty() ? text : data;
                    // Nenhum item bateu (texto digitado livre): rótulo == o
                    // próprio texto, não tem outro rótulo "de verdade".
                    result[QStringLiteral("%1__label").arg(param.name)] = text;
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
        case core::ParameterType::Grid: {
            const auto *grid = qobject_cast<GridParamField *>(field);
            const QMap<QString, QString> injected = core::gridValues(param, grid ? grid->rows() : QJsonArray());
            for (auto it = injected.constBegin(); it != injected.constEnd(); ++it) {
                result[it.key()] = it.value();
            }
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

    // Estado das checkboxes "Informar <label>?" dos parâmetros opcionais —
    // persiste junto (mesma chave sintética lida em setupUi) pra lembrar
    // sim/não da próxima vez (pedido do usuário).
    for (auto it = m_optionalCheckboxByParamName.constBegin();
         it != m_optionalCheckboxByParamName.constEnd(); ++it) {
        result[optionalEnabledKey(it.key())] = it.value()->isChecked()
            ? QStringLiteral("1") : QStringLiteral("0");
    }

    return result;
}

QMap<QString, QStringList> ParameterFormDialog::updatedUsageHistory() const
{
    // Parte do histórico existente e promove, para cada parâmetro, o(s)
    // valor(es) recém-escolhido(s) ao topo (mais recente), sem duplicatas.
    // Limita o tamanho para não crescer sem controle. Aplica a todos os
    // tipos, mas é especialmente útil para Select (ordena as opções/
    // entradas de coleção na próxima vez).
    //
    // Itera m_params (não mais o mapa de values() direto): precisamos de
    // param.collectionId/param.multiSelect abaixo, e de quebra isso já
    // exclui naturalmente as chaves SINTÉTICAS que values() também
    // devolve (ex: "nome__label", "nome.end", a flag de "Informar X?") —
    // nenhuma delas é útil como histórico de reabertura, só o valor do
    // parâmetro em si.
    constexpr int kMaxHistory = 50;
    QMap<QString, QStringList> updated = m_usageHistory;
    const QMap<QString, QString> chosen = values();
    for (const core::Parameter &param : m_params) {
        const QString raw = chosen.value(param.name);
        if (raw.isEmpty() || param.type == core::ParameterType::Grid) {
            continue;
        }
        // BUG RELATADO ("collections, ao selecionar multi valores não
        // salva como parâmetro recente"): coleção (single OU multi) e
        // select multi de opções fixas guardam vários ids/valores JUNTOS
        // numa CSV só (ver values()) — mas CollectionSelectorDialog (e o
        // combo de opções fixas) procuram cada id/valor ISOLADO dentro de
        // m_history via indexOf(). Uma CSV inteira ("id1,id2,id3") nunca
        // bate com um indexOf("id1") avulso — o histórico até era
        // gravado, só nunca surtia efeito nenhum ao reabrir. Cada valor
        // precisa virar SUA PRÓPRIA entrada na lista.
        const bool isMultiValued = !param.collectionId.isEmpty() || param.multiSelect;
        const QStringList incoming = isMultiValued
            ? raw.split(QLatin1Char(','), Qt::SkipEmptyParts)
            : QStringList{raw};

        QStringList list = updated.value(param.name);
        // Prepend em ordem REVERSA: processar o ÚLTIMO escolhido primeiro
        // faz o PRIMEIRO escolhido acabar na posição 0 (mais "recente")
        // depois de todos os prepends — preserva a ordem de escolha.
        for (auto it = incoming.crbegin(); it != incoming.crend(); ++it) {
            list.removeAll(*it);
            list.prepend(*it);
        }
        while (list.size() > kMaxHistory) {
            list.removeLast();
        }
        updated[param.name] = list;
    }
    return updated;
}

} // namespace kai::ui
