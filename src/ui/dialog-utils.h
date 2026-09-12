#pragma once

#include <QDialogButtonBox>
#include <QPushButton>
#include <QToolButton>
#include <QAbstractButton>
#include <QIcon>
#include <QColor>
#include <QSize>
#include <QWidget>
#include <QRect>
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QSortFilterProxyModel>
#include <QShortcut>
#include <QKeySequence>
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QString>
#include <QMessageBox>

#include "utils/translation-manager.h"
#include "utils/design-tokens.h"
#include "utils/action-shortcuts.h"
#include "core/config-manager.h"
#include "ui/lucide-icons.h"

namespace kai::ui {

// Sub-namespace deliberado (em vez de despejar direto em kai::ui): o
// ParameterFormDialog (arquivo de OUTRA tarefa em andamento na mesma
// árvore, fora do escopo desta) já tem sua PRÓPRIA cópia local (anônima)
// de wrapWithLabel com a MESMA assinatura — se estas fossem declaradas
// direto em kai::ui, o include deste header ali (já presente,
// transitivamente, via stripDialogButtonIcons etc.) tornaria toda chamada
// não-qualificada "wrapWithLabel(...)" naquele arquivo AMBÍGUA (erro de
// build real, encontrado ao integrar). Isolar num sub-namespace evita a
// colisão sem precisar tocar naquele arquivo.
namespace layout_helpers {

// Campo com um rótulo pequeno em caixa alta ACIMA dele, em vez de um
// QFormLayout tradicional (rótulo à esquerda empurrando o campo pra
// direita, "buraco no meio" — mockup enviado pelo usuário). Devolve um
// container pronto pra entrar num QGridLayout/QVBoxLayout. Extraído de
// CommandEditorDialog (era anônimo/local àquele .cpp) para ser
// reaproveitado IDENTICAMENTE por outros diálogos re-skinados no mesmo
// padrão visual (FolderEditorDialog e, futuramente, CollectionEditorDialog
// — pedido do usuário: "mesmo tema", ele valora consistência entre
// diálogos, não cópias que podem divergir com o tempo).
inline QWidget *wrapWithLabel(QWidget *parent, const QString &label, QWidget *field)
{
    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("fieldLabelWrap"));
    // Container TRANSPARENTE. Sem isto, a regra GLOBAL "QWidget {
    // background-color: bg }" (theme-manager) pinta o fundo ESCURO (bg) em
    // TODA a área do wrap — label + campo —, e como o campo (QLineEdit/
    // QComboBox) tem o MESMO bg, os dois se fundem numa "caixa escura maior
    // que o campo" (relatado: "o fundo escuro está numa caixa da qual o
    // campo tá dentro"). Transparente, só o CAMPO pinta o bg (do tamanho do
    // campo) e o resto mostra o surface2 do card por trás — o visual da aba
    // Aparência (certa.png). Qualificado por objectName pra não vazar a
    // transparência a outros QWidget.
    container->setStyleSheet(QStringLiteral(
        "QWidget#fieldLabelWrap { background: transparent; }"));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(1));
    auto *labelWidget = new QLabel(label.toUpper(), container);
    labelWidget->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px;"
        " background: transparent;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    layout->addWidget(labelWidget);
    field->setParent(container);
    layout->addWidget(field);
    return container;
}

