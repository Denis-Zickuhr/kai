#include "utils/theme-manager.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStandardPaths>

#include "utils/logger.h"

namespace kai::utils {

namespace {
constexpr const char *kLogTag = "ThemeManager";

// Casa "${nome_var}" ou "${nome_var + #rrggbb}" / "${nome_var - #rrggbb}"
// (espaços opcionais em torno do operador,).
const QRegularExpression &expressionPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\$\{\s*([A-Za-z0-9_]+)\s*(?:([+-])\s*(#[0-9a-fA-F]{3,6})\s*)*\})"));
    return pattern;
}

// Casa cada operação individual "+ #hex" / "- #hex" dentro de uma expressão,
// permitindo múltiplas operações encadeadas (${fg - #044 + #400}).
const QRegularExpression &chainedOpPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(([+-])\s*(#[0-9a-fA-F]{3,6}))"));
    return pattern;
}
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_watcher(std::make_unique<QFileSystemWatcher>(this))
{
    QDir().mkpath(themesDirPath());
    connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, this, &ThemeManager::handleFileChanged);
}

ThemeManager::~ThemeManager() = default;

QString ThemeManager::themesDirPath() const
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(configDir).filePath(QStringLiteral("themes"));
}

bool ThemeManager::loadTheme(const QString &themeName)
{
    const QString filePath = QDir(themesDirPath()).filePath(themeName + QStringLiteral(".json"));
    return loadThemeFromFile(filePath);
}

bool ThemeManager::hasTheme() const
{
    return m_hasTheme;
}

const ResolvedTheme &ThemeManager::currentTheme() const
{
    return m_currentTheme;
}

void ThemeManager::setLiveReloadEnabled(bool enabled)
{
    m_liveReloadEnabled = enabled;
    if (!enabled && !m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    } else if (enabled && !m_currentFilePath.isEmpty() && !m_watcher->files().contains(m_currentFilePath)) {
        m_watcher->addPath(m_currentFilePath);
    }
}

bool ThemeManager::looksLikeHexColor(const QString &value)
{
    static const QRegularExpression hexPattern(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return hexPattern.match(value).hasMatch();
}

QString ThemeManager::applyColorOp(const QString &baseColor, QChar op, const QString &hexOperand)
{
    if (!looksLikeHexColor(baseColor) || !looksLikeHexColor(hexOperand)) {
        // Operando inválido: retorna a base inalterada em vez de crashar
        // (resiliência).
        return baseColor;
    }

    const int baseR = baseColor.mid(1, 2).toInt(nullptr, 16);
    const int baseG = baseColor.mid(3, 2).toInt(nullptr, 16);
    const int baseB = baseColor.mid(5, 2).toInt(nullptr, 16);

    const int opR = hexOperand.mid(1, 2).toInt(nullptr, 16);
    const int opG = hexOperand.mid(3, 2).toInt(nullptr, 16);
    const int opB = hexOperand.mid(5, 2).toInt(nullptr, 16);

    const int sign = (op == QLatin1Char('-')) ? -1 : 1;

    const int r = std::clamp(baseR + sign * opR, 0, 255);
    const int g = std::clamp(baseG + sign * opG, 0, 255);
    const int b = std::clamp(baseB + sign * opB, 0, 255);

    return QStringLiteral("#%1%2%3")
        .arg(r, 2, 16, QChar('0'))
        .arg(g, 2, 16, QChar('0'))
        .arg(b, 2, 16, QChar('0'));
}

QString ThemeManager::resolveExpressions(const QString &input, const QMap<QString, QString> &variables)
{
    QString result = input;

    // Reprocessa em múltiplas passadas para suportar expressões que
    // referenciam variáveis já resolvidas por outra expressão. Limite de 5
    // passadas evita loop infinito em referências circulares malformadas.
    for (int pass = 0; pass < 5; ++pass) {
        bool anyReplacement = false;
        QRegularExpressionMatchIterator it = expressionPattern().globalMatch(result);

        QVector<QPair<QString, QString>> replacements;
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString fullExpression = match.captured(0);
            const QString varName = match.captured(1);

            if (!variables.contains(varName)) {
                utils::Logger::warning(kLogTag,
                    QStringLiteral("Variável de tema '%1' não declarada. Expressão ignorada.").arg(varName));
                continue;
            }

            QString currentValue = variables.value(varName);

            // Aplica cada operação encadeada "+ #hex" / "- #hex" presente
            // na expressão completa, em ordem.
            QRegularExpressionMatchIterator opIt = chainedOpPattern().globalMatch(fullExpression);
            while (opIt.hasNext()) {
                const QRegularExpressionMatch opMatch = opIt.next();
                const QChar op = opMatch.captured(1).at(0);
                QString hexOperand = opMatch.captured(2);
                if (hexOperand.size() == 4) {
                    // Formato curto #rgb -> expande para #rrggbb.
                    hexOperand = QStringLiteral("#%1%1%2%2%3%3")
                        .arg(hexOperand.at(1)).arg(hexOperand.at(2)).arg(hexOperand.at(3));
                }
                currentValue = applyColorOp(currentValue, op, hexOperand);
            }

            replacements.append({fullExpression, currentValue});
        }

        for (const auto &pair : replacements) {
            if (result.contains(pair.first)) {
                result.replace(pair.first, pair.second);
                anyReplacement = true;
            }
        }

        if (!anyReplacement) {
            break;
        }
    }

    return result;
}

QString ThemeManager::buildQss(const QMap<QString, QString> &variables, const QMap<QString, QString> &components) const
{
    QString qss;

    // Regras base derivadas diretamente das variáveis (fallback para
    // componentes não declarados explicitamente,). A família
    // de fonte do tema é seguida por fallbacks genéricos conhecidos do
    // sistema (fontes feias: fontes declaradas no tema mas
    // ausentes no sistema do usuário caem silenciosamente no fallback
    // serifado do Qt; declarar alternativas aqui evita esse resultado).
    qss += QStringLiteral("QWidget { background-color: %1; color: %2; "
                          "font-family: '%3', 'Ubuntu', 'Noto Sans', 'DejaVu Sans', sans-serif; font-size: %4; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")),
             variables.value(QStringLiteral("fg"), QStringLiteral("#f8f8f2")),
             variables.value(QStringLiteral("font_family"), QStringLiteral("Ubuntu")),
             variables.value(QStringLiteral("font_size"), QStringLiteral("12pt")));

