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

// Casa "${nome_var}" ou "${nome_var + #rrggbb}" / "${nome_var - #rrggbb}" (espaços opcionais em torno do operador).
const QRegularExpression &expressionPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\$\{\s*([A-Za-z0-9_]+)\s*(?:([+-])\s*(#[0-9a-fA-F]{3,6})\s*)*\})"));
    return pattern;
}

// Casa cada operação "+ #hex" / "- #hex" de uma expressão (encadeáveis: ${fg - #044 + #400}).
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
        // Operando inválido: devolve a base inalterada.
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

    // Várias passadas, para expressões que referenciam variáveis resolvidas por outra expressão; o limite de 5 evita loop em referências circulares.
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

            QRegularExpressionMatchIterator opIt = chainedOpPattern().globalMatch(fullExpression);
            while (opIt.hasNext()) {
                const QRegularExpressionMatch opMatch = opIt.next();
                const QChar op = opMatch.captured(1).at(0);
                QString hexOperand = opMatch.captured(2);
                if (hexOperand.size() == 4) {
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

QString ThemeManager::buildQss(const QMap<QString, QString> &themeVariables, const QMap<QString, QString> &components) const
{
    // Os cantos seguem a preferência do usuário (Configurações → Aparência → Cantos), não o que o arquivo do tema declara:
    // este módulo não conhece os tokens (é do núcleo, sem widgets), então as regras base saem com `@RADIUS_MD@` e
    // `@RADIUS_SM@` e a interface os troca pelo raio escolhido ao aplicar (ver themeStyleSheet em ui/app-stylesheet.h).
    QMap<QString, QString> variables = themeVariables;
    variables.insert(QStringLiteral("border_radius"), QStringLiteral("@RADIUS_MD@"));
    variables.insert(QStringLiteral("input_radius"), QStringLiteral("@RADIUS_MD@"));

    QString qss;

    // Regras base derivadas das variáveis. A família de fonte do tema vem seguida de fallbacks genéricos: uma família declarada mas ausente no sistema cairia no fallback serifado do Qt.
    qss += QStringLiteral("QWidget { background-color: %1; color: %2; "
                          "font-family: '%3', 'Ubuntu', 'Noto Sans', 'DejaVu Sans', sans-serif; font-size: %4; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")),
             variables.value(QStringLiteral("fg"), QStringLiteral("#f8f8f2")),
             variables.value(QStringLiteral("font_family"), QStringLiteral("Ubuntu")),
             variables.value(QStringLiteral("font_size"), QStringLiteral("12pt")));

    // Campos de texto com fundo base (bg), afundados nos blocos de seção (QGroupBox usa alt_bg); borda sutil na cor de seleção.
    qss += QStringLiteral("QLineEdit, QComboBox { "
                          "background-color: %1; border: 1px solid %2; border-radius: %3; padding: 7px 9px; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")),
             variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")),
             variables.value(QStringLiteral("input_radius"), QStringLiteral("8px")));

    qss += QStringLiteral("QTreeWidget, QListWidget, QTreeView, QListView, QTableWidget, QTableView, "
                          "QPlainTextEdit, QTextEdit, QTextBrowser, QListView { "
                          "background-color: %1; border: none; border-radius: 0px; padding: 2px; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")));

    // Sem moldura em qualquer área rolável (QScrollArea, QAbstractScrollArea e derivados): evita o frame nativo claro.
    qss += QStringLiteral(
        "QAbstractScrollArea { border: none; }\n"
        "QScrollArea { border: none; background-color: %1; }\n"
        "QFrame { border: none; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")));

    qss += QStringLiteral(
        "QScrollArea { border: none; background-color: %1; }\n"
        "QAbstractScrollArea { border: none; }\n"
        "QAbstractScrollArea::viewport { border: none; background-color: %1; }\n")
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")));

    // Zebra sutil (alternate-background-color) estilo CopyQ; requer setAlternatingRowColors(true) no QTreeWidget (CommandTreeWidget::createTreeForRoot).
    qss += QStringLiteral(
        "QTreeWidget, QListWidget, QTreeView, QListView, QTableWidget, QTableView { "
        "alternate-background-color: %1; background-color: %2; }\n")
        .arg(variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")),
             variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")));

    // Espaçamento e tipografia num bloco só: as regras de QToolButton/QTabBar::tab/QHeaderView::section aparecem uma vez no arquivo, para uma declaração posterior de mesma especificidade não sobrescrever um valor maior.
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
        // O contêiner QTabBar sem regra própria cai no frame nativo do Fusion: uma linha clara no topo da barra.
        "QTabBar { border: none; background: transparent; }\n"
        "QTabBar::tab { font-size: %1; font-weight: 600; min-width: 90px; padding: 8px 16px; "
        "border: none; background-color: %3; margin-right: 1px; }\n"
        // Aba selecionada estilo CopyQ: fundo base com barra de acento embaixo; as demais em alt_bg.
        "QTabBar::tab:selected { background-color: %7; border-bottom: 2px solid %5; }\n"
        "QTabBar::tab:!selected { color: %6; }\n"
        // Hover usa %2 (sel_bg), não %5 (accent): os ícones das abas são recoloridos com accent_color e um hover na mesma cor os deixava invisíveis.
        "QTabBar::tab:hover:!selected { background-color: %2; }\n"
        "QLabel { padding: 2px; background: transparent; border: none; }\n"
        "QComboBox { min-height: 24px; padding: 6px 10px; }\n"
        "QCheckBox { spacing: 8px; padding: 3px; }\n"
        "QDialog { padding: 4px; }\n"
        "QFormLayout, QVBoxLayout, QHBoxLayout { spacing: 8px; }\n"
        // Campos embutidos em células (setCellWidget): sem borda, ocupando a célula inteira, fundo transparente para herdar a zebra; padding vertical zero (a altura da linha dá o respiro) para o texto não cortar.
        "QTableWidget QLineEdit { padding: 0px 6px; margin: 0px; min-height: 0px; "
        "border: none; background: transparent; }\n"
        "QTableWidget QComboBox { padding: 0px 6px; margin: 0px; min-height: 0px; border: none; }\n"
        // Foco em campo dentro de tabela: fundo sólido (bg) e seleção de texto no acento, para legibilidade sobre a zebra.
        "QTableWidget QLineEdit:focus { background: %8; selection-background-color: %5; "
        "selection-color: %6; border: 1px solid %5; border-radius: @RADIUS_SM@; }\n")
        .arg(variables.value(QStringLiteral("font_size"), QStringLiteral("12pt")),
             variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")),
             variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")),
             variables.value(QStringLiteral("border_radius"), QStringLiteral("8px")),
             variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")))
        .arg(variables.value(QStringLiteral("alt_fg"), QStringLiteral("#d6acff")))
        .arg(variables.value(QStringLiteral("bg"), QStringLiteral("#282a36")))
        .arg(variables.value(QStringLiteral("input_focus_bg"), QStringLiteral("#2b2d3a")));

    // Foco de teclado: 1px na cor de acento (2px parecia borda dura); containers (árvore/lista) recebem o mesmo tratamento sutil.
    qss += QStringLiteral(
        "QLineEdit:focus, QComboBox:focus, QTableWidget:focus { "
        "border: 1px solid %1; }\n"
        "QToolButton:focus, QPushButton:focus { border: 1px solid %1; }\n")
        .arg(variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")));

    // QTabWidget, QDialog e QScrollBar sem regra própria caem no fallback nativo claro, que destoa de temas escuros.
    qss += QStringLiteral(
        "QMainWindow { background-color: %1; }\n"
        "QWidget#rootContainer { background-color: %1; border-radius: @RADIUS_MD@; border: 1px solid %1; }\n"
        // Diálogos usam a moldura própria do Kai (ui/shared/dialog-frame.h; borda/raio em app-stylesheet.cpp): aqui só o fundo.
        "QDialog { background-color: %1; }\n"
        "QMessageBox { background-color: %1; }\n"
        "QMainWindow::separator { background-color: %1; width: 0px; height: 0px; }\n"
        // QSplitter handle: sem regra, o Qt desenha a divisória nativa clara; pinta com o fundo (invisível em repouso) e realce na cor de seleção no hover, mantendo a área arrastável.
        "QSplitter::handle { background-color: %1; }\n"
        "QSplitter::handle:horizontal { width: 6px; }\n"
        "QSplitter::handle:vertical { height: 6px; }\n"
        "QSplitter::handle:hover { background-color: %2; }\n"
        // Pane das abas sem borda dura (só o fundo).
        "QTabWidget::pane { border: none; border-top: 1px solid %2; }\n"
        // QCheckBox não é estilizada aqui: o estilo único vem do app-stylesheet global.
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
        // Seções de formulário como blocos: alt_bg destacado do fundo, cantos arredondados, título na cor de acento, padding generoso e sem borda dura.
        "QGroupBox { background-color: %1; border: none; border-radius: @RADIUS_MD@; "
        "margin-top: 14px; padding: 14px 12px 12px 12px; font-weight: 600; }\n"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; "
        "left: 12px; top: 2px; padding: 0px 4px; color: %4; font-size: 10pt; }\n")
        // Botões de diálogo (QDialogButtonBox): em buildModernStylesheet() (app-stylesheet.cpp).
        .arg(variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c")),
             variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a")),
             variables.value(QStringLiteral("border_radius"), QStringLiteral("4px")),
             variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9")));

    // Polimento estilo CopyQ: hover sutil nos itens, selecionado preenchido na cor de seleção, scrollbar fina e cantos arredondados.
    qss += QStringLiteral(
        // Zebra sutil (alternate row) e seleção sólida sem borda (estilo CopyQ); hover só escurece levemente.
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
        // Chevron do dropdown: SVG com cor explícita (o QSS não resolve currentColor dos ícones Lucide).
        "QComboBox::down-arrow { image: url(:/icons/lucide/chevron-down-fg.svg); width: 14px; height: 14px; }\n"
        "QComboBox QAbstractItemView { background-color: %5; border: 1px solid %5; "
        "border-radius: %6; selection-background-color: %2; outline: none; }\n"
        // Remove o retângulo pontilhado de foco que o Qt desenha em itens de árvore/lista/tabela.
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
            continue;
        }
        qss += QStringLiteral("%1 { %2 }\n").arg(selector, it.value());
    }

    // Modernização dos inputs, inserida por último para vencer as regras anteriores e as do tema (mesma especificidade: a última vence). Inclui o popup do QComboBox/QCompleter, que sem regra cai no fallback nativo branco.
    {
        const QString bg = variables.value(QStringLiteral("bg"), QStringLiteral("#282a36"));
        const QString altBg = variables.value(QStringLiteral("alt_bg"), QStringLiteral("#21222c"));
        const QString fg = variables.value(QStringLiteral("fg"), QStringLiteral("#f8f8f2"));
        const QString selBg = variables.value(QStringLiteral("sel_bg"), QStringLiteral("#44475a"));
        const QString selFg = variables.value(QStringLiteral("sel_fg"), QStringLiteral("#f8f8f2"));
        const QString accent = variables.value(QStringLiteral("accent_color"), QStringLiteral("#bd93f9"));
        const QString radius = variables.value(QStringLiteral("border_radius"), QStringLiteral("8px"));

        qss += QStringLiteral(
            "QLineEdit, QComboBox { min-height: 30px; padding: 9px 12px; border-radius: @RADIUS_MD@; "
            "border: 1px solid %4; background-color: %1; selection-background-color: %6; selection-color: %7; }\n"
            "QLineEdit:hover, QComboBox:hover { border: 1px solid %5; }\n"
            "QLineEdit:focus, QComboBox:focus { border: 1px solid %5; }\n"
            "QPlainTextEdit, QTextEdit { border-radius: @RADIUS_MD@; padding: 10px; border: 1px solid %4; background-color: %1; }\n"
            "QPushButton { min-height: 30px; padding: 9px 18px; border-radius: @RADIUS_MD@; "
            "background-color: %2; border: 1px solid %4; font-weight: 600; }\n"
            "QPushButton:hover { background-color: %4; border: 1px solid %5; }\n"
            "QPushButton:pressed { background-color: %5; }\n"
            "QPushButton:default { background-color: %5; color: %7; border: 1px solid %5; }\n"
            "QToolButton { min-height: 30px; padding: 7px; border-radius: @RADIUS_MD@; }\n"
            "QToolButton:hover { background-color: %4; }\n"
            "QComboBox::drop-down { border: none; width: 26px; }\n"
            // Popup do QComboBox: fundo do tema, item alto, selecionado com acento (elimina o fundo branco nativo).
            "QComboBox QAbstractItemView { background-color: %2; color: %3; "
            "border: 1px solid %5; border-radius: @RADIUS_MD@; padding: 4px; outline: none; "
            "selection-background-color: %6; selection-color: %7; }\n"
            "QComboBox QAbstractItemView::item { min-height: 28px; padding: 6px 10px; border-radius: @RADIUS_SM@; }\n"
            // Popup do QCompleter (QListView flutuante): sem a regra vem branco, com o texto colado na borda.
            "QCompleter QAbstractItemView, QListView#completerPopup { background-color: %2; color: %3; "
            "border: 1px solid %5; border-radius: @RADIUS_MD@; padding: 4px; outline: none; "
            "selection-background-color: %6; selection-color: %7; }\n"
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
        // Tema inválido: mantém o tema anterior carregado.
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

    // O QFileSystemWatcher pode remover o path após escritas via rename (editores): reagenda o watch se o arquivo ainda existir, mesmo que o parse falhe.
    if (!loadThemeFromFile(path)) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Live reload falhou para '%1'; mantendo tema anterior.").arg(path));
    }

    if (!m_watcher->files().contains(path) && QFile::exists(path)) {
        m_watcher->addPath(path);
    }
}

} // namespace kai::utils