// Card "superfície" plano (não colapsável — diferente de
// CollapsibleSectionCard): só um painel com fundo levemente destacado e
// cantos arredondados, pra dar hierarquia visual às seções fixas de um
// diálogo (ex: "Identificação"). Mesma razão de extração de
// wrapWithLabel acima.
inline QWidget *makeSurfaceCard(QWidget *parent)
{
    auto *card = new QWidget(parent);
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setObjectName(QStringLiteral("kaiSurfaceCard"));
    // Bug real reportado ("os campos internos... estão usando a cor de
    // fundo errada, exemplo: aqui apenas o campo ícone tem a cor certa"):
    // um setStyleSheet() SEM seletor ("background-color: ...;" cru) não
    // fica restrito a este widget — no Qt, a folha de estilo de um
    // ancestral tem PRIORIDADE sobre a folha de estilo global da aplicação
    // pra qualquer descendente que a regra alcance, e uma regra sem
    // seletor casa com TODO mundo (equivale a "*"). Isso pintava o fundo
    // do card por cima de QLineEdit/QComboBox/QSpinBox filhos, que
    // deveriam ficar com o bg() mais escuro do tema global, não o
    // surface() do card. Qualificar com #kaiSurfaceCard restringe a regra
    // só a ESTE widget — os filhos voltam a herdar do QSS global.
    // Caixa de agrupamento no mesmo look da aba Aparência (aprovado pelo
    // usuário): fundo surface2 + BORDA + raio que SEGUE a preferência de
    // canto (radiusLg). Os campos internos (via wrapWithLabel) são
    // transparentes, então só o card pinta o fundo/borda e o recorte
    // arredondado aparece. Qualificado por #kaiSurfaceCard para não
    // cascatear aos QLineEdit/QComboBox filhos.
    card->setStyleSheet(QStringLiteral(
        "QWidget#kaiSurfaceCard { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(utils::tokens::surface2()).arg(utils::tokens::borderColor())
        .arg(utils::tokens::radiusLg()));
    return card;
}

// Banner de HINT/texto explicativo no padrão aprovado pelo usuário (o de
// "Efeitos visuais" em Settings > Aparência): caixa com fundo do accent
// bem translúcido, borda e texto na cor do accent, cantos que seguem a
// preferência (radiusMd), texto com quebra de linha. Extraído para ser
// reutilizado IDENTICAMENTE nos demais textos descritivos do app
// (inicialização, perfis de execução, atalho global) — em vez de cópias
// que divergem. Qualificado por #kaiHintBanner (regra restrita ao label).
inline QLabel *makeHintBanner(QWidget *parent, const QString &text)
{
    auto *hint = new QLabel(text, parent);
    hint->setObjectName(QStringLiteral("kaiHintBanner"));
    hint->setWordWrap(true);
    hint->setContentsMargins(10, 8, 10, 8);
    const QColor accentColor(utils::tokens::accent());
    const QString rgbaBg = QStringLiteral("rgba(%1, %2, %3, 40)")
        .arg(accentColor.red()).arg(accentColor.green()).arg(accentColor.blue());
    hint->setStyleSheet(QStringLiteral(
        "QLabel#kaiHintBanner { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; color: %2; }")
        .arg(rgbaBg, utils::tokens::accent()).arg(utils::tokens::radiusMd()));
    return hint;
}

} // namespace layout_helpers

// Botões do cabeçalho dos editores de JSON cru ("modo avançado"): botão
// de voltar ao modo simples + Formatar + Minificar. Construídos aqui, num
// único ponto, para que os TRÊS editores de JSON do app (Comando/Pasta/
// Coleção) fiquem visualmente IDÊNTICOS por construção — em vez de cada
// diálogo estilizar os seus próprios botões e arriscar divergir (a
// reclamação original do usuário: "o botão de voltar para o modo simples
// deve ter o mesmo tema do outro botão").
//
// Segunda rodada de feedback ("o toggle entre modo simples e avançado
// ficou OK, mas ainda está diferente, pois no modo normal é um botão de
// texto, no outro é um botão cru além disso o format e minify devem ser
// icones"): os três QPushButton "crus" (com borda/fundo cheios) deram
// lugar a QToolButton, replicando LITERALMENTE os dois padrões visuais já
// estabelecidos alhures no app, em vez de inventar um terceiro:
//   - backButton: MESMO estilo "link" (ícone + texto em accent, sem caixa,
//     sublinha no hover) do botão que leva ATÉ o modo avançado
//     ("Advanced mode (JSON)", CommandEditorDialog::setupUi) — antes eram
//     visualmente diferentes (um QPushButton com borda, um QToolButton sem
//     borda) mesmo fazendo o MESMO tipo de coisa (alternar de modo).
//   - formatButton/minifyButton: ícone puro (sem texto), autoRaise, mesmo
//     padrão da engrenagem ao lado de "COMMAND SCRIPT"/do ícone de
//     formatar ao lado de "BODY" em CommandEditorDialog — o rótulo textual
//     vira tooltip.
//
// `rawLabelText`: rótulo descritivo à esquerda (ex: "Edição em JSON cru").
// `backButtonText`: texto do botão de voltar ao modo simples (cada
// diálogo decide o que "simples" significa para ele — fechar/reject, ou
// converter e sinalizar troca de modo — então só os WIDGETS são
// compartilhados aqui; as conexões de clique ficam por conta do chamador).
struct JsonEditorHeaderButtons {
    QToolButton *backButton = nullptr;
    QToolButton *formatButton = nullptr;
    QToolButton *minifyButton = nullptr;
};