    // Campos de texto editáveis: fundo base (bg) para "afundar" dentro dos
    // blocos de seção (QGroupBox usa alt_bg, mais claro), criando contraste
    // e hierarquia visual clara (repaginação — feedback do usuário). Borda
    // sutil na cor de seleção só para delimitar o campo.
    qss += QStringLiteral("QLineEdit, QComboBox { "
                          "background-color: %1; border: 1px solid %2; border-radius: %3; padding: 7px 9px; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")),
             variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")),
             variables.value(QStringLiteral("input_radius"), QStringLiteral("8px")));

    qss += QStringLiteral("QTreeWidget, QListWidget, QTreeView, QListView, QTableWidget, QTableView, "
                          "QPlainTextEdit, QTextEdit, QTextBrowser, QListView { "
                          "background-color: %1; border: none; border-radius: 0px; padding: 2px; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")));

    // Garante SEM moldura em QUALQUER área rolável e seu frame (cobre
    // QScrollArea, QAbstractScrollArea e derivados que o Qt desenha com
    // frame nativo claro — a "borda branca" reportada em vários forms).
    qss += QStringLiteral(
        "QAbstractScrollArea { border: none; }\n"
        "QScrollArea { border: none; background-color: %1; }\n"
        "QFrame { border: none; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")));

    // QScrollArea e viewports sem moldura (estilo CopyQ): elimina qualquer
    // frame nativo claro em volta de áreas roláveis (diálogos, árvore).
    qss += QStringLiteral(
        "QScrollArea { border: none; background-color: %1; }\n"
        "QAbstractScrollArea { border: none; }\n"
        "QAbstractScrollArea::viewport { border: none; background-color: %1; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")));

    // Zebra sutil (alternate-background-color) estilo CopyQ: linhas
    // alternadas com um leve contraste sobre o fundo, facilitando a
    // leitura da lista sem molduras. Requer setAlternatingRowColors(true)
    // no QTreeWidget (feito em CommandTreeWidget::createTreeForRoot).
    qss += QStringLiteral(
        "QTreeWidget, QListWidget, QTreeView, QListView { "
        "alternate-background-color: %1; }\n")
        .arg(variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")));

    // Espaçamento e tipografia consolidados em um único bloco
    // ("ainda tá meio sem espaço os textos"). Regras
    // de QToolButton/QTabBar::tab/QHeaderView::section aparecem só uma vez
    // no arquivo inteiro para evitar que uma declaração posterior com a
    // mesma especificidade sobrescreva silenciosamente um valor já maior
    // definido aqui (bug de especificidade CSS que limitava o efeito de
    // ajustes anteriores). Valores generosos em toda a interface: itens de
    // árvore/lista com respiro vertical amplo, botões com altura mínima
    // maior, abas com peso visual forte, tabelas editáveis com header e
    // linhas maiores.
    qss += QStringLiteral(
        "QTreeWidget::item, QListWidget::item { padding: 6px 8px; min-height: 20px; }\n"
        "QTableWidget::item { padding: 5px; }\n"
        "QTableView, QTableWidget { gridline-color: %2; }\n"
        "QHeaderView::section { padding: 7px 8px; font-weight: 600; background-color: %2; "
        "color: %6; border: none; border-bottom: 2px solid %5; }\n"
        "QLineEdit { padding: 7px 9px; font-size: %1; }\n"
        "QPlainTextEdit { padding: 7px; }\n"
        "QPushButton, QToolButton { font-size: %1; min-height: 26px; min-width: 26px; padding: 6px 12px; "
        "background-color: transparent; border: 1px solid transparent; border-radius: %4; }\n"
        "QToolButton:hover, QPushButton:hover { background-color: %2; border: 1px solid %2; }\n"
        "QToolButton:checked, QToolButton:pressed, QPushButton:pressed { background-color: %5; }\n"
        // QTabBar (o CONTÊINER da barra, não os itens ::tab individuais) sem
        // regra própria cai no frame nativo do estilo Fusion — uma linha
        // clara/branca no topo da barra que destoa do tema escuro (bug
        // relatado: linha branca entre o cabeçalho do OutputPanel e a barra
        // de abas "Output" logo abaixo).
        "QTabBar { border: none; background: transparent; }\n"
        "QTabBar::tab { font-size: %1; font-weight: 600; min-width: 90px; padding: 8px 16px; "
        "border: none; background-color: %3; margin-right: 1px; }\n"
        // Aba selecionada estilo CopyQ: fundo mais claro (fundo base) com
        // uma barra de acento embaixo; não-selecionadas ficam num tom mais
        // escuro (alt_bg), criando a separação visual do CopyQ.
        "QTabBar::tab:selected { background-color: %7; border-bottom: 2px solid %5; }\n"
        "QTabBar::tab:!selected { color: %6; }\n"
        "QTabBar::tab:hover:!selected { background-color: %5; }\n"
        "QLabel { padding: 2px; background: transparent; border: none; }\n"
        "QComboBox { min-height: 24px; padding: 6px 10px; }\n"
        "QCheckBox { spacing: 8px; padding: 3px; }\n"
        "QDialog { padding: 4px; }\n"
        "QFormLayout, QVBoxLayout, QHBoxLayout { spacing: 8px; }\n"
        // Campos embutidos em células de tabela via setCellWidget. Bug real
        // corrigido aqui: "textos cortados dentro de tabelas". O
        // padding genérico de QLineEdit somado à altura de linha fixa da
        // tabela sobrava pouco espaço vertical; seletor mais específico
        // reduz o padding só dentro de QTableWidget.
        // Campos embutidos em células de tabela via setCellWidget: SEM
        // borda e ocupando a célula inteira, para o texto/valor não ser
        // cortado verticalmente (bug reportado: "texto cropado" nas
        // tabelas de variáveis). Fundo transparente para herdar a zebra
        // da linha; padding horizontal leve, vertical zero (a altura da
        // linha já dá o respiro).
        "QTableWidget QLineEdit { padding: 0px 6px; margin: 0px; min-height: 0px; "
        "border: none; background: transparent; }\n"
        "QTableWidget QComboBox { padding: 0px 6px; margin: 0px; min-height: 0px; border: none; }\n"
        // Contraste de foco/seleção em campos DENTRO de tabelas (feedback do
        // usuário: ao focar/selecionar tudo o contraste ficava ruim). Ao
        // focar, o campo embutido ganha um fundo sólido (bg) e uma seleção
        // de texto com o acento, garantindo legibilidade sobre a zebra.
        "QTableWidget QLineEdit:focus { background: %8; selection-background-color: %5; "
        "selection-color: %6; border: 1px solid %5; border-radius: 4px; }\n")
        .arg(variables.value(QStringLiteral("font_size"), QStringLiteral("12pt")),
             variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")),
             variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")),
             variables.value(QStringLiteral("border_radius"), QStringLiteral("8px")),
             variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")))
        .arg(variables.value(QStringLiteral("alt_fg"), QStringLiteral("#d6acff")))
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")))
        .arg(variables.value(QStringLiteral("input_focus_bg"), QStringLiteral("#2b2d3a")));

    // Altura de linha maior em tabelas editáveis (item
    // reportado: KeyValueEditorWidget/ParameterEditorWidget com pouco
    // espaço). QTableWidget não expõe uma propriedade QSS direta de
    // row-height; a altura real da linha é ajustada programaticamente via
    // QHeaderView::setDefaultSectionSize no setupUi de cada editor.

    // Visibilidade de foco de teclado em toda a interface
    // (feedback do usuário: "melhorar a visibilidade do foco atual em
    // todo o sistema" + "borda muito dura, suavizar"). Foco de 1px na cor
    // de acento (em vez de 2px), evitando o efeito de "borda dura"
    // reportado, mas mantendo o campo/controle focado claramente
    // destacado. Widgets de container (árvore/lista) recebem o mesmo
    // tratamento sutil.
    qss += QStringLiteral(
        "QLineEdit:focus, QComboBox:focus, QTableWidget:focus { "
        "border: 1px solid %1; }\n"
        "QToolButton:focus, QPushButton:focus { border: 1px solid %1; }\n")
        .arg(variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")));

    // Widgets adicionais (QCheckBox, QTabWidget, QDialog, QScrollBar) que,
    // sem regra própria, caem no fallback nativo do Qt — geralmente com
    // bordas brancas/cinza claras que destoam de temas escuros (bug
    // reportado: "bordas brancas feias").
    qss += QStringLiteral(
        "QMainWindow { background-color: %1; }\n"
        "QWidget#rootContainer { background-color: %1; border-radius: 8px; border: 1px solid %1; }\n"
        // Diálogos com a decoração NATIVA do sistema (barra de título +
        // moldura do WM), garantindo drag/resize/fechar funcionais e uma
        // borda visível fornecida pelo próprio window manager. O conteúdo
        // interno já é escuro e sem bordas brancas (regras acima). Só
        // definimos o fundo escuro do tema aqui.
        "QDialog { background-color: %1; }\n"
        "QMessageBox { background-color: %1; }\n"
        "QMainWindow::separator { background-color: %1; width: 0px; height: 0px; }\n"
        // QSplitter handle: causa raiz da "borda branca" reportada 10x —
        // sem regra própria, o Qt desenha a divisória nativa clara entre
        // os painéis (árvore/sidebar e navegação/saída). Pintamos o handle
        // com a própria cor de fundo (invisível em repouso) e só um leve
        // realce na cor de seleção ao passar o mouse, eliminando a linha
        // branca dura sem perder a área arrastável.
        "QSplitter::handle { background-color: %1; }\n"
        "QSplitter::handle:horizontal { width: 6px; }\n"
        "QSplitter::handle:vertical { height: 6px; }\n"
        "QSplitter::handle:hover { background-color: %2; }\n"
        // Pane das abas sem borda dura: só o fundo, deixando o conteúdo
        // fluir sem a moldura clara de 1px que destoava do tema escuro.
        "QTabWidget::pane { border: none; border-top: 1px solid %2; }\n"
        // (Checkbox NÃO é estilizada aqui: o estilo único vem do
        // app-stylesheet global — caixa transparente + tick do qrc. Este
        // bloco antigo pintava um fundo cheio de accent SEM o check, causando
        // o "fundo feio" relatado. Removido para não sobrescrever o global.)
        "QScrollBar:vertical, QScrollBar:horizontal { background-color: %1; border: none; }\n"
        "QScrollBar::handle { background-color: %2; border-radius: %3; }\n"
        "QToolBar { border: none; spacing: 8px; padding: 6px; }\n"
        "QMenuBar { border: none; background-color: %1; padding: 2px; }\n"
        "QMenuBar::item { padding: 6px 12px; background-color: transparent; border-radius: %3; }\n"
        "QMenuBar::item:selected { background-color: %2; }\n"
        "QMenuBar::item:pressed { background-color: %2; }\n"
        "QMenu { padding: 4px; border: 1px solid %1; }\n"
        "QMenu::item { padding: 7px 18px; border-radius: %3; }\n"
        "QMenu::item:selected { background-color: %2; }\n"
        // Seções de formulário como BLOCOS com contraste (repaginação —
        // feedback do usuário: melhorar contraste entre blocos e padding).
        // Fundo alternativo (alt_bg) levemente destacado do fundo base,
        // cantos arredondados, título em destaque na cor de acento e
        // padding interno generoso. Sem borda dura — o contraste de fundo
        // já separa os blocos visualmente.
        "QGroupBox { background-color: %1; border: none; border-radius: 8px; "
        "margin-top: 14px; padding: 14px 12px 12px 12px; font-weight: 600; }\n"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; "
        "left: 12px; top: 2px; padding: 0px 4px; color: %4; font-size: 10pt; }\n"
        // Botões de diálogo (OK/Cancel): SEM ícones nativos (o estilo do
        // Qt injeta um check/► verde e um X que destoavam do app — bug
        // reportado: "ícones feios e fora de padrão"). Os ícones são
        // removidos programaticamente (stripDialogButtonIcons), e aqui só
        // estilizamos: cantos arredondados, sem borda dura (borda na cor do
        // próprio fundo do botão = invisível), hover na cor de acento; o
        // botão default/primário (OK) recebe fundo de acento com texto
        // contrastante e negrito, destacando a ação primária.
        "QDialogButtonBox QPushButton { min-width: 88px; padding: 7px 18px; border-radius: %3; "
        "border: 1px solid %2; background-color: %2; icon-size: 0px; }\n"
        "QDialogButtonBox QPushButton:hover { border: 1px solid %4; background-color: %4; color: %1; }\n"
        "QDialogButtonBox QPushButton:default { background-color: %4; color: %1; border: 1px solid %4; "
        "font-weight: 600; }\n"
        "QDialogButtonBox QPushButton:default:hover { background-color: %4; }\n")
        .arg(variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")),
             variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")),
             variables.value(QStringLiteral("border_radius"), QStringLiteral("4px")),
             variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")));

    // Redesign visual estilo CopyQ (polimento final): itens de
    // árvore com hover sutil e item selecionado com borda de destaque na
    // cor de acento (não só background), scrollbar mais fina e discreta,
    // e cantos consistentemente arredondados em toda a interface.
    qss += QStringLiteral(
        // Zebra sutil (alternate row) e seleção AZUL SÓLIDA sem borda,
        // estilo CopyQ: a linha selecionada é totalmente preenchida pela
        // cor de seleção do tema, sem contorno duro; hover apenas escurece
        // levemente. Sem bordas em nenhum estado de item.
        // Zebra sutil (alternate row) e seleção AZUL SÓLIDA sem borda,
        // estilo CopyQ: a linha selecionada é totalmente preenchida pela
        // cor de seleção do tema, sem contorno duro; hover apenas escurece
        // levemente. Sem bordas em nenhum estado de item.
        // %1=hover_bg  %2=selection_bg(accent)  %3=selection_fg(bg)
        // %4=accent(scrollbar hover)  %5=combo_popup_bg(alt_bg)
        "QTreeWidget::item, QListWidget::item, QTreeView::item, QListView::item { "
        "border: none; }\n"
        "QTreeWidget::item:hover, QListWidget::item:hover, QTreeView::item:hover { "
        "background-color: %1; }\n"
        "QTreeWidget::item:selected, QListWidget::item:selected, QTreeView::item:selected, "
        "QListView::item:selected, QTreeWidget::item:selected:active, QListWidget::item:selected:active { "
        "background-color: %2; color: %3; border: none; }\n"
        "QScrollBar:vertical { width: 10px; margin: 2px; }\n"
        "QScrollBar:horizontal { height: 10px; margin: 2px; }\n"
        "QScrollBar::handle:hover { background-color: %4; }\n"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0px; width: 0px; border: none; }\n"
        "QComboBox::drop-down { border: none; width: 22px; }\n"
        // Chevron (seta pra baixo) indicando que é um dropdown (feedback do
        // usuário). Usa um SVG com cor explícita (QSS não resolve
        // currentColor dos ícones Lucide padrão).
        "QComboBox::down-arrow { image: url(:/icons/lucide/chevron-down-fg.svg); width: 14px; height: 14px; }\n"
        "QComboBox QAbstractItemView { background-color: %5; border: 1px solid %5; "
        "border-radius: %6; selection-background-color: %2; outline: none; }\n"
        // Remove o retângulo pontilhado de foco (dotted focus rect) que o
        // Qt desenha em itens de árvore/lista/tabela e parece uma borda
        // tênue clara.
        "QAbstractItemView { outline: none; }\n")
        .arg(variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")) + QStringLiteral("55"),
             variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")),
             variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")),
             variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")),
             variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")),
             variables.value(QStringLiteral("border_radius"), QStringLiteral("4px")));

    static const QMap<QString, QString> selectorFor = {
        {QStringLiteral("main_window"), QStringLiteral("QMainWindow")},
        {QStringLiteral("menu_bar"), QStringLiteral("QMenuBar")},
        {QStringLiteral("menu"), QStringLiteral("QMenu")},
        {QStringLiteral("tool_bar"), QStringLiteral("QToolBar")},
        {QStringLiteral("tool_button"), QStringLiteral("QToolButton")},
        {QStringLiteral("tool_button_pressed"), QStringLiteral("QToolButton:pressed")},
        {QStringLiteral("tool_button_selected"), QStringLiteral("QToolButton:checked")},
        {QStringLiteral("tab_bar"), QStringLiteral("QTabBar")},
        {QStringLiteral("tab_selected"), QStringLiteral("QTabBar::tab:selected")},
        {QStringLiteral("tab_unselected"), QStringLiteral("QTabBar::tab:!selected")},
        {QStringLiteral("search_bar"), QStringLiteral("QLineEdit")},
        {QStringLiteral("search_bar_focused"), QStringLiteral("QLineEdit:focus")},
        {QStringLiteral("item_selected"), QStringLiteral("QTreeWidget::item:selected")},
        {QStringLiteral("notification"), QStringLiteral("QLabel#notification")},
        {QStringLiteral("tooltip"), QStringLiteral("QToolTip")},
    };

    for (auto it = components.constBegin(); it != components.constEnd(); ++it) {
        const QString selector = selectorFor.value(it.key());
        if (selector.isEmpty()) {
            continue; // componente desconhecido, ignorado silenciosamente
        }
        qss += QStringLiteral("%1 { %2 }\n").arg(selector, it.value());
    }

    // ---- Modernização dos inputs (feedback do usuário: "modernize TODOS
    // os inputs; mais espaço, fonte maior, mais bonito") ----
    // Inserido POR ÚLTIMO para vencer as regras anteriores e as do tema
    // (mesma especificidade => a última vence). Foco em: campos mais
    // altos e espaçosos, cantos arredondados, e — crucial — o POPUP do
    // QComboBox e do QCompleter (a QAbstractItemView do dropdown) que sem
    // regra própria cai no fallback nativo branco com texto desalinhado
    // (bug reportado: "combo de busca com fundo branco e texto fora").
    {
        const QString bg = variables.value(QStringLiteral("bg"), QStringLiteral("#282a36"));
        const QString altBg = variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c"));
        const QString fg = variables.value(QStringLiteral("fg"), QStringLiteral("#f8f8f2"));
        const QString selBg = variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a"));
        const QString selFg = variables.value(QStringLiteral("sel_fg"), QStringLiteral("#f8f8f2"));
        const QString accent = variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9"));
        const QString radius = variables.value(QStringLiteral("border_radius"), QStringLiteral("8px"));

        qss += QStringLiteral(
            // Campos e botões mais altos, espaçosos e arredondados.
            "QLineEdit, QComboBox { min-height: 30px; padding: 9px 12px; border-radius: 8px; "
            "border: 1px solid %4; background-color: %1; selection-background-color: %6; selection-color: %7; }\n"
            "QLineEdit:hover, QComboBox:hover { border: 1px solid %5; }\n"
            "QLineEdit:focus, QComboBox:focus { border: 1px solid %5; }\n"
            "QPlainTextEdit, QTextEdit { border-radius: 8px; padding: 10px; border: 1px solid %4; background-color: %1; }\n"
            "QPushButton { min-height: 30px; padding: 9px 18px; border-radius: 8px; "
            "background-color: %2; border: 1px solid %4; font-weight: 600; }\n"
            "QPushButton:hover { background-color: %4; border: 1px solid %5; }\n"
            "QPushButton:pressed { background-color: %5; }\n"
            "QPushButton:default { background-color: %5; color: %7; border: 1px solid %5; }\n"
            "QToolButton { min-height: 30px; padding: 7px; border-radius: 8px; }\n"
            "QToolButton:hover { background-color: %4; }\n"
            // Seta do combo com área confortável.
            "QComboBox::drop-down { border: none; width: 26px; }\n"
            // POPUP do QComboBox: fundo do tema, item alto, selecionado com
            // acento — elimina o fundo branco nativo e alinha o texto.
            "QComboBox QAbstractItemView { background-color: %2; color: %3; "
            "border: 1px solid %5; border-radius: 8px; padding: 4px; outline: none; "
            "selection-background-color: %6; selection-color: %7; }\n"
            "QComboBox QAbstractItemView::item { min-height: 28px; padding: 6px 10px; border-radius: 6px; }\n"
            // POPUP do QCompleter (busca): é uma QListView flutuante; sem
            // esta regra vem branca com o texto colado na borda.
            "QCompleter QAbstractItemView, QListView#completerPopup { background-color: %2; color: %3; "
            "border: 1px solid %5; border-radius: 8px; padding: 4px; outline: none; "
            "selection-background-color: %6; selection-color: %7; }\n"
            // Espaçamento geral um pouco mais generoso.
            "QCheckBox { spacing: 10px; padding: 5px; }\n"
            "QComboBox QAbstractItemView { font-size: %8; }\n")
            .arg(bg, altBg, fg, selBg, accent, selBg)   // %1..%6 (obs: %6 selBg p/ selection-bg)
            .arg(selFg)                                  // %7
            .arg(variables.value(QStringLiteral("font_size"), QStringLiteral("12pt"))); // %8
        Q_UNUSED(radius);
    }

    return qss;
}

bool ThemeManager::loadThemeFromFile(const QString &filePath)
{
    QFile file(filePath);

    if (!file.exists()) {
        const QString error = QStringLiteral("Arquivo de tema não encontrado: '%1'.").arg(filePath);
        utils::Logger::error(kLogTag, error);
        emit themeLoadFailed(filePath, error);
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        const QString error = QStringLiteral("Não foi possível abrir '%1'.").arg(filePath);
        utils::Logger::error(kLogTag, error);
        emit themeLoadFailed(filePath, error);
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Tema inválido: mantém o tema anterior carregado (/
        // nunca crasha, nunca perde o estado visual anterior).
        const QString error = QStringLiteral("Tema '%1' inválido: %2").arg(filePath, parseError.errorString());
        utils::Logger::error(kLogTag, error);
        emit themeLoadFailed(filePath, error);
        return false;
    }

    const QJsonObject root = doc.object();

    QMap<QString, QString> rawVariables;
    const QJsonObject variablesObj = root.value("variables").toObject();
    for (auto it = variablesObj.constBegin(); it != variablesObj.constEnd(); ++it) {
        rawVariables[it.key()] = it.value().toString();
    }

    // Resolve expressões dentro das próprias variáveis primeiro (uma
    // variável pode referenciar outra: "hover_bg": "${bg - #111111}").
    QMap<QString, QString> resolvedVariables = rawVariables;
    for (auto it = rawVariables.constBegin(); it != rawVariables.constEnd(); ++it) {
        resolvedVariables[it.key()] = resolveExpressions(it.value(), resolvedVariables);
    }

    QMap<QString, QString> resolvedComponents;
    const QJsonObject componentsObj = root.value("components").toObject();
    for (auto it = componentsObj.constBegin(); it != componentsObj.constEnd(); ++it) {
        resolvedComponents[it.key()] = resolveExpressions(it.value().toString(), resolvedVariables);
    }

    ResolvedTheme theme;
    theme.name = root.value("name").toString(QFileInfo(filePath).baseName());
    theme.variables = resolvedVariables;
    theme.qss = buildQss(resolvedVariables, resolvedComponents);

    m_currentTheme = theme;
    m_hasTheme = true;

    if (!m_currentFilePath.isEmpty() && m_watcher->files().contains(m_currentFilePath)) {
        m_watcher->removePath(m_currentFilePath);
    }
    m_currentFilePath = filePath;
    if (m_liveReloadEnabled) {
        m_watcher->addPath(filePath);
    }

    utils::Logger::info(kLogTag, QStringLiteral("Tema '%1' carregado de '%2'.").arg(theme.name, filePath));
    emit themeReloaded(theme);
    return true;
}

void ThemeManager::handleFileChanged(const QString &path)
{
    utils::Logger::info(kLogTag, QStringLiteral("Alteração detectada em '%1', recarregando tema (live reload).").arg(path));

    // QFileSystemWatcher pode remover o path do watch após certas operações
    // de escrita (ex: editores que salvam via rename); reagendamos o watch
    // se o arquivo ainda existir, mesmo que o parsing falhe.
    if (!loadThemeFromFile(path)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Live reload falhou para '%1'; mantendo tema anterior.").arg(path));
    }

    if (!m_watcher->files().contains(path) && QFile::exists(path)) {
        m_watcher->addPath(path);
    }
}

} // namespace kai::utils
