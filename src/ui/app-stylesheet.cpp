#include "ui/app-stylesheet.h"

#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QWidget>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dwmapi.h>
#endif

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

// Mistura duas cores (t=0 -> a, t=1 -> b). Usado para derivar listras/bordas.
QColor shiftToward(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF()   + (b.redF()   - a.redF())   * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF()  + (b.blueF()  - a.blueF())  * t);
}

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

// Gera (e cacheia em disco) o SVG de um TOGGLE SWITCH (pílula + bolinha),
// usado pelos QCheckBox marcados com a propriedade kaiRole="switch"
// (repaginação do Settings — mockup usa switches, não checkboxes de caixa,
// para todas as flags booleanas). Mesma lógica do check acima: o QSS não
// recolore SVG, então a cor é assada no arquivo; um caminho por
// (estado, cor de trilho, cor da bolinha).
QString themedSwitchSvgPath(bool checked, const QColor &track, const QColor &knob, const QColor &border)
{
    const QString dir = QDir::tempPath() + QStringLiteral("/kai-theme");
    QDir().mkpath(dir);
    const QString state = checked ? QStringLiteral("on") : QStringLiteral("off");
    const QString path = dir + QStringLiteral("/switch-%1-%2-%3.svg")
        .arg(state, track.name(QColor::HexRgb).mid(1), knob.name(QColor::HexRgb).mid(1));
    const qreal cx = checked ? 26.0 : 10.0;
    // stroke-width 1.5 -> 2: contorno do trilho ficava fino demais pra
    // notar em temas com pouco contraste entre borda e superfície
    // (relatado: switch desligado "sumia", só a bolinha aparecia).
    const QString svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"36\" height=\"20\" viewBox=\"0 0 36 20\">"
        "<rect x=\"1\" y=\"1\" width=\"34\" height=\"18\" rx=\"9\" "
        "fill=\"%1\" stroke=\"%2\" stroke-width=\"2\"/>"
        "<circle cx=\"%3\" cy=\"10\" r=\"7\" fill=\"%4\"/></svg>")
        .arg(track.name(QColor::HexRgb), border.name(QColor::HexRgb)).arg(cx).arg(knob.name(QColor::HexRgb));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(svg.toUtf8());
        f.close();
    }
    return path;
}

} // namespace