// Cria um QToolButton ícone-only no padrão "ação discreta de cabeçalho"
// (autoRaise, fundo transparente que ganha hoverBg no hover) — mesmo QSS
// usado pela engrenagem de "Advanced Settings" e pelo formatador de BODY em
// CommandEditorDialog. Extraído aqui porque format/minify do JSON header
// (abaixo) E os novos botões de Formatar/Minificar do parâmetro Json em
// ParameterFormDialog (pedido do usuário, mesmo item) precisam do MESMO
// visual.
inline QToolButton *makeHeaderIconButton(QWidget *parent, const QString &iconName,
                                          const QString &tooltip)
{
    auto *button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(LucideIcons::icon(iconName, QColor(utils::tokens::mutedFg()), 16));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(tooltip);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent; }"
        "QToolButton:hover { background: %2; }")
        .arg(utils::tokens::radiusSm()).arg(utils::tokens::hoverBg()));
    return button;
}

inline JsonEditorHeaderButtons buildJsonEditorHeaderRow(QWidget *parent, QHBoxLayout *headerLayout,
                                                          const QString &rawLabelText,
                                                          const QString &backButtonText)
{
    JsonEditorHeaderButtons buttons;
    headerLayout->addWidget(new QLabel(rawLabelText, parent));
    headerLayout->addStretch();

    buttons.backButton = new QToolButton(parent);
    buttons.backButton->setText(backButtonText);
    buttons.backButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    buttons.backButton->setIcon(LucideIcons::icon(QStringLiteral("code-xml"),
        QColor(utils::tokens::accent()), 16));
    buttons.backButton->setCursor(Qt::PointingHandCursor);
    buttons.backButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; color: %1; font-weight: 600; }"
        "QToolButton:hover { text-decoration: underline; }")
        .arg(utils::tokens::accent()));
    headerLayout->addWidget(buttons.backButton);

    // "braces" ({}) para Formatar (pretty-print) e "shrink" para Minificar
    // (compactar) — os dois ícones do pool Lucide embarcado que melhor
    // comunicam cada ação sem depender de texto.
    buttons.formatButton = makeHeaderIconButton(parent, QStringLiteral("braces"),
        utils::tr(QStringLiteral("json.dialog.format_title")));
    headerLayout->addWidget(buttons.formatButton);

    buttons.minifyButton = makeHeaderIconButton(parent, QStringLiteral("shrink"),
        utils::tr(QStringLiteral("json.dialog.minify_title")));
    headerLayout->addWidget(buttons.minifyButton);

    return buttons;
}

