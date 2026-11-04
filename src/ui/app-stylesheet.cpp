#include "ui/app-stylesheet.h"

#include <cmath>

#include "ui/shared/table-utils.h"
#include "ui/shared/tab-bar-style.h"
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

// Gera (e cacheia em disco) o SVG de um toggle switch (pílula + bolinha) dos QCheckBox com kaiRole="switch". O QSS não
// recolore SVG, então a cor é assada no arquivo: um caminho por (estado, cor de trilho, cor da bolinha).
QString themedSwitchSvgPath(bool checked, const QColor &track, const QColor &knob, const QColor &border)
{
    const QString dir = QDir::tempPath() + QStringLiteral("/kai-theme");
    QDir().mkpath(dir);
    const QString state = checked ? QStringLiteral("on") : QStringLiteral("off");
    const QString path = dir + QStringLiteral("/switch-%1-%2-%3.svg")
        .arg(state, track.name(QColor::HexRgb).mid(1), knob.name(QColor::HexRgb).mid(1));
    const qreal cx = checked ? 26.0 : 10.0;
    // stroke-width 2: com 1.5 o contorno do trilho sumia em temas de pouco contraste.
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

QString themeStyleSheet(const QString &themeQss)
{
    QString qss = themeQss;
    qss.replace(QStringLiteral("@RADIUS_MD@"), QString::number(tk::radiusMd()) + QStringLiteral("px"));
    qss.replace(QStringLiteral("@RADIUS_SM@"), QString::number(tk::radiusSm()) + QStringLiteral("px"));
    return qss + buildModernStylesheet();
}

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

    qss += QStringLiteral(
        "QWidget { font-family: '%1'; font-size: %2pt; }\n"
        "QLabel[kaiRole=\"title\"] { font-size: %3pt; font-weight: %4; color: %5; }\n"
        "QLabel[kaiRole=\"subtitle\"] { font-size: %2pt; font-weight: 500; color: %5; }\n"
        "QLabel[kaiRole=\"caption\"] { font-size: %6pt; color: %7; }\n")
        .arg(tk::fontFamily()).arg(fsBody).arg(fsTitle).arg(wTitle).arg(fg).arg(fsSmall).arg(muted);

    qss += QStringLiteral(
        "QDialog { background-color: %1; }\n"
        "QGroupBox { background-color: %2; border: 1px solid %3; border-radius: %4px;"
        " margin-top: %5px; padding: %6px; }\n"
        "QGroupBox::title { subcontrol-origin: margin; left: %6px; padding: 0 %7px;"
        " color: %8; font-weight: %9; }\n")
        .arg(surface).arg(surface2).arg(border).arg(rLg)
        .arg(tk::space(2)).arg(tk::space(3)).arg(tk::space(1)).arg(muted).arg(wTitle);

    qss += QStringLiteral(
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit {"
        " background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
        " padding: %5px %6px; min-height: %7px; }\n"
        // O padding da regra acima também valia para a área dos botões, empurrando as setas e deixando uma faixa vazia:
        // zera o padding à direita e dá geometria própria aos sub-controles.
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
        .arg(tk::space(4)).arg((ctrlH - 4) / 2).arg(hover);

    qss += QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; padding: %5px %6px; min-height: %7px; font-weight: 500;"
        " outline: none; }\n"
        "QPushButton:focus { border: 1px solid %11; }\n"
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

    qss += QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: %1px;"
        " padding: %2px; }\n"
        "QToolButton:hover { background-color: %3; }\n"
        "QToolButton:pressed { background-color: %4; }\n"
        "QToolButton:checked { background-color: %4; }\n")
        .arg(rSm).arg(tk::space(1)).arg(hover).arg(sel);

    // Seleção sóbria: o QSS do core pintava a linha com o accent puro (contraste estourado, briga com as listras). Aqui é
    // uma superfície discreta com texto em accent; cobre :active/:!active porque o :selected:active do core é mais específico.
    // Listras: desvio mínimo do fundo, de tk::treeStripeBg() para o DraggableTreeWidget repintar a MESMA cor (ver
    // command-tree-widget.cpp).
    const QString stripe = tk::treeStripeBg();
    // Árvore: padding vertical um degrau acima do genérico e seleção só com preenchimento sólido, sem borda (numa árvore
    // de várias colunas uma borda por ::item vira uma caixa por célula). Não há regra para QTreeView::branch: o QSS nunca
    // pinta aquela área; linhas e indicadores são desenhados em C++ por DraggableTreeWidget::drawBranches.
    const int treePadY = padY + 1;
    qss += QStringLiteral(
        "QTreeWidget, QTreeView, QListWidget, QListView, QTableWidget, QTableView {"
        " background-color: %1; border: none; outline: none; alternate-background-color: %8;"
        " show-decoration-selected: 1; }\n"
        "QListView::item, QListWidget::item {"
        " padding: %2px %3px; border-radius: %4px; }\n"
        // Itens da árvore de comandos colados, sem cantos arredondados entre linhas (bloco contínuo).
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

    // Tabelas seguem a preferência de cantos: a própria tabela é a caixa arredondada e o viewport fica transparente (senão
    // um quadrado vazaria pelos cantos do card); o recuo interno mantém cabeçalho e linhas dentro do arco (p >= 0,3r).
    {
        const int tableRadius = tk::radiusMd();
        const int tablePad = tableRadius <= 0 ? 2 : static_cast<int>(std::ceil(tableRadius * 0.3)) + 1;
        qss += QStringLiteral(
            "QTableWidget, QTableView, QListWidget[kaiRole=\"insetList\"] { border-radius: %1px; padding: %2px; }\n"
            "QTableWidget QWidget#qt_scrollarea_viewport, QTableView QWidget#qt_scrollarea_viewport,"
            " QListWidget[kaiRole=\"insetList\"] QWidget#qt_scrollarea_viewport { background: transparent; }\n"
            // Viewport transparente: a trilha transparente da scrollbar deixava ver o fundo da janela; usa a cor do corpo.
            "QTableWidget QScrollBar:vertical, QTableView QScrollBar:vertical,"
            " QListWidget[kaiRole=\"insetList\"] QScrollBar:vertical,"
            " QTableWidget QScrollBar:horizontal, QTableView QScrollBar:horizontal,"
            " QListWidget[kaiRole=\"insetList\"] QScrollBar:horizontal { background: %3; }\n")
            .arg(tableRadius).arg(tablePad).arg(bg);
    }

    qss += QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }\n"
        // Mesma altura (a da barra menos os 2px do sublinhado), recuo e fonte das abas da Saída e das pastas raiz.
        "QTabBar::tab { background: transparent; color: %2; padding: %3px %4px;"
        " border: none; border-bottom: 2px solid transparent; font-weight: 500;"
        " height: %8px; font-size: %9pt; }\n"
        "QTabBar::tab:hover { color: %5; }\n"
        "QTabBar::tab:selected { color: %5; border-bottom: 2px solid %6;"
        " font-weight: %7; }\n")
        .arg(bg).arg(tk::tabInactiveFg()).arg(0).arg(tk::space(3)).arg(fg).arg(accent).arg(wTitle)
        .arg(standardTabBarHeight() - 2).arg(tk::fontSizePt());

    // Só min-height na alça (como sempre foi): um min-width aqui entra no tamanho mínimo das áreas de rolagem com a barra
    // horizontal escondida (QAbstractScrollArea soma o tamanho das duas barras) e alarga a coluna de ações.
    // A alça usa o texto discreto com transparência, não a cor de BORDA: em temas escuros a borda quase não se
    // distingue do fundo e a barra "sumia" quando a lista passava do espaço (relatado).
    const QColor mutedColor(muted);
    const auto withAlpha = [&mutedColor](int alpha) {
        return QStringLiteral("rgba(%1, %2, %3, %4)").arg(mutedColor.red()).arg(mutedColor.green()).arg(mutedColor.blue()).arg(alpha);
    };
    // A alça de redimensionar no canto dos diálogos aparecia como um quadradinho: o redimensionamento já é feito pelas
    // bordas da moldura, então a alça fica invisível (continua existindo).
    qss += QStringLiteral("QSizeGrip { background: transparent; image: none; }\n");
    qss += QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: %1px; margin: 0; }\n"
        "QScrollBar:horizontal { background: transparent; height: %1px; margin: 0; }\n"
        "QScrollBar::handle { background: %2; border-radius: %3px; min-height: %4px; }\n"
        "QScrollBar::handle:hover { background: %5; }\n"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }\n"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }\n")
        .arg(tk::space(3)).arg(withAlpha(110)).arg(tk::space(1)).arg(tk::space(6)).arg(withAlpha(190));

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

    qss += QStringLiteral(
        "QLabel[kaiState=\"running\"] { color: %1; font-weight: 600; }\n"
        "QLabel[kaiState=\"success\"] { color: %2; font-weight: 600; }\n"
        "QLabel[kaiState=\"error\"] { color: %3; font-weight: 600; }\n"
        "QLabel[kaiState=\"idle\"] { color: %4; }\n")
        .arg(tk::infoFg()).arg(tk::successFg()).arg(tk::errorFg()).arg(muted);

    qss += QStringLiteral(
        "QToolTip { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; padding: %5px %6px; }\n"
        // Checkbox/radio sem preenchimento cheio de accent ao marcar: só um tick, borda de accent e ícone de check (SVG
        // gerado no tema); o radio usa raio total.
        "QCheckBox, QRadioButton { spacing: %11px; background: transparent; }\n"
        "QCheckBox::indicator, QRadioButton::indicator { width: %7px; height: %7px;"
        " border: 1.5px solid %3; border-radius: %8px; background: transparent; }\n"
        // Desmarcado com fundo preenchido (%9, o bg base) em vez de transparente: dentro de card surface/surface2 a borda
        // fina sumia.
        // Idem no radio desmarcado (a regra acima cobre só QCheckBox).
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
        // Check de itens de lista/árvore: mesma aparência da checkbox do tema (caixa transparente + tick do qrc).
        "QListView::indicator, QTreeView::indicator {"
        " width: %7px; height: %7px; border: 1.5px solid %3;"
        " border-radius: %8px; background: transparent; }\n"
        "QListView::indicator:hover, QTreeView::indicator:hover {"
        " border: 1.5px solid %10; }\n"
        // Fundo cheio no marcado: o tick do qrc é branco, e sobre o fundo
        // claro do tema light ele sumia (caixa marcada parecia vazia).
        "QListView::indicator:checked, QTreeView::indicator:checked {"
        " background: %10; border: 1.5px solid %10;"
        " image: url(:/icons/lucide/check-on.svg); }\n"
        "QSplitter::handle { background: transparent; }\n"
        "QSplitter::handle:hover { background: %11; }\n"
        // Headers em negrito com barra vertical entre colunas (borda direita; a última seção não recebe).
        "QHeaderView::section { background: %14; color: %2; border: none;"
        " border-bottom: 1px solid %3; border-right: 1px solid %3;"
        " padding: %5px %6px; font-weight: 700; }\n"
        "QHeaderView::section:last { border-right: none; }\n"
        "QHeaderView::section:hover { background: %19; }\n"
        // Tabelas com respiro: padding generoso nas células, grade sutil e altura mínima para os editores inline.
        "QTableWidget, QTableView { gridline-color: %3; }\n"
        "QTableWidget::item, QTableView::item { padding: %5px %6px; }\n"
        // Editores dentro da tabela: min-height + padding da regra genérica estouravam a altura da linha e a moldura vazava
        // sobre a grade; altura travada (max = min) e padding enxuto.
        "QWidget#tableCellHost { background: transparent; }\n"
        // Editores de célula com a mesma aparência nos dois tipos (setCellWidget e o criado pelo delegate no duplo clique):
        // métricas da regra genérica de input (altura de controle, padding padrão, raio médio).
        "QTableWidget QLineEdit, QTableView QLineEdit,"
        " QTableWidget QComboBox, QTableView QComboBox,"
        " QTableWidget QSpinBox, QTableView QSpinBox {"
        " min-height: %24px; padding: %17px %23px; margin: 0px;"
        " border-radius: %18px; }\n"
        // max-height restrito aos editores embutidos; no editor do delegate achatava o campo.
        "QWidget#tableCellHost QLineEdit, QWidget#tableCellHost QComboBox {"
        " max-height: %24px; }\n"
        // Célula selecionada não deve pintar por cima do editor embutido.
        "QTableWidget::item:selected, QTableView::item:selected {"
        " background-color: %19; }\n"
        "QHeaderView::section:horizontal { min-height: %16px; }\n")
        // ARG CHAIN: os placeholders têm lacunas (sem %15, %20-%22) e QString::arg preenche o menor %N restante, então o
        // chain precisa de exatamente um arg por placeholder distinto, em ordem ascendente: 1..14, 16, 17, 18, 19, 23, 24.
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

    // QToolTip com fonte menor que o corpo e largura máxima (hints longos quebram em várias linhas). Regra própria para
    // não renumerar a cadeia %N.
    qss += QStringLiteral(
        "QToolTip { font-size: %1pt; max-width: 320px; }\n")
        .arg(fsSmall);

    // Popup do QComboBox (janela top-level separada): a regra de ThemeManager::buildQss() usava border == background e
    // cores cruas do JSON. Esta vem por último (buildModernStylesheet vence), com borda e raio visíveis.
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
    // Moldura do popup transparente: quem desenha fundo/borda/raio é a lista
    // acima (a janela é translúcida, ver ComboPopupFilter).
    qss += QStringLiteral("QComboBoxPrivateContainer { background: transparent; border: none; }\n");
    // Lista SEMPRE abre abaixo do campo (dropdown), no padrão do select de
    // parâmetro. O estilo Fusion abre os combos não editáveis como "popup"
    // sobreposto ao campo, com moldura branca própria e fora do tema.
    qss += QStringLiteral("QComboBox { combobox-popup: 0; }\n");

    // QRadioButton::indicator:checked em regra própria (sem renumerar a cadeia %N), com raio MAIOR que o da base: a borda
    // do estado marcado é bem mais grossa (4px vs 1.5px) e infla a caixa além do content-box, e o raio da base não cobriria
    // metade dela. space(3) cobre a inflação.
    qss += QStringLiteral(
        "QRadioButton::indicator:checked { background: %1; border: 4px solid %2;"
        " border-radius: %3px; outline: 1.5px solid %1; }\n")
        .arg(accent, bg).arg(tk::space(3));

    // Toggle switch (kaiRole="switch"): o mesmo QCheckBox/bool com outra pele (pílula: trilho + bolinha, SVG assado por cor,
    // ver themedSwitchSvgPath). Tem que vir DEPOIS de qualquer bloco que estilize QCheckBox::indicator: o engine de QSS do
    // Qt não resolve a cascata por especificidade de forma confiável, regras posteriores ganham empates. Não duplicar o
    // bloco CHECKBOX/RADIO aqui: os dois brigavam pelas mesmas propriedades.
    {
        // 0.7 (era 0.4): o trilho desligado ficava perto demais de surface2 (fundo dos cards) e sumia, sobrando só a bolinha.
        const QColor offTrack(shiftToward(QColor(surface2), QColor(border), 0.7));
        const QString svgOff = themedSwitchSvgPath(false, offTrack, QColor(fg), QColor(border));
        const QString svgOn = themedSwitchSvgPath(true, QColor(accent), QColor(bg), QColor(accent));
        qss += QStringLiteral(
            "QCheckBox[kaiRole=\"switch\"] { spacing: %3px; }\n"
            "QCheckBox[kaiRole=\"switch\"]::indicator { width: 36px; height: 20px;"
            " border: none; background: transparent; image: url(%1); }\n"
            
            "QCheckBox[kaiRole=\"switch\"]::indicator:unchecked { "
            "   image: url(%1); "
            "   border-radius: 10px; "
            "   border: 1px solid rgba(0, 0, 0, 0.15); "
            "   background: rgba(0, 0, 0, 0.05); "
            "}\n"
            
            // Zera borda e fundo no :checked para a sombra não vazar com a switch ligada.
            "QCheckBox[kaiRole=\"switch\"]::indicator:checked { "
            "   image: url(%2); "
            "   border: none; "
            "   background: transparent; "
            "}\n"
            
            "QCheckBox[kaiRole=\"switch\"]:disabled { color: %4; }\n")
            .arg(svgOff, svgOn).arg(tk::space(2)).arg(muted);
    }

    // Card de LINHA (ex.: Perfil de Execução) com destaque de borda quando padrão/selecionado (kaiState="default"/
    // "selected", repolido pelo dono; ver TerminalProfilesEditorWidget).
    qss += QStringLiteral(
        "QFrame#profileCard { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }\n"
        "QFrame#profileCard[kaiState=\"default\"] { border: 1px solid %4; }\n"
        "QFrame#profileCard[kaiDrop=\"true\"] { border: 1px dashed %4; }\n")
        .arg(surface2).arg(border).arg(rLg).arg(accent);

    // Pill de status colorida por estado: mistura sutil do bg base em direção à cor do estado (shiftToward), opaca (sem
    // rgba(): evita artefato de alpha sobre conteúdo variável).
    const QString badgeIdleBg = hex(shiftToward(QColor(bg), QColor(muted), 0.18));
    const QString badgeRunningBg = hex(shiftToward(QColor(bg), QColor(tk::infoFg()), 0.38));
    const QString badgeSuccessBg = hex(shiftToward(QColor(bg), QColor(tk::successFg()), 0.38));
    const QString badgeErrorBg = hex(shiftToward(QColor(bg), QColor(tk::errorFg()), 0.38));
    qss += QStringLiteral(
        // Barra transparente: sem fundo no header nem no painel; a área de páginas tem o fundo do terminal.
        "QWidget#outputHeader { background: transparent; }\n"
        // A pill de status é um QWidget (bolinha + texto): seletor por objectName; aqui só raio, fundo por estado e fonte.
        "#outputStatusBadge { border-radius: %3px;"
        " background-color: %4; font-size: %5pt; font-weight: 600; }\n"
        "#outputStatusBadge[kaiState=\"idle\"] { background-color: %6; }\n"
        "#outputStatusBadge[kaiState=\"running\"] { background-color: %7; }\n"
        "#outputStatusBadge[kaiState=\"success\"] { background-color: %8; }\n"
        "#outputStatusBadge[kaiState=\"error\"] { background-color: %9; }\n"
        // Páginas: fundo do terminal + raio da preferência de canto (%10).
        "QStackedWidget#outputPages { background: %11; border: none;"
        " border-radius: %10px; }\n"
        "QTabBar#outputTabs { background: transparent; }\n"
        // Abas dentro da caixinha (surface2); a selecionada ganha fundo próprio (chip). Altura explícita (%18): "height" no
        // QSS é a altura do CONTEÚDO, então o ::tab usa a da QTabBar (m_barHeight, ver OutputPanel::setupUi) menos margin
        // (2+2) e padding vertical (%12 x 2); com outra conta a aba vazava da barra ou ficava desalinhada.
        "QTabBar#outputTabs::tab { padding: %12px %13px; color: %14;"
        " background: transparent; border: none; border-radius: %3px;"
        " margin: 2px 1px; height: %18px; font-size: %5pt; font-weight: 500; }\n"
        "QTabBar#outputTabs::tab:hover { color: %15; }\n"
        "QTabBar#outputTabs::tab:selected { color: %15; background: %16; }\n"
        // A entrada da saída é uma faixa colada no rodapé, sem raio nem borda (a regra genérica de QLineEdit a deixava um retângulo flutuante).
        "QLineEdit#outputInput { border: none; border-top: 1px solid %17;"
        " border-radius: 0px; margin: 0px; }\n")
        // %3 = raio do badge de status: rSm (respeita a preferência de canto).
        .arg(rSm).arg(bg).arg(fsSmall)
        .arg(badgeIdleBg).arg(badgeRunningBg).arg(badgeSuccessBg).arg(badgeErrorBg)
        // %10 = raio das páginas (respeita a preferência de canto), %11 = fundo
        // do terminal.
        .arg(rSm).arg(tk::terminalBg())
        // %12 = padding vertical das abas (pequeno: a altura da barra é fixa
        // em m_barHeight e o texto centraliza), %13 = padding horizontal.
        .arg(tk::space(1)).arg(tk::space(3))
        .arg(tk::tabInactiveFg()).arg(fg).arg(hover).arg(border)
        // %18 = altura de conteúdo do ::tab (ver a regra acima).
        .arg(tk::controlHeight() + tk::space(2) - 4 - 2 * tk::space(1));

    // Badge "Pulado" (OutputStatus::Skipped) em regra própria, sem renumerar a cadeia %N.
    qss += QStringLiteral(
        "#outputStatusBadge[kaiState=\"skipped\"] { background-color: %1; }\n")
        .arg(hex(shiftToward(QColor(bg), QColor(tk::warningFg()), 0.38)));

    // Navegação lateral (Settings): item selecionado em destaque arredondado; sem faixa de accent à esquerda (borda lateral
    // num item com raio desenha um arco torto).
    qss += QStringLiteral(
        "QListWidget#settingsNav { background-color: %1; border: none;"
        " border-right: 1px solid %2; padding: %3px %4px; outline: none; }\n"
        "QListWidget#settingsNav::item { padding: %5px %6px; border-radius: %7px;"
        " color: %8; }\n"
        "QListWidget#settingsNav::item:hover { background-color: %9; color: %10; }\n"
        "QListWidget#settingsNav::item:selected { background-color: %11;"
        " color: %10; font-weight: %12; }\n")
        .arg(surface).arg(border).arg(tk::space(2)).arg(tk::space(1))
        .arg(padY).arg(padX).arg(rSm).arg(muted).arg(hover).arg(fg)
        .arg(sel).arg(wTitle);

    // Moldura dos painéis principais ("panelCard"): mesma borda de 1px + raio dos tokens; o miolo mantém a cor de cada
    // painel. Os widgets precisam de WA_StyledBackground.
    qss += QStringLiteral(
        "QWidget#panelCard { background: transparent; border: 1px solid %1;"
        " border-radius: %2px; }\n"
        // Painel que fica logo abaixo da faixa de abas de uma zona: a faixa é a borda de cima (arredondada), então o
        // cartão cola nela — sem a própria borda e os cantos de cima retos; só os de baixo seguem o raio.
        "QWidget#panelCard[kaiTabbed=\"true\"] { border-top: none;"
        " border-top-left-radius: 0px; border-top-right-radius: 0px; }\n"
        // Cartões encostados lado a lado sob a mesma faixa: costura com cantos retos e uma só linha.
        "QWidget#panelCard[kaiJoinRight=\"true\"] { border-right: none;"
        " border-top-right-radius: 0px; border-bottom-right-radius: 0px; }\n"
        "QWidget#panelCard[kaiJoinLeft=\"true\"] { border-top-left-radius: 0px; border-bottom-left-radius: 0px; }\n"
        // Painéis que eram diálogos: dentro de um cartão, o fundo do diálogo some (o cartão é a moldura).
        "QDialog[kaiEmbedded=\"true\"] { background: transparent; border: none; border-radius: 0px; }\n")
        .arg(border).arg(rMd);

    // Cantos da JANELA: o contêiner raiz segue radiusMd (o mesmo da máscara em MainWindow::updateWindowShape); maximizada/tela cheia fica reto.
    qss += QStringLiteral(
        "QWidget#rootContainer { border-radius: %1px; }\n"
        "QWidget#rootContainer[kaiMaximized=\"true\"] { border-radius: 0px; }\n"
        // Janela translúcida: só o contêiner raiz pinta fundo (com os cantos
        // arredondados e suavizados); o QMainWindow por trás fica transparente.
        "QMainWindow[kaiTranslucent=\"true\"] { background: transparent; }\n"
        // A barra do topo encosta nos cantos de cima: sem isto o fundo quadrado
        // dela vazaria para fora do arco da janela.
        "QWidget#rootContainer TopUtilityBar { border-top-left-radius: %1px;"
        " border-top-right-radius: %1px; }\n"
        "QWidget#rootContainer[kaiMaximized=\"true\"] TopUtilityBar {"
        " border-top-left-radius: 0px; border-top-right-radius: 0px; }\n")
        .arg(rMd);

    // Cantos dos DIÁLOGOS: todo QDialog ganha a moldura do Kai (ver dialog-frame.h): fundo com o raio da janela principal
    // (borda e sombra interna vêm de DialogEdge); com cantos nativos do DWM (kaiFlat) fica reto. A barra de título é
    // transparente e arredonda os cantos de cima.
    qss += QStringLiteral(
        "QDialog[kaiFramed=\"true\"] { border-radius: %1px; }\n"
        "QDialog[kaiFramed=\"true\"][kaiFlat=\"true\"] { border-radius: 0px; }\n"
        "QWidget#kaiDialogTitleBar { background: transparent; border-top-left-radius: %1px;"
        " border-top-right-radius: %1px; }\n")
        .arg(rMd);

    // Handle do QSplitter: linha de 1px na cor de borda do tema (border-top se vertical, border-left se horizontal) sobre fundo transparente; o hover reforça com accent.
    qss += QStringLiteral(
        "QSplitter::handle { background: transparent; }\n"
        "QSplitter::handle:horizontal { border-left: 1px solid %1; }\n"
        "QSplitter::handle:vertical { border-top: 1px solid %1; }\n"
        "QSplitter::handle:horizontal:hover { border-left: 1px solid %2; }\n"
        "QSplitter::handle:vertical:hover { border-top: 1px solid %2; }\n")
        .arg(border, accent);

    // Divisores entre painéis com moldura própria ("outerSplitter", "actionsSplitter") não desenham nada (viravam borda dupla), mas continuam arrastáveis.
    for (const QString &name : {QStringLiteral("outerSplitter"), QStringLiteral("actionsSplitter")}) {
        qss += QStringLiteral(
            "QSplitter#%1::handle, QSplitter#%1::handle:hover,"
            " QSplitter#%1::handle:horizontal, QSplitter#%1::handle:vertical,"
            " QSplitter#%1::handle:horizontal:hover, QSplitter#%1::handle:vertical:hover {"
            " background: transparent; border: none; }\n").arg(name);
    }

    // Gradientes de tema em 3 bases reutilizáveis; cada área usa uma base e o tema decide com um par start/end:
    //   - "primary": chrome do app (janela, diálogos, header, árvore de comandos) e superfícies da Saída;
    //   - "secondary": reservada (campos de entrada não usam gradiente);
    //   - "tertiary": entalhes decorativos (logo e dica da boas-vindas); botões não usam gradiente.
    // Uma base só entra se o tema ativo declara o par (tk::hasGradient) e o interruptor mestre está ligado
    // (tk::setGradientsEnabled); senão a string vem vazia e vale o fundo sólido de theme.qss. "primary" no chrome usa os
    // seletores que pintavam o bg sólido em theme.qss; só background-color é substituída.
    if (tk::hasGradient(QStringLiteral("primary"))) {
        const QString primaryBg = tk::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary"));
        qss += QStringLiteral("QWidget#rootContainer { %1 }\n").arg(primaryBg);
        // Diálogos são janelas próprias, fora de #rootContainer: sem esta regra ficavam com o fundo sólido de QDialog.
        qss += QStringLiteral("QDialog { %1 }\n").arg(primaryBg);
        // TopUtilityBar: seletor por classe (Q_OBJECT expõe o className ao QSS).
        qss += QStringLiteral("TopUtilityBar { %1 }\n").arg(primaryBg);
        // Só as QTreeWidget internas de CommandTreeWidget (seletor descendente por classe). Com imagem de fundo,
        // CommandTreeWidget::applyBackgroundStyle aplica um setStyleSheet local que vence este.
        qss += QStringLiteral("CommandTreeWidget QTreeWidget { %1 }\n").arg(primaryBg);
    }
    // Fundo da árvore de comandos sempre transparente, em todos os estados: o Qt pinta hover/seleção por célula e, sem
    // regra para o estado normal, a 2ª coluna cai no fallback nativo (zebra da paleta) e reabre o vão ao passar o mouse.
    // Com tudo zerado, DraggableTreeWidget é o único dono do fundo (zebra, hover, seleção; ver setRowColors).
    // border/outline explícitos também: o item atual ganha do Fusion um contorno pontilhado próprio, mesmo sem hover.
    // border-radius: 0px repetido para a regra ser auto-contida (sem cantos redondos no hover/zebra).
    qss += QStringLiteral(
        "CommandTreeWidget QTreeWidget::item, CommandTreeWidget QTreeWidget::item:hover,"
        " CommandTreeWidget QTreeWidget::item:selected,"
        " CommandTreeWidget QTreeWidget::item:selected:active,"
        " CommandTreeWidget QTreeWidget::item:selected:!active,"
        " CommandTreeWidget QTreeWidget::item:focus {"
        " background-color: transparent; border: none; outline: none; border-radius: 0px; }\n");
    // Botões de confirmação (QDialogButtonBox) numa regra só: neutro e primário (kaiRole="primary" ou :default), cada um
    // com hover, pressed, foco e desabilitado. Cores sólidas; seletores mais específicos que os de QPushButton para vencer
    // regras anteriores.
    {
        const QString primary = tk::buttonColor();
        const QColor primaryColor(primary);
        const QString hoverNeutral = tk::selBg();
        const QString pressedNeutral = QColor(tk::selBg()).darker(112).name();
        // Ordem importa (mesma especificidade): foco < hover < pressed, para
        // o hover continuar visível no botão que está com foco.
        qss += QStringLiteral(
            "QDialogButtonBox QPushButton { min-width: 88px; padding: 7px 18px;"
            " border-radius: %1px; border: 1px solid %2; background-color: %3; color: %4;"
            " font-weight: 500; icon-size: 0px; }\n"
            "QDialogButtonBox QPushButton:focus { border: 1px solid %6; }\n"
            "QDialogButtonBox QPushButton:hover { background-color: %5; border: 1px solid %6; }\n"
            "QDialogButtonBox QPushButton:pressed { background-color: %7; border: 1px solid %6; }\n"
            "QDialogButtonBox QPushButton:disabled { background-color: %3; border: 1px solid %2; color: %8; }\n")
            .arg(tk::radiusMd())
            .arg(tk::borderColor(), tk::surface2(), tk::fg(), hoverNeutral, tk::accent(),
                 pressedNeutral, tk::mutedFg());
        const auto sel = [](const char *state) {
            const QString st = QString::fromLatin1(state);
            return QStringLiteral("QDialogButtonBox QPushButton[kaiRole=\"primary\"]%1,"
                                  " QDialogButtonBox QPushButton:default%1").arg(st);
        };
        qss += QStringLiteral(
            "%1 { background-color: %2; color: %3; border: 1px solid %2; font-weight: 600; }\n"
            "%4 { border: 1px solid %5; }\n"
            "%6 { background-color: %7; border: 1px solid %7; }\n"
            "%8 { background-color: %9; border: 1px solid %9; }\n"
            "%10 { background-color: %2; border: 1px solid %2; }\n")
            .arg(sel(""), primary, tk::buttonFg())
            .arg(sel(":focus"), tk::fg())
            .arg(sel(":hover"), primaryColor.lighter(112).name())
            .arg(sel(":pressed"), primaryColor.darker(112).name())
            .arg(sel(":disabled"));
    }
    // Anel de foco nos botões primários fora de QDialogButtonBox.
    qss += QStringLiteral("QPushButton[kaiRole=\"primary\"]:focus { border: 1px solid %1; }\n")
        .arg(tk::fg());
    // Campos de entrada (QLineEdit/QComboBox/QSpinBox/QPlainTextEdit...)
    // ficam SEMPRE com fundo sólido: o gradiente do tema vale só para fundos
    // (janela, diálogos, cards) e botões de ação, nunca para os campos.

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
        // A janela principal fica translúcida de propósito (cantos suaves, ver
        // MainWindow): não desliga o atributo nela.
        window->setAttribute(Qt::WA_TranslucentBackground,
                             window->property("kaiRoundedWindow").toBool());
        return;
    }

#if defined(Q_OS_WIN)
    // Windows 11 (build 22000+): backdrop nativo do sistema (o compositor faz o blur, custo ~zero para o app).
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