QString buildModernStylesheet()
{
    const QString bg = tk::bg();
    const QString fg = tk::fg();
    const QString muted = tk::mutedFg();
    const QString border = tk::borderColor();
    const QString accent = tk::accent();
    const QString hover = tk::hoverBg();
    const QString surface = tk::surface();
    const QString surface2 = tk::surface2();
    const QString sel = tk::selBg();

    const int rSm = tk::radiusSm();
    const int rMd = tk::radiusMd();
    const int rLg = tk::radiusLg();
    const int ctrlH = tk::controlHeight();
    const int padY = tk::space(2);
    const int padX = tk::space(3);
    const int fsBody = tk::fontSizePt();
    const int fsSmall = tk::fontSizeSmallPt();
    const int fsTitle = tk::fontSizeTitlePt();
    const int wTitle = tk::titleWeight();

    QString qss;

    // --- Tipografia: escala explícita. Antes cada widget escolhia
    //     bold/600/11pt/13px/15px por conta própria, sem hierarquia. ---
    qss += QStringLiteral(
        "QWidget { font-family: '%1'; font-size: %2pt; }\n"
        "QLabel[kaiRole=\"title\"] { font-size: %3pt; font-weight: %4; color: %5; }\n"
        "QLabel[kaiRole=\"subtitle\"] { font-size: %2pt; font-weight: 500; color: %5; }\n"
        "QLabel[kaiRole=\"caption\"] { font-size: %6pt; color: %7; }\n")
        .arg(tk::fontFamily()).arg(fsBody).arg(fsTitle).arg(wTitle).arg(fg).arg(fsSmall).arg(muted);

    // --- Superfícies em camadas (elevação por cor, estilo Material 3) ---
    qss += QStringLiteral(
        "QDialog { background-color: %1; }\n"
        "QGroupBox { background-color: %2; border: 1px solid %3; border-radius: %4px;"
        " margin-top: %5px; padding: %6px; }\n"
        "QGroupBox::title { subcontrol-origin: margin; left: %6px; padding: 0 %7px;"
        " color: %8; font-weight: %9; }\n")
        .arg(surface).arg(surface2).arg(border).arg(rLg)
        .arg(tk::space(2)).arg(tk::space(3)).arg(tk::space(1)).arg(muted).arg(wTitle);

    // --- Inputs: altura consistente, foco com anel de accent ---
    qss += QStringLiteral(
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit {"
        " background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
        " padding: %5px %6px; min-height: %7px; }\n"
        // SPINBOX: o padding da regra acima também se aplicava à ÁREA DOS BOTÕES,
        // empurrando as setas para dentro e produzindo aquela faixa larga vazia
        // na frente dos ícones de incremento (relatado). Aqui zeramos o padding
        // à direita e damos aos sub-controles geometria própria.
        "QSpinBox, QDoubleSpinBox { padding-right: 0px; }\n"
        "QSpinBox::up-button, QDoubleSpinBox::up-button {"
        " subcontrol-origin: border; subcontrol-position: top right;"
        " width: %20px; height: %21px; margin: 1px 1px 0px 0px;"
        " border: none; border-top-right-radius: %4px;"
        " background: transparent; }\n"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        " subcontrol-origin: border; subcontrol-position: bottom right;"
        " width: %20px; height: %21px; margin: 0px 1px 1px 0px;"
        " border: none; border-bottom-right-radius: %4px;"
        " background: transparent; }\n"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover,"
        " QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {"
        " background: %22; }\n"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        " image: url(:/icons/lucide/chevron-up-fg.svg); width: 10px; height: 10px; }\n"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        " image: url(:/icons/lucide/chevron-down-fg.svg); width: 10px; height: 10px; }\n"
        "QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled,"
        " QDoubleSpinBox::up-arrow:disabled, QDoubleSpinBox::down-arrow:disabled {"
        " opacity: 0.3; }\n"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus,"
        " QTextEdit:focus { border: 1px solid %8; }\n"
        "QLineEdit:disabled, QComboBox:disabled { color: %9; }\n")
        .arg(bg).arg(fg).arg(border).arg(rMd)
        .arg(padY).arg(padX).arg(ctrlH).arg(accent).arg(muted)
        // Botões do spinbox: metade da altura do controle cada, largura enxuta.
        .arg(tk::space(4)).arg((ctrlH - 4) / 2).arg(hover);

    // --- Botões: primário (accent) vs neutro, via propriedade kaiRole ---
    qss += QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; padding: %5px %6px; min-height: %7px; font-weight: 500; }\n"
        "QPushButton:hover { background-color: %8; }\n"
        "QPushButton:pressed { background-color: %9; }\n"
        "QPushButton:disabled { color: %10; border-color: %3; }\n"
        "QPushButton[kaiRole=\"primary\"] { background-color: %11; color: %12;"
        " border: 1px solid %11; font-weight: 600; }\n"
        "QPushButton[kaiRole=\"danger\"] { background-color: transparent; color: %13;"
        " border: 1px solid %13; }\n"
        "QPushButton:default { border: 1px solid %11; }\n")
        .arg(surface2).arg(fg).arg(border).arg(rMd).arg(padY).arg(padX).arg(ctrlH)
        .arg(hover).arg(sel).arg(muted).arg(tk::buttonColor())
        .arg(tk::buttonFg())
        .arg(tk::errorFg());

    // --- Botões de ícone: UM tamanho só (antes 24/30/32 coexistiam) ---
    qss += QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: %1px;"
        " padding: %2px; }\n"
        "QToolButton:hover { background-color: %3; }\n"
        "QToolButton:pressed { background-color: %4; }\n"
        "QToolButton:checked { background-color: %4; }\n")
        .arg(rSm).arg(tk::space(1)).arg(hover).arg(sel);

    // --- Listas e árvores ---
    // SELEÇÃO SÓBRIA: o QSS do core pintava a linha inteira com o ACCENT puro
    // (fundo accent + texto na cor do fundo), o que estoura o contraste e briga
    // com as listras (bug reportado: "os temas deixaram bem ruim o contraste").
    // Aqui a seleção é uma superfície discreta com o TEXTO em accent, e é
    // preciso cobrir também :active/:!active — os seletores do core incluem
    // :selected:active, que é mais específico e venceria.
    // LISTRAS SUTIS: alternate-background-color derivado do fundo com um desvio
    // mínimo, em vez de alt_bg (que é um salto grande de luminância). Vem de
    // um token compartilhado (tk::treeStripeBg()) para o DraggableTreeWidget
    // conseguir reaplicar a MESMA cor ao pintar a listra manualmente (ver
    // command-tree-widget.cpp).
    const QString stripe = tk::treeStripeBg();
    // Exibição em árvore refinada (pedido do usuário: "mais respiro"):
    // padding vertical um degrau maior que o padY genérico dos outros
    // widgets. A seleção volta a ser só PREENCHIMENTO sólido, sem borda —
    // numa QTreeWidget de VÁRIAS colunas (a árvore de comandos tem colunas
    // extras pros botões inline de cada linha) uma borda por ::item vira
    // uma CAIXA POR CÉLULA, não um contorno único ao redor da linha
    // inteira (bug relatado com print: "virou vários segmentos" em vez de
    // um destaque único). Nada de regras QSS pra `QTreeView::branch` aqui:
    // aquela área NUNCA é pintada pelo QSS (é código do próprio
    // QTreeView) — por isso as setas customizadas não apareciam antes; as
    // linhas de conexão + o indicador de expandir/colapsar agora são
    // desenhados em C++ por DraggableTreeWidget::drawBranches (ver
    // ui/shared/draggable-tree-widget.h), com estilo configurável em
    // Settings → Aparência → "Linhas da árvore" (Nativa/Nenhuma/Contínua).
    const int treePadY = padY + 1;
    qss += QStringLiteral(
        "QTreeWidget, QTreeView, QListWidget, QListView, QTableWidget, QTableView {"
        " background-color: %1; border: none; outline: none; alternate-background-color: %8;"
        " show-decoration-selected: 1; }\n"
        "QListView::item, QListWidget::item {"
        " padding: %2px %3px; border-radius: %4px; }\n"
        // ÁRVORE SEM RAIO NAS LINHAS (pedido do usuário: "quero que ele
        // fique totalmente colado um no outro"): diferente de listas/
        // tabelas, os itens da árvore de comandos ficam coladas — sem
        // cantinho arredondado entre uma linha e a seguinte, formando um
        // bloco único e contínuo ao selecionar/hover vários itens em
        // sequência.
        "QTreeView::item, QTreeWidget::item {"
        " padding: %2px %3px; border-radius: 0px; }\n"
        "QTreeView::item:hover, QListView::item:hover,"
        " QTreeWidget::item:hover, QListWidget::item:hover { background-color: %5; }\n"
        "QTreeView::item:selected, QListView::item:selected,"
        " QTreeWidget::item:selected, QListWidget::item:selected,"
        " QTreeView::item:selected:active, QListView::item:selected:active,"
        " QTreeWidget::item:selected:active, QListWidget::item:selected:active,"
        " QTreeView::item:selected:!active, QListView::item:selected:!active,"
        " QTreeWidget::item:selected:!active, QListWidget::item:selected:!active {"
        " background-color: %6; color: %7; border: none; }\n"
        "QTableWidget::item:selected, QTableView::item:selected {"
        " background-color: %6; color: %7; }\n")
        .arg(bg).arg(treePadY).arg(padX).arg(rSm).arg(hover).arg(sel).arg(accent).arg(stripe);

    // --- Abas: indicador de accent, sem moldura 3D ---
    qss += QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }\n"
        "QTabBar::tab { background: transparent; color: %2; padding: %3px %4px;"
        " border: none; border-bottom: 2px solid transparent; font-weight: 500; }\n"
        "QTabBar::tab:hover { color: %5; }\n"
        "QTabBar::tab:selected { color: %5; border-bottom: 2px solid %6;"
        " font-weight: %7; }\n")
        .arg(bg).arg(muted).arg(padY).arg(padX).arg(fg).arg(accent).arg(wTitle);

    // --- Scrollbars slim (o padrão do Qt é largo e datado) ---
    qss += QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: %1px; margin: 0; }\n"
        "QScrollBar:horizontal { background: transparent; height: %1px; margin: 0; }\n"
        "QScrollBar::handle { background: %2; border-radius: %3px; min-height: %4px; }\n"
        "QScrollBar::handle:hover { background: %5; }\n"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }\n"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }\n")
        .arg(tk::space(3)).arg(border).arg(tk::space(1)).arg(tk::space(6)).arg(muted);

    // --- Menus com respiro e cantos ---
    qss += QStringLiteral(
        "QMenu { background-color: %1; border: 1px solid %2; border-radius: %3px;"
        " padding: %4px; }\n"
        "QMenu::item { padding: %5px %6px; border-radius: %7px; }\n"
        "QMenu::item:selected { background-color: %8; }\n"
        "QMenu::separator { height: 1px; background: %2; margin: %4px %5px; }\n"
        "QMenuBar { background: transparent; }\n"
        "QMenuBar::item { padding: %5px %6px; border-radius: %7px;"
        " background: transparent; }\n"
        "QMenuBar::item:selected { background-color: %8; }\n")
        .arg(surface2).arg(border).arg(rMd).arg(tk::space(1))
        .arg(padY).arg(padX).arg(rSm).arg(hover);

    // --- Badges/chips de status via propriedade kaiState ---
    qss += QStringLiteral(
        "QLabel[kaiState=\"running\"] { color: %1; font-weight: 600; }\n"
        "QLabel[kaiState=\"success\"] { color: %2; font-weight: 600; }\n"
        "QLabel[kaiState=\"error\"] { color: %3; font-weight: 600; }\n"
        "QLabel[kaiState=\"idle\"] { color: %4; }\n")
        .arg(tk::infoFg()).arg(tk::successFg()).arg(tk::errorFg()).arg(muted);

    // --- Tooltip, checkbox, splitter, header ---
    qss += QStringLiteral(
        "QToolTip { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; padding: %5px %6px; }\n"
        // CHECKBOX/RADIO. Pedido do usuário: SEM preenchimento cheio de accent
        // ao marcar — apenas um TICK interno. A caixa tem um fundo de superfície
        // sutil (não transparente) para nunca sumir sobre a linha selecionada
        // da tabela; ao marcar, ganha borda de accent + o ícone de check na cor
        // do accent (SVG gerado no tema). O radio usa raio total.
        "QCheckBox, QRadioButton { spacing: %11px; background: transparent; }\n"
        "QCheckBox::indicator, QRadioButton::indicator { width: %7px; height: %7px;"
        " border: 1.5px solid %3; border-radius: %8px; background: transparent; }\n"
        // DESMARCADO com fundo PREENCHIDO (%9, a cor de fundo BASE do app —
        // já usada aqui mesmo pro anel interno do radio marcado, então
        // nenhum novo parâmetro no .arg() chain) em vez de transparent —
        // bug real reportado com foto: dentro de um card com fundo
        // "surface"/"surface2" (survey de opções de export, por exemplo), a
        // borda fina sobre fundo transparente ficava quase invisível, a
        // ponto de o checkbox desmarcado sumir de vista por completo. Mesmo
        // raciocínio já usado pro trilho do switch desligado (ver bloco
        // TOGGLE SWITCH abaixo: "ficava perto demais de surface2... some
        // sob o card").
        // MESMO fundo preenchido no radio desmarcado (pedido do usuário:
        // "os radio buttons por todo o sistema não têm contraste se
        // desativados") — a regra acima só cobria QCheckBox::indicator
        // :unchecked; o radio ficava com borda fina sobre fundo
        // TRANSPARENTE, sumindo sobre cards de superfície igual ao bug já
        // corrigido pro checkbox.
        "QCheckBox::indicator:unchecked, QRadioButton::indicator:unchecked {"
        " background: %9; image: none; }\n"
        "QRadioButton::indicator { border-radius: %12px; }\n"
        "QCheckBox::indicator:hover, QRadioButton::indicator:hover {"
        " border: 1.5px solid %10; }\n"
        "QCheckBox::indicator:checked { background: transparent; border: 1.5px solid %10;"
        " image: url(:/icons/lucide/check-on.svg); }\n"
        "QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {"
        " border: 1.5px solid %13; background: transparent; }\n"
        "QCheckBox:disabled, QRadioButton:disabled { color: %13; }\n"
        // Indicador de CHECK de itens de lista/árvore (ex: lista multi-select
        // do formulário de execução): mesma aparência da checkbox do tema —
        // caixa transparente + tick do qrc, em vez do indicador nativo do SO.
        "QListView::indicator, QTreeView::indicator {"
        " width: %7px; height: %7px; border: 1.5px solid %3;"
        " border-radius: %8px; background: transparent; }\n"
        "QListView::indicator:hover, QTreeView::indicator:hover {"
        " border: 1.5px solid %10; }\n"
        "QListView::indicator:checked, QTreeView::indicator:checked {"
        " background: transparent; border: 1.5px solid %10;"
        " image: url(:/icons/lucide/check-on.svg); }\n"
        "QSplitter::handle { background: transparent; }\n"
        "QSplitter::handle:hover { background: %11; }\n"
        // HEADERS em NEGRITO com BARRA vertical separando as colunas (pedido).
        // A borda direita é o separador; a última seção não recebe, para não
        // desenhar uma barra solta na ponta.
        "QHeaderView::section { background: %14; color: %2; border: none;"
        " border-bottom: 1px solid %3; border-right: 1px solid %3;"
        " padding: %5px %6px; font-weight: 700; }\n"
        "QHeaderView::section:last { border-right: none; }\n"
        "QHeaderView::section:hover { background: %19; }\n"
        // TABELAS COM RESPIRO. As células ficavam coladas e sem altura para
        // editar (relatado: "não tem espaço pra editar"). Padding generoso nas
        // células, grade sutil e altura mínima para os editores inline caberem.
        "QTableWidget, QTableView { gridline-color: %3; }\n"
        "QTableWidget::item, QTableView::item { padding: %5px %6px; }\n"
        // Editores DENTRO da tabela: a regra genérica de QLineEdit/QComboBox tem
        // min-height de controle mais padding, e a soma estourava a altura da
        // linha — a moldura do campo vazava sobre a grade (relatado: "o frame do
        // campo tá saindo fora"). Aqui a altura é TRAVADA (max = min) e o padding
        // é enxuto; o respiro em volta vem das margens do cellHost.
        "QWidget#tableCellHost { background: transparent; }\n"
        // EDITORES DE CÉLULA — MESMA APARÊNCIA NOS DOIS CASOS.
        // Há dois tipos: os que EU embuto com setCellWidget (tabela de parâmetros
        // dinâmicos, alvos de terminal, chave-valor) e o que o DELEGATE cria ao
        // dar duplo clique numa célula de QTableWidgetItem (tabela de coleções).
        // Eles estavam visivelmente diferentes — altura 36 vs 32, padding 4/8 vs
        // 8/12 e raio 5 vs 9 (relatado). Agora ambos usam EXATAMENTE as métricas
        // da regra genérica de input (altura de controle, padding padrão, raio
        // médio), então editar uma célula de coleção fica igual a editar um campo
        // da tabela de parâmetros.
        "QTableWidget QLineEdit, QTableView QLineEdit,"
        " QTableWidget QComboBox, QTableView QComboBox,"
        " QTableWidget QSpinBox, QTableView QSpinBox {"
        " min-height: %24px; padding: %17px %23px; margin: 0px;"
        " border-radius: %18px; }\n"
        // O max-height fica RESTRITO aos editores embutidos: sem isso eles
        // estouram a linha e a moldura vaza sobre a grade. Aplicar ao editor do
        // delegate seria o que o achatava antes.
        "QWidget#tableCellHost QLineEdit, QWidget#tableCellHost QComboBox {"
        " max-height: %24px; }\n"
        // Célula selecionada não deve pintar por cima do editor embutido.
        "QTableWidget::item:selected, QTableView::item:selected {"
        " background-color: %19; }\n"
        "QHeaderView::section:horizontal { min-height: %16px; }\n")
        // ARG CHAIN: os placeholders deste bloco TÊM LACUNAS (não há %15, %20,
        // %21, %22). Como QString::arg é posicional e preenche o MENOR %N
        // restante a cada chamada, o chain precisa ter EXATAMENTE um arg por
        // placeholder DISTINTO, em ordem ASCENDENTE de %N. Args a mais
        // deslocavam toda a numeração (bug: "Argument missing" + cores caindo
        // em campos de tamanho, ex: "#3e3f4apx"). Ordem dos %N presentes:
        // 1..14, 16, 17, 18, 19, 23, 24.
        .arg(surface2)                       // %1  tooltip bg
        .arg(fg)                             // %2  texto
        .arg(border)                         // %3  bordas
        .arg(rSm)                            // %4  raio tooltip
        .arg(padY)                           // %5  padding vertical
        .arg(padX)                           // %6  padding horizontal
        .arg(tk::space(4))                   // %7  tamanho do indicador (16)
        .arg(tk::space(1))                   // %8  raio do indicador (4)
        .arg(bg)                             // %9  borda interna do radio checked
        .arg(accent)                         // %10 accent (hover/checked)
        .arg(tk::space(2))                   // %11 spacing checkbox
        .arg(tk::space(2))                   // %12 raio do radio
        .arg(muted)                          // %13 disabled
        .arg(surface)                        // %14 header bg
        .arg(standardRowHeight() - tk::space(2)) // %16 header min-height
        .arg(padY)                           // %17 padding vertical do editor de célula
        .arg(rMd)                            // %18 raio médio do editor
        .arg(hover)                          // %19 header hover / seleção
        .arg(padX)                           // %23 padding horizontal do editor
        .arg(tk::controlHeight());           // %24 altura do editor de célula

    // QToolTip: fonte menor que o corpo (herdava fsBody, 12pt — grande
    // demais pra um tooltip) + largura máxima, pra strings de hint longas
    // (ex: "settings.density.hint") quebrarem em várias linhas em vez de
    // uma única linha gigante sem wrap (bug relatado: "as caixinhas de
    // tooltip são muito grandes"). Regra PRÓPRIA em vez de somar %N na
    // cadeia gigante acima (mesmo motivo do radio abaixo: não renumerar).
    qss += QStringLiteral(
        "QToolTip { font-size: %1pt; max-width: 320px; }\n")
        .arg(fsSmall);

    // QComboBox — popup (lista aberta): o combo FECHADO já é filho real da
    // cascata QSS e sai temado, mas o popup (QComboBoxPrivateContainer) é
    // uma janela top-level separada cuja regra em ThemeManager::buildQss()
    // usava border == background (some visualmente) e cores cruas do JSON
    // em vez dos tokens (viola a regra de border-radius do AGENTS.md).
    // Esta regra é adicionada por ÚLTIMO (buildModernStylesheet vence,
    // convenção já documentada no topo do arquivo), com borda e raio
    // visíveis de verdade, então cobre/derruba a antiga.
    qss += QStringLiteral(
        "QComboBox QAbstractItemView { background-color: %1; color: %2;"
        " border: 1px solid %3; border-radius: %4px; padding: %5px; outline: none; }\n"
        "QComboBox QAbstractItemView::item { padding: %6px %7px; border-radius: %8px;"
        " min-height: %9px; }\n"
        "QComboBox QAbstractItemView::item:selected { background-color: %10; color: %2; }\n"
        "QComboBox QAbstractItemView::item:hover { background-color: %11; }\n")
        .arg(surface2).arg(fg).arg(border).arg(rMd).arg(tk::space(1))
        .arg(tk::space(1)).arg(padX).arg(rSm).arg(tk::controlHeight() - tk::space(2))
        .arg(sel).arg(hover);

    // QRadioButton::indicator:checked — regra PRÓPRIA (em vez de espremida
    // na cadeia %N acima, mesmo motivo do badge "Pulado" abaixo: não
    // precisar renumerar ~18 argumentos existentes). border-radius
    // REPETIDO aqui (a regra base QRadioButton::indicator já declara o
    // seu) e com um raio MAIOR, não o mesmo da base — achado real com
    // foto (diálogo de Exportar, JSON/YAML): o radio desmarcado saía
    // redondo certinho, mas MARCADO virava "quadrado arredondado" — a
    // borda do estado marcado é bem mais grossa (4px vs 1.5px), o que
    // INFLA a caixa renderizada além do content-box; com o MESMO raio da
    // base (metade do content-box original) isso já não cobre metade da
    // caixa INFLADA. space(3) (~12px) cobre a inflação sem precisar
    // calcular o crescimento exato da borda em cada densidade.
    qss += QStringLiteral(
        "QRadioButton::indicator:checked { background: %1; border: 4px solid %2;"
        " border-radius: %3px; outline: 1.5px solid %1; }\n")
        .arg(accent, bg).arg(tk::space(3));

    // --- TOGGLE SWITCH (kaiRole="switch") ---
    // Repaginação visual do Settings (mockup enviado pelo usuário): flags
    // booleanas viram um "pill switch" (trilho + bolinha) em vez da caixa de
    // checkbox padrão — reaproveita o MESMO QCheckBox/bool por trás (só
    // troca a pele via propriedade), sem novo widget/estado. SVG assado por
    // cor (ver themedSwitchSvgPath), igual à estratégia do check normal.
    //
    // ESTE BLOCO TEM QUE VIR DEPOIS de QUALQUER outro bloco que estilize
    // QCheckBox::indicator em geral (ver o bloco "CHECKBOX/RADIO" logo
    // acima de buildModernStylesheet, no trecho de tooltip/checkbox/
    // splitter/header) — o QSS engine do Qt não implementa a cascata CSS
    // por especificidade de forma confiável aqui; regras posteriores no
    // MESMO qss concatenado ganham de empates, então o switch precisa ser
    // o ÚLTIMO a declarar `QCheckBox[...]::indicator` pra sua largura/
    // altura/borda/fundo sempre vencerem, sem depender de especificidade.
    //
    // BUG REAL CORRIGIDO (achado com foto, "quadrado errado de fundo" no
    // diálogo de Exportar): uma sessão anterior adicionou um SEGUNDO
    // bloco de tema pra QCheckBox::indicator/QRadioButton::indicator bem
    // aqui, sem perceber que o app JÁ tinha um (o bloco "CHECKBOX/RADIO"
    // citado acima, mais antigo — borda fina + check-on.svg, fundo
    // transparente). Os dois brigavam pelas mesmas propriedades ao mesmo
    // tempo, produzindo um resultado inconsistente/glitchado em vez de
    // "sem estilo nenhum" (o bug original, ANTES de qualquer um dos dois
    // blocos existir, era mesmo o indicador nativo cru do Fusion). O
    // bloco duplicado foi removido — o tema original já cobre
    // QCheckBox/QRadioButton (e também QListView/QTreeView::indicator)
    // sozinho.
    {
        // 0.4 -> 0.7: o trilho DESLIGADO ficava perto demais de surface2 (o
        // mesmo fundo dos cards ao redor), praticamente some sob o card —
        // sobrava só a bolinha visível, sem trilho (relatado com print:
        // "checkbox ruim de enxergar por default", especificamente a
        // desligada). Mais peso da cor de borda deixa a pílula reconhecível
        // mesmo desligada, sem virar um preenchimento cheio (que pareceria
        // "ligado").
        const QColor offTrack(shiftToward(QColor(surface2), QColor(border), 0.7));
        const QString svgOff = themedSwitchSvgPath(false, offTrack, QColor(fg), QColor(border));
        const QString svgOn = themedSwitchSvgPath(true, QColor(accent), QColor(bg), QColor(accent));
        qss += QStringLiteral(
            "QCheckBox[kaiRole=\"switch\"] { spacing: %3px; }\n"
            "QCheckBox[kaiRole=\"switch\"]::indicator { width: 36px; height: 20px;"
            " border: none; background: transparent; image: url(%1); }\n"
            
            "QCheckBox[kaiRole=\"switch\"]::indicator:unchecked { "
            "   image: url(%1); "
            "   border-radius: 10px; "                 // Curvatura perfeita para 20px de altura
            "   border: 1px solid rgba(0, 0, 0, 0.15); " // Contraste sutil nas bordas
            "   background: rgba(0, 0, 0, 0.05); "       // Sombra interna leve
            "}\n"
            
            // É importante zerar a borda e o fundo no estado :checked para 
            // a sombra não vazar quando a switch estiver ligada.
            "QCheckBox[kaiRole=\"switch\"]::indicator:checked { "
            "   image: url(%2); "
            "   border: none; "
            "   background: transparent; "
            "}\n"
            
            "QCheckBox[kaiRole=\"switch\"]:disabled { color: %4; }\n")
            .arg(svgOff, svgOn).arg(tk::space(2)).arg(muted);
    }

    // --- CARDS de seção (QGroupBox já cobre a maioria — ver acima), aqui só
    // o card de LINHA (ex: um Perfil de Execução na lista de Perfis) que
    // precisa de destaque de borda quando é o item padrão/selecionado
    // (propriedade dinâmica kaiState="default"/"selected", repolida em
    // runtime pelo dono — ver TerminalProfilesEditorWidget). ---
    qss += QStringLiteral(
        "QFrame#profileCard { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }\n"
        "QFrame#profileCard[kaiState=\"default\"] { border: 1px solid %4; }\n"
        "QFrame#profileCard[kaiDrop=\"true\"] { border: 1px dashed %4; }\n")
        .arg(surface2).arg(border).arg(rLg).arg(accent);

    // --- SAÍDA V2: cabeçalho e abas do painel de saída ---
    // Pill de status COLORIDA por estado (mockup enviado pelo usuário: "●
    // Idle" num chip com fundo/texto verde, não um chip neutro com texto
    // colorido) — mistura sutil do bg base em direção à cor do estado
    // (shiftToward, mesmo helper já usado pras listras de zebra), opaca
    // (sem rgba() — evita artefato de alpha sobre conteúdo variável).
    const QString badgeIdleBg = hex(shiftToward(QColor(bg), QColor(muted), 0.18));
    const QString badgeRunningBg = hex(shiftToward(QColor(bg), QColor(tk::infoFg()), 0.38));
    const QString badgeSuccessBg = hex(shiftToward(QColor(bg), QColor(tk::successFg()), 0.38));
    const QString badgeErrorBg = hex(shiftToward(QColor(bg), QColor(tk::errorFg()), 0.38));
    qss += QStringLiteral(
        // Barra transparente (feedback do usuário: "deixe transparente"). Sem
        // fundo próprio no header nem no painel — a barra herda o fundo de
        // trás. A área de saída (páginas) tem o fundo do terminal.
        "QWidget#outputHeader { background: transparent; }\n"
        // Pill de status: agora é um QWidget (bolinha + texto), não mais um
        // QLabel — seletor por objectName sem tipo. O padding vem do layout
        // interno; aqui só o raio, o fundo por estado e a fonte (herdada
        // pelo QLabel de texto filho).
        "#outputStatusBadge { border-radius: %3px;"
        " background-color: %4; font-size: %5pt; font-weight: 600; }\n"
        "#outputStatusBadge[kaiState=\"idle\"] { background-color: %6; }\n"
        "#outputStatusBadge[kaiState=\"running\"] { background-color: %7; }\n"
        "#outputStatusBadge[kaiState=\"success\"] { background-color: %8; }\n"
        "#outputStatusBadge[kaiState=\"error\"] { background-color: %9; }\n"
        // Área de páginas: fundo do terminal + RAIO conforme a preferência de
        // canto do usuário (%10) — antes ficava quadrado fixo (relatado).
        "QStackedWidget#outputPages { background: %11; border: none;"
        " border-radius: %10px; }\n"
        "QTabBar#outputTabs { background: transparent; }\n"
        // Abas dentro da caixinha (surface2). A aba SELECIONADA ganha um
        // fundo próprio (chip) em vez da antiga linha de accent embaixo
        // (pedido do usuário: tirar a bordinha roxa, usar a cor da caixinha).
        // ALTURA EXPLÍCITA (%18): sem isto, cada ::tab usa a altura do seu
        // próprio sizeHint (ícone + texto + padding), quase sempre MENOR que
        // a QTabBar (fixa em m_barHeight, ver OutputPanel::setupUi) — o
        // rótulo então ficava colado no topo da aba, com um vão sobrando
        // embaixo (relatado: "a aba tem os itens desalinhados, embaixo tem
        // um espaço maior que em cima"). Fixando a altura do próprio ::tab
        // igual à da barra (descontada a margem vertical), o Qt centraliza
        // ícone+texto dentro dela de verdade.
        //
        // BUG REAL encontrado depois (relatado de novo: "o focus da saída
        // ainda está desalinhado com o outer da aba"), com número na mão
        // medido via amostragem de pixel + QTabBar::tabRect(): esta conta
        // ignorava que "height" no QSS é a altura do CONTEÚDO — o retângulo
        // final pintado da aba soma margin (2+2=4, ver "margin: 2px 1px"
        // abaixo) + padding (%12 em cima e embaixo, 2×) + este height. Com
        // o valor antigo (controlHeight()-4 = 28) a soma dava 4+8+28=40,
        // mas a QTabBar SÓ tinha 36px de altura (tinha um desconto de
        // space(1) em OutputPanel::setupUi que não existe mais) — a aba
        // pintava 4px além do fim da barra, cortada/vazando de forma
        // desigual (o motivo real do "desalinhado"). Agora a QTabBar
        // preenche a caixinha INTEIRA (m_barHeight, sem desconto) e esta
        // conta subtrai margin+padding de m_barHeight pra fechar exato:
        // altura da barra − margin (2+2) − padding (%12 × 2).
        "QTabBar#outputTabs::tab { padding: %12px %13px; color: %14;"
        " background: transparent; border: none; border-radius: %3px;"
        " margin: 2px 1px; height: %18px; font-size: %5pt; font-weight: 500; }\n"
        "QTabBar#outputTabs::tab:hover { color: %15; }\n"
        "QTabBar#outputTabs::tab:selected { color: %15; background: %16; }\n"
        // A entrada da saída é uma FAIXA colada no rodapé: sem raio e sem
        // borda em volta. A regra genérica de QLineEdit dava a ela um retângulo
        // arredondado flutuante, que somado à borda superior parecia uma linha
        // torta solta (bug reportado: "as linhas do terminal ficaram tortas").
        "QLineEdit#outputInput { border: none; border-top: 1px solid %17;"
        " border-radius: 0px; margin: 0px; }\n")
        // %3 = raio do badge de status: rSm (respeita a preferência de canto
        // do usuário — reto/suave/arredondado — em vez de uma pill fixa que
        // ignorava a estilização de borda, relatado).
        .arg(rSm).arg(bg).arg(fsSmall)
        .arg(badgeIdleBg).arg(badgeRunningBg).arg(badgeSuccessBg).arg(badgeErrorBg)
        // %10 = raio das páginas (respeita a preferência de canto), %11 = fundo
        // do terminal.
        .arg(rSm).arg(tk::terminalBg())
        // %12 = padding vertical das abas (pequeno: a altura da barra é fixa
        // em m_barHeight e o texto centraliza), %13 = padding horizontal.
        .arg(tk::space(1)).arg(tk::space(3))
        .arg(muted).arg(fg).arg(hover).arg(border)
        // %18 = altura de CONTEÚDO do ::tab: altura real da QTabBar
        // (m_barHeight = controlHeight() + space(2)) menos a margem
        // vertical (2px + 2px) e menos o padding vertical (%12, 2×) — ver
        // comentário acima da regra pro porquê exato.
        .arg(tk::controlHeight() + tk::space(2) - 4 - 2 * tk::space(1));

    // Badge "Pulado" (Execution Condition não atendida — ver
    // OutputStatus::Skipped): regra própria, em vez de espremida na cadeia
    // %N acima, pra não ter que renumerar os ~18 argumentos existentes.
    qss += QStringLiteral(
        "#outputStatusBadge[kaiState=\"skipped\"] { background-color: %1; }\n")
        .arg(hex(shiftToward(QColor(bg), QColor(tk::warningFg()), 0.38)));

    // --- Navegação lateral (Settings em blocos): item selecionado com faixa
    //     de accent à esquerda, estilo painel de preferências moderno. ---
    qss += QStringLiteral(
        "QListWidget#settingsNav { background-color: %1; border: none;"
        " border-right: 1px solid %2; padding: %3px %4px; outline: none; }\n"
        "QListWidget#settingsNav::item { padding: %5px %6px; border-radius: %7px;"
        " color: %8; border-left: 2px solid transparent; }\n"
        "QListWidget#settingsNav::item:hover { background-color: %9; color: %10; }\n"
        "QListWidget#settingsNav::item:selected { background-color: %11;"
        " color: %10; border-left: 2px solid %12; font-weight: %13; }\n")
        .arg(surface).arg(border).arg(tk::space(2)).arg(tk::space(1))
        .arg(padY).arg(padX).arg(rSm).arg(muted).arg(hover).arg(fg)
        .arg(sel).arg(accent).arg(wTitle);

    // --- Handle do QSplitter: uma LINHA de 1px na cor de borda do tema,
    // igual ao separador da barra de ações do topo (referência do usuário).
    // Fundo transparente + uma borda fina no meio do handle (não um bloco
    // cheio, que ficava grosso demais). Vertical = linha horizontal
    // (border-top); horizontal = linha vertical (border-left). O hover
    // reforça com a cor de accent, sem "sumir". ---
    qss += QStringLiteral(
        "QSplitter::handle { background: transparent; }\n"
        "QSplitter::handle:horizontal { border-left: 1px solid %1; }\n"
        "QSplitter::handle:vertical { border-top: 1px solid %1; }\n"
        "QSplitter::handle:horizontal:hover { border-left: 1px solid %2; }\n"
        "QSplitter::handle:vertical:hover { border-top: 1px solid %2; }\n")
        .arg(border, accent);

    // --- Gradientes de tema, em 3 BASES reutilizáveis (pedido do usuário:
    // "quero gradientes diferentes, para que o tema possa decidir se usa
    // ou não em tal lugar") — cada área do app usa uma das 3 bases, nunca
    // uma cor própria isolada, então o tema controla TODAS as áreas de uma
    // categoria com um único par start/end:
    //   - "primary":   chrome do app (janela, diálogos, header, árvore de
    //                   comandos) E as superfícies da Saída (cartões da
    //                   aba Requisição/Headers, stack de saída) — "app e
    //                   saídas", pedido do usuário.
    //   - "secondary":  caixinhas de texto (QLineEdit/QComboBox/
    //                   QPlainTextEdit) — pedido do usuário.
    //   - "tertiary":   botões e entalhes (QPushButton primário, toggle de
    //                   modo avançado/switches) — pedido do usuário.
    // Cada base só entra em jogo quando o tema ATIVO declara o par de
    // cores para ela (ver tk::hasGradient) e o interruptor mestre está
    // ligado (Configurações -> Aparência -> "Gradientes do tema", ver
    // tk::setGradientsEnabled); senão a string vem vazia e o fundo sólido
    // de sempre (theme.qss) continua valendo — nenhum destes blocos força
    // um fallback próprio, o tema decide base a base.
    //
    // "primary" no chrome do app: mesmos seletores que já pintavam o bg
    // sólido em theme.qss (QWidget#rootContainer/QDialog/TopUtilityBar/
    // árvore de comandos); aqui só a propriedade background-color é
    // substituída, border/border-radius continuam os mesmos declarados lá.
    if (tk::hasGradient(QStringLiteral("primary"))) {
        const QString primaryBg = tk::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary"));
        qss += QStringLiteral("QWidget#rootContainer { %1 }\n").arg(primaryBg);
        // Diálogos (Editar Comando, Configurações, etc.) são janelas
        // PRÓPRIAS, fora da hierarquia de #rootContainer — sem esta regra,
        // ficavam com o fundo SÓLIDO genérico de QDialog (theme.qss),
        // destoando do resto do app (relatado pelo usuário: "a aba de
        // configuração não usa o mesmo tema das outras abas... fundos de
        // gradiente").
        qss += QStringLiteral("QDialog { %1 }\n").arg(primaryBg);
        // TopUtilityBar (barra superior/título) — seletor por CLASSE
        // (Q_OBJECT já expõe o className pro motor de QSS), sem precisar
        // de objectName dedicado.
        qss += QStringLiteral("TopUtilityBar { %1 }\n").arg(primaryBg);
        // Árvore de comandos — só as QTreeWidget internas de
        // CommandTreeWidget (seletor descendente por classe), não
        // qualquer QTreeWidget do app (diálogos etc). Quando o usuário tem
        // uma imagem de fundo própria configurada (Configurações ->
        // Aparência -> "Plano de fundo"), CommandTreeWidget aplica um
        // setStyleSheet LOCAL na própria árvore que sempre vence este aqui
        // (ver CommandTreeWidget::applyBackgroundStyle) — sem conflito.
        qss += QStringLiteral("CommandTreeWidget QTreeWidget { %1 }\n").arg(primaryBg);
    }
    // Fundo da árvore de comandos SEMPRE transparente via QSS — em TODOS os
    // estados, inclusive o normal/parado (pedido do usuário: "as bordas são
    // entre as células" — a árvore tem 2 colunas, nome/ícone e status; o
    // Qt pinta hover/seleção por CÉLULA, e a segunda coluna, vazia na maior
    // parte do tempo, nunca entra no estado :hover — só a que está sob o
    // cursor. SEM uma regra de fundo para o estado normal, o Qt cai no
    // fallback nativo (zebra da paleta) SÓ nessa célula, repintando por
    // cima do preenchimento manual e reabrindo o vão — daí a "bordinha"
    // reaparecer só passando o mouse, mesmo com :hover/:selected já
    // transparentes). Com TODO estado zerado aqui, o DraggableTreeWidget é
    // o ÚNICO responsável pelo fundo (zebra, hover e seleção), cobrindo a
    // linha inteira de uma vez (ver DraggableTreeWidget::setRowColors) — escopado só a
    // esta árvore (seletor descendente por classe), sem afetar nenhuma
    // outra lista/árvore do app.
    // border/outline explícitos aqui também: o item "atual" (foco de
    // teclado/clique) ganha do Fusion um contorno/retângulo pontilhado
    // PRÓPRIO da célula (distinto do preenchimento de fundo, e SEM
    // depender de hover) — relatado pelo usuário como a bordinha
    // aparecendo "no focus, nem precisa do mouse em cima". outline:none já
    // existe no seletor genérico do container (mais acima), mas reforçamos
    // aqui, escopado e com prioridade máxima, direto no ::item.
    // border-radius: 0px REPETIDO aqui (a regra base mais acima já zera,
    // mas pra esta ficar 100% auto-contida e não depender de nenhuma outra
    // regra pra não reintroduzir cantos arredondados no hover/zebra —
    // relatado pelo usuário: "na linha zebrada, ao fazer hover ainda
    // aparece as bordinhas redondas").
    qss += QStringLiteral(
        "CommandTreeWidget QTreeWidget::item, CommandTreeWidget QTreeWidget::item:hover,"
        " CommandTreeWidget QTreeWidget::item:selected,"
        " CommandTreeWidget QTreeWidget::item:selected:active,"
        " CommandTreeWidget QTreeWidget::item:selected:!active,"
        " CommandTreeWidget QTreeWidget::item:focus {"
        " background-color: transparent; border: none; outline: none; border-radius: 0px; }\n");
    // "tertiary": botões e entalhes (pedido do usuário) — aplicado aqui só
    // no botão PRIMÁRIO (kaiRole="primary"), o "badge/acento pontual" mais
    // recorrente da UI. Badges de STATUS (#outputStatusBadge) ficam de
    // fora de propósito — ali a cor carrega significado (verde=sucesso,
    // vermelho=erro) e um gradiente decorativo por cima diluiria esse
    // sinal.
    if (tk::hasGradient(QStringLiteral("tertiary"))) {
        const QString tertiaryBg = tk::gradientQss(QStringLiteral("background-color"), QStringLiteral("tertiary"));
        // kaiRole="primary" é usado em pouquíssimos lugares — a maioria dos
        // botões "de ação" (accent sólido) do app não passa por essa
        // property, e sim por um setStyleSheet LOCAL (ver
        // CollapsibleSectionCard::m_actionButton, que sempre vence um
        // seletor global — também migrado pra base "tertiary", ver
        // collapsible-section-card.cpp). Cobrimos aqui os dois casos que
        // SÃO globais: kaiRole="primary" em si, e o botão :default de
        // QDialogButtonBox (OK/Salvar na maioria dos diálogos) — mesmo
        // seletor que já pinta o accent sólido em theme-manager.cpp, só a
        // propriedade background-color é substituída.
        qss += QStringLiteral(
            "QPushButton[kaiRole=\"primary\"] { %1 border: none; }\n"
            "QDialogButtonBox QPushButton:default { %1 border: none; }\n")
            .arg(tertiaryBg);
    }
    // "secondary": caixinhas de texto (pedido do usuário) — QLineEdit/
    // QComboBox/QPlainTextEdit. Inserido POR ÚLTIMO (mesma técnica já
    // usada no bloco "Modernização dos inputs" abaixo) pra vencer a
    // declaração sólida anterior com a MESMA especificidade; sem gradiente
    // declarado no tema, esta regra não entra em jogo e o fundo sólido de
    // sempre continua valendo.
    if (tk::hasGradient(QStringLiteral("secondary"))) {
        const QString secondaryBg = tk::gradientQss(QStringLiteral("background-color"), QStringLiteral("secondary"));
        qss += QStringLiteral(
            "QLineEdit, QComboBox, QPlainTextEdit, QTextEdit { %1 }\n")
            .arg(secondaryBg);
    }

    return qss;
}

void applyElevation(QWidget *widget, int level)
{
    if (!widget) {
        return;
    }
    if (!tk::effects().shadows) {
        // Efeito desligado: remove qualquer sombra anterior (troca em runtime).
        if (qobject_cast<QGraphicsDropShadowEffect *>(widget->graphicsEffect())) {
            widget->setGraphicsEffect(nullptr);
        }
        return;
    }
    auto *shadow = new QGraphicsDropShadowEffect(widget);
    const int lvl = qBound(1, level, 3);
    shadow->setBlurRadius(8.0 * lvl + 8.0);
    shadow->setOffset(0, 1.5 * lvl);
    QColor c(0, 0, 0);
    c.setAlphaF(0.18 + 0.06 * lvl);
    shadow->setColor(c);
    widget->setGraphicsEffect(shadow);
}

void applyWindowBackdrop(QWidget *window)
{
    if (!window) {
        return;
    }
    const auto &fx = tk::effects();
    if (!fx.translucency && !fx.blur) {
        window->setAttribute(Qt::WA_TranslucentBackground, false);
        return;
    }

#if defined(Q_OS_WIN)
    // Windows 11 (build 22000+): backdrop NATIVO do sistema. Isto é o
    // "material líquido" de verdade — o compositor do Windows faz o blur do
    // que está atrás da janela, com custo praticamente zero para o app.
    if (fx.blur) {
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (hwnd) {
            // DWMWA_SYSTEMBACKDROP_TYPE = 38; 2 = Mica, 3 = Acrylic, 4 = Tabbed.
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20.
            int backdrop = 3; // Acrylic: mais "vidro" que o Mica
            ::DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
            BOOL dark = QColor(tk::bg()).lightnessF() < 0.5 ? TRUE : FALSE;
            ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            window->setAttribute(Qt::WA_TranslucentBackground, true);
            return;
        }
    }
#endif

    // Demais plataformas (e fallback do Windows): translucidez do próprio
    // widget. Sem compositor com blur (ex: WSLg) o resultado é um vidro
    // "fosco" simples, não um blur real — degradação consciente.
    if (fx.translucency) {
        window->setWindowOpacity(0.97);
    }
}

void animateFadeIn(QWidget *widget, int durationMs)
{
    if (!widget || !tk::effects().animations) {
        return;
    }
    auto *opacity = new QGraphicsOpacityEffect(widget);
    opacity->setOpacity(0.0);
    widget->setGraphicsEffect(opacity);
    auto *anim = new QPropertyAnimation(opacity, "opacity", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    // Ao terminar, REMOVE o efeito: manter um QGraphicsEffect ativo força
    // repaint em camada offscreen e custa performance de rolagem.
    QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
        widget->setGraphicsEffect(nullptr);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace kai::ui