// Remove os ícones nativos que alguns estilos do Qt injetam nos botões
// padrão do QDialogButtonBox (um "check" verde no OK e um "X" no Cancel).
// Esses ícones destoavam do restante do app (bug reportado: "ícones feios
// e fora de padrão"). Também TRADUZ os rótulos padrão (Ok/Cancel/Close/
// Yes/No...) para o idioma do app via language pack (feedback do usuário:
// apareciam "Ok"/"Cancel" em inglês). Chamar logo após criar o
// QDialogButtonBox de cada diálogo.
inline void stripDialogButtonIcons(QDialogButtonBox *box)
{
    if (!box) {
        return;
    }
    // Traduz por botão-padrão conhecido (as chaves ficam no i18n; sem
    // tradução, cai no texto atual). Só ajustamos o texto se a chave
    // existir no pack.
    struct Std { QDialogButtonBox::StandardButton id; const char *key; };
    static const Std kStd[] = {
        {QDialogButtonBox::Ok, "dialog.ok"},
        {QDialogButtonBox::Cancel, "dialog.cancel"},
        {QDialogButtonBox::Close, "dialog.close"},
        {QDialogButtonBox::Yes, "dialog.yes"},
        {QDialogButtonBox::No, "dialog.no"},
        {QDialogButtonBox::Save, "dialog.save"},
        {QDialogButtonBox::Apply, "dialog.apply"},
    };
    for (const Std &s : kStd) {
        if (QPushButton *b = box->button(s.id)) {
            const QString t = utils::tr(QString::fromLatin1(s.key));
            // utils::tr devolve a própria chave se não houver tradução;
            // nesse caso mantém o texto nativo do Qt.
            if (t != QString::fromLatin1(s.key)) {
                b->setText(t);
            }
        }
    }
    const auto buttons = box->buttons();
    for (QAbstractButton *button : buttons) {
        button->setIcon(QIcon());
    }
    // Marca EXPLICITAMENTE a ação primária (Ok/Save/Yes, a que existir)
    // como botão padrão do diálogo — bug real relatado com print
    // ("Editar parâmetro": botão OK aparecia sem texto/fundo nenhum, um
    // retângulo vazio): sem isto, QDialogButtonBox só marca :default
    // automaticamente em certas condições (ex: quando NENHUM outro widget
    // pediu foco primeiro), e a regra de QSS "QDialogButtonBox
    // QPushButton:default" — que é quem dá o fundo de accent + texto
    // contrastante ao botão primário — nunca chegava a se aplicar,
    // deixando o botão só com border:1px transparente e SEM a cor de
    // texto que o :hover/:default garantem (o fundo escuro "engolia" o
    // texto). setDefault(true) força a marcação sempre, garantindo que a
    // regra de estilo se aplique de verdade.
    for (auto role : {QDialogButtonBox::Ok, QDialogButtonBox::Save, QDialogButtonBox::Yes}) {
        if (QPushButton *primary = box->button(role)) {
            primary->setDefault(true);
            primary->setAutoDefault(true);
            break;
        }
    }
}

// Substituto de QMessageBox::question(...) com os botões Sim/Não SEMPRE no
// idioma do app. Os StandardButtons do Qt (Yes/No) são rotulados por
// traduções internas do Qt (qtbase_*.qm) que o Kai NÃO carrega — o language
// pack próprio (assets/i18n) não alcança QMessageBox, então os botões
// ficavam presos em inglês mesmo com a UI inteira em português (bug
// relatado: "alguns botões com yes e confirm fixo"). Aqui os rótulos vêm de
// dialog.yes/dialog.no (as mesmas chaves que stripDialogButtonIcons já usa
// para QDialogButtonBox), então corrigir o pack corrige os dois casos.
// Devolve true só quando o usuário confirma (clica em "Sim").
//
// addButton(QMessageBox::Yes/No) (STANDARD button) — só o TEXTO é trocado
// depois. Código (testes inclusive) que localiza os botões via
// box.button(QMessageBox::Yes) espera o standard button registrado; usar
// addButton(texto, role) NÃO registra isso, e box.button(QMessageBox::Yes)
// volta nullptr (SEGFAULT observado: um ->click() num ponteiro nulo).
inline bool confirmYesNo(QWidget *parent, const QString &title, const QString &text,
                          bool defaultToYes = false)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, parent);
    QPushButton *yesButton = box.addButton(QMessageBox::Yes);
    QPushButton *noButton = box.addButton(QMessageBox::No);
    yesButton->setText(utils::tr(QStringLiteral("dialog.yes")));
    noButton->setText(utils::tr(QStringLiteral("dialog.no")));
    box.setDefaultButton(defaultToYes ? yesButton : noButton);
    box.exec();
    return box.clickedButton() == yesButton;
}

// Centraliza um diálogo (ou qualquer widget top-level) sobre a janela do
// seu pai. Corrige o comportamento em que diálogos apareciam no canto
// (0,0) da tela — feedback do usuário. Chamar após definir o tamanho
// (resize) e antes/depois do primeiro show; é seguro chamar mais de uma
// vez. Se não houver pai, centraliza na tela do próprio widget.
inline void centerOnParent(QWidget *dialog)
{
    if (!dialog) {
        return;
    }
    QWidget *ref = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
    if (ref) {
        const QRect refGeom = ref->frameGeometry();
        const QSize size = dialog->size();
        const int x = refGeom.center().x() - size.width() / 2;
        const int y = refGeom.center().y() - size.height() / 2;
        dialog->move(qMax(0, x), qMax(0, y));
    }
}

// Torna um QComboBox PESQUISÁVEL: campo editável com filtro por substring
// (case-insensitive) e completamento em popup. Pedido de UX: adicionar busca
// ao seletor de pastas usado em Coleções, Pastas e Comandos, que podem ter
// muitas pastas. Chamar DEPOIS de popular os itens do combo.
//
// NoInsert impede que digitar um texto que não é uma pasta crie um item novo;
// o chamador deve ler a pasta escolhida por currentData()/itemData(), nunca
// pelo texto digitado.
inline void makeSearchableCombo(QComboBox *combo)
{
    if (!combo) {
        return;
    }
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->completer(); // garante criação do modelo interno
    auto *completer = new QCompleter(combo->model(), combo);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    // ESTILIZA O POPUP para seguir o tema (bug reportado: "caixinha que foge do
    // tema"). O popup do completer é um QAbstractItemView SEPARADO que não herda
    // o QSS de lista da aplicação; aplicamos as cores dos design tokens direto.
    if (QAbstractItemView *popup = completer->popup()) {
        using namespace kai::utils;
        popup->setStyleSheet(QStringLiteral(
            "QAbstractItemView {"
            " background-color: %1;"
            " color: %2;"
            " border: 1px solid %3;"
            " border-radius: %4px;"
            " outline: none;"
            " padding: 2px; }"
            "QAbstractItemView::item { padding: 4px 8px; border-radius: %5px; }"
            "QAbstractItemView::item:selected { background-color: %6; color: %2; }")
            .arg(tokens::surface2(), tokens::fg(), tokens::borderColor())
            .arg(tokens::radiusMd()).arg(tokens::radiusSm())
            .arg(tokens::selBg()));
    }
    combo->setCompleter(completer);
}

// Instala no `dialog` um atalho remapeável (action.toggle_edit_mode — ver
// utils::actionShortcutSpecs/Shortcuts Manager v2) que ALTERNA o modo de
// edição simples <-> avançado (JSON), acionando o botão `advancedButton`
// (o mesmo que o usuário clicaria). Pedido de UX: um atalho único para
// trocar de modo nos editores que têm modo avançado (coleções, pastas,
// comandos). Chamar após criar o botão de modo avançado.
inline void installEditModeToggleShortcut(QDialog *dialog, QAbstractButton *advancedButton)
{
    if (!dialog || !advancedButton) {
        return;
    }
    const QString seq = utils::firstShortcutFor(core::ConfigManager().loadSettings(),
                                                QStringLiteral("action.toggle_edit_mode"));
    if (seq.trimmed().isEmpty()) {
        return;
    }
    auto *sc = new QShortcut(QKeySequence(seq), dialog);
    sc->setContext(Qt::WindowShortcut);
    QObject::connect(sc, &QShortcut::activated, advancedButton, [advancedButton]() {
        if (advancedButton->isEnabled()) {
            advancedButton->click();
        }
    });
}

} // namespace kai::ui