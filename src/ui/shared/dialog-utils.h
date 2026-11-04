#pragma once

#include <QDialogButtonBox>
#include <QEvent>
#include <QPointer>
#include <QTimer>
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
#include <QSizePolicy>
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QString>
#include <QMessageBox>
#include <QVector>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <functional>
#include <algorithm>

#include "utils/translation-manager.h"
#include "utils/design-tokens.h"
#include "utils/action-shortcuts.h"
#include "core/config-manager.h"
#include "core/models.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/token-style.h"

namespace kai::ui {

// Botão de tamanho confortável (28px): padding generoso, ícone de 16px, raio pelo token. Sem texto vira quadrado só de ícone.
inline void makeCompactButton(QAbstractButton *button, int height = 28)
{
    button->setFixedHeight(height);
    button->setCursor(Qt::PointingHandCursor);
    if (button->text().isEmpty()) {
        button->setFixedWidth(height);
    }
    button->setIconSize(QSize(16, 16));
    const QString selector = qobject_cast<QPushButton *>(button) ? QStringLiteral("QPushButton")
                                                                  : QString::fromLatin1(button->metaObject()->className());
    const int padding = button->text().isEmpty() ? 0 : utils::tokens::space(3);
    bindFieldStyle(button, [selector, padding](int radius) {
        return QStringLiteral("%1 { padding: 0px %2px; min-height: 0px; border-radius: %3px; }").arg(selector).arg(padding).arg(radius);
    });
}

// Caminho completo (raiz -> pasta), um nome por segmento. O limite de profundidade protege contra ciclos
// em dados corrompidos.
inline QStringList folderPathSegments(const QVector<core::Folder> &allFolders, const QString &folderId)
{
    QStringList segments;
    QString currentId = folderId;
    for (int guard = 0; guard < 64 && !currentId.isEmpty(); ++guard) {
        const core::Folder *current = nullptr;
        for (const core::Folder &f : allFolders) {
            if (f.id == currentId) {
                current = &f;
                break;
            }
        }
        if (!current) {
            break;
        }
        segments.prepend(current->name);
        currentId = current->parentId.value_or(QString());
    }
    return segments;
}

// Item de combo: indentação por profundidade + nome + caminho completo entre parênteses (só com ancestrais).
inline QString folderComboLabel(const QVector<core::Folder> &allFolders, const QString &folderId)
{
    const QStringList segments = folderPathSegments(allFolders, folderId);
    if (segments.isEmpty()) {
        return QString();
    }
    const QString indent = QStringLiteral("    ").repeated(segments.size() - 1);
    const QString name = segments.last();
    if (segments.size() == 1) {
        return indent + name;
    }
    return QStringLiteral("%1%2   (%3)").arg(indent, name, segments.join(QStringLiteral(" / ")));
}

// Pré-ordem de árvore (raízes primeiro, cada uma seguida dos descendentes, alfabético entre irmãos). O QCompleter
// filtra por substring preservando a ordem do modelo, então a pasta aparece antes dos filhos.
inline QVector<core::Folder> foldersInTreeOrder(const QVector<core::Folder> &allFolders)
{
    QMap<QString, QVector<core::Folder>> childrenByParent; // "" = raiz
    for (const core::Folder &f : allFolders) {
        childrenByParent[f.parentId.value_or(QString())].append(f);
    }
    for (auto it = childrenByParent.begin(); it != childrenByParent.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
            [](const core::Folder &a, const core::Folder &b) {
                return a.name.localeAwareCompare(b.name) < 0;
            });
    }

    QVector<core::Folder> ordered;
    ordered.reserve(allFolders.size());
    QSet<QString> visited; // guarda contra ciclo em dado corrompido
    std::function<void(const QString &)> visit = [&](const QString &parentId) {
        for (const core::Folder &f : childrenByParent.value(parentId)) {
            if (visited.contains(f.id)) {
                continue;
            }
            visited.insert(f.id);
            ordered.append(f);
            visit(f.id);
        }
    };
    visit(QString());
    // Sobra defensiva: pasta órfã (parent_id aponta pra algo inexistente)
    // não alcançada pela varredura acima — inclui no fim, não descarta.
    if (ordered.size() < allFolders.size()) {
        for (const core::Folder &f : allFolders) {
            if (!visited.contains(f.id)) {
                ordered.append(f);
            }
        }
    }
    return ordered;
}

// Trava a largura do combo fechado em `chars` caracteres (o sizeHint padrão segue o item mais largo e inflava o
// diálogo com caminhos longos). O dropdown aberto continua largo.
inline void capComboBoxWidth(QComboBox *combo, int chars = 26)
{
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(chars);
}

// Navegação lateral + stack de páginas no padrão do SettingsDialog (mesmo objectName "settingsNav" para o QSS).
struct SidebarTabsHost {
    QListWidget *nav = nullptr;
    QStackedWidget *pages = nullptr;
};

// `navWidth`: largura fixa do nav; itens maiores elidem (tooltip com o texto completo em addSidebarTabPage).
inline SidebarTabsHost buildSidebarTabsHost(QWidget *parent, int navWidth = 200)
{
    auto *bodyLayout = new QHBoxLayout(parent);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    SidebarTabsHost host;
    host.nav = new QListWidget(parent);
    host.nav->setObjectName(QStringLiteral("settingsNav"));
    host.nav->setFrameShape(QFrame::NoFrame);
    host.nav->setFixedWidth(navWidth);
    host.nav->setIconSize(QSize(16, 16));
    host.nav->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    host.nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyLayout->addWidget(host.nav, 0);

    host.pages = new QStackedWidget(parent);
    host.pages->setObjectName(QStringLiteral("sidebarTabsPages"));
    bodyLayout->addWidget(host.pages, 1);
    QObject::connect(host.nav, &QListWidget::currentRowChanged, host.pages, &QStackedWidget::setCurrentIndex);
    return host;
}

// Nova aba num SidebarTabsHost: página com scroll próprio (regra do viewport qualificada por objectName, nunca
// um background cru) e tooltip com o título completo. Devolve o QVBoxLayout dos cards.
inline QVBoxLayout *addSidebarTabPage(SidebarTabsHost &host, const QString &title, const QString &iconName)
{
    auto *pageContent = new QWidget();
    pageContent->setObjectName(QStringLiteral("sidebarTabPageContent"));
    pageContent->setStyleSheet(QStringLiteral(
        "QWidget#sidebarTabPageContent { background: transparent; }"));
    auto *pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setContentsMargins(16, 16, 16, 12);
    pageLayout->setSpacing(12);

    auto *pageScroll = new QScrollArea(host.pages);
    pageScroll->setWidgetResizable(true);
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pageScroll->setWidget(pageContent);
    pageScroll->viewport()->setObjectName(QStringLiteral("sidebarTabPageViewport"));
    pageScroll->viewport()->setStyleSheet(QStringLiteral(
        "QWidget#sidebarTabPageViewport { background: transparent; }"));

    host.pages->addWidget(pageScroll);
    auto *item = new QListWidgetItem(
        LucideIcons::icon(iconName, QColor(utils::tokens::mutedFg()), 16), title, host.nav);
    item->setToolTip(title);
    item->setSizeHint(QSize(0, 36));
    return pageLayout;
}

// Sub-namespace para não colidir com outros wrapWithLabel do app (ex.: fields::wrapWithLabel).
namespace layout_helpers {

// Campo com rótulo pequeno em caixa alta acima dele, em vez de QFormLayout.
inline QWidget *wrapWithLabel(QWidget *parent, const QString &label, QWidget *field)
{
    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("fieldLabelWrap"));
    // Container transparente: sem isto a regra global de QWidget pinta o bg escuro em volta do campo. Qualificado
    // por objectName para não vazar.
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

// Card plano de superfície (não colapsável) para as seções fixas de um diálogo.
inline QWidget *makeSurfaceCard(QWidget *parent)
{
    auto *card = new QWidget(parent);
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setObjectName(QStringLiteral("kaiSurfaceCard"));
    // Regra qualificada por #kaiSurfaceCard: um setStyleSheet sem seletor casaria com todos os filhos (o QSS do
    // ancestral vence o global) e pintaria QLineEdit/QComboBox com o fundo do card.
    // Usa o gradiente de tema "primary" quando declarado; senão surface2 sólido.
    const QString bgDecl = utils::tokens::hasGradient(QStringLiteral("primary"))
        ? utils::tokens::gradientQss(QStringLiteral("background-color"), QStringLiteral("primary"))
        : QStringLiteral("background-color: %1;").arg(utils::tokens::surface2());
    card->setStyleSheet(QStringLiteral(
        "QWidget#kaiSurfaceCard { %1 border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(bgDecl, utils::tokens::borderColor())
        .arg(utils::tokens::radiusLg()));
    return card;
}

// Banner de hint: fundo translúcido do accent, borda e texto na cor do accent. Qualificado por #kaiHintBanner.
inline QLabel *makeHintBanner(QWidget *parent, const QString &text)
{
    auto *hint = new QLabel(text, parent);
    hint->setObjectName(QStringLiteral("kaiHintBanner"));
    hint->setWordWrap(true);
    // Ignored na horizontal: com wordWrap, o sizeHint do QLabel ainda impõe largura mínima ao layout pai e
    // estourava o diálogo.
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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

// Botões do cabeçalho dos editores de JSON cru: voltar ao modo simples (estilo "link", igual ao botão que leva ao
// modo avançado), Formatar e Minificar (só ícone; o rótulo vira tooltip). Só os widgets são compartilhados:
// os cliques ficam com o chamador. `rawLabelText`: rótulo à esquerda; `backButtonText`: texto do voltar.
struct JsonEditorHeaderButtons {
    QToolButton *backButton = nullptr;
    QToolButton *formatButton = nullptr;
    QToolButton *minifyButton = nullptr;
};

// QToolButton só-ícone no padrão de ação discreta de cabeçalho (autoRaise, hoverBg no hover).
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

    buttons.formatButton = makeHeaderIconButton(parent, QStringLiteral("braces"),
        utils::tr(QStringLiteral("json.dialog.format_title")));
    headerLayout->addWidget(buttons.formatButton);

    buttons.minifyButton = makeHeaderIconButton(parent, QStringLiteral("shrink"),
        utils::tr(QStringLiteral("json.dialog.minify_title")));
    headerLayout->addWidget(buttons.minifyButton);

    return buttons;
}

// Remove os ícones nativos que alguns estilos do Qt põem no OK/Cancel do QDialogButtonBox e traduz os rótulos
// padrão pelo language pack. Chamar logo após criar o box.
inline void stripDialogButtonIcons(QDialogButtonBox *box)
{
    if (!box) {
        return;
    }
    // Traduz por botão-padrão conhecido; sem a chave no pack, mantém o texto atual.
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
            // utils::tr devolve a própria chave sem tradução: nesse caso mantém o texto do Qt.
            if (t != QString::fromLatin1(s.key)) {
                b->setText(t);
            }
        }
    }
    const auto buttons = box->buttons();
    for (QAbstractButton *button : buttons) {
        button->setIcon(QIcon());
    }
    // A ação primária (Ok/Save/Yes) vira default explícito: o QSS "QPushButton:default" dá o accent e o
    // QDialogButtonBox nem sempre marca sozinho. autoDefault fica desligado nos demais (senão o botão focado vira
    // o :default e herda o gradiente primário). kaiRole="primary" é fixo, independe de foco; Enter segue confirmando.
    for (QAbstractButton *button : buttons) {
        if (auto *push = qobject_cast<QPushButton *>(button)) {
            push->setAutoDefault(false);
        }
    }
    for (auto role : {QDialogButtonBox::Ok, QDialogButtonBox::Save, QDialogButtonBox::Yes}) {
        if (QPushButton *primary = box->button(role)) {
            primary->setDefault(true);
            primary->setProperty("kaiRole", QStringLiteral("primary"));
            break;
        }
    }
}

// Substituto de QMessageBox::question com Sim/Não no idioma do app (os StandardButtons usam traduções do Qt
// que o Kai não carrega; os rótulos vêm de dialog.yes/dialog.no). Devolve true só se o usuário confirmar.
// Usa addButton(QMessageBox::Yes/No) e só troca o texto: com addButton(texto, role), box.button(QMessageBox::Yes)
// (usado nos testes) devolve nullptr.
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

// A janela sobre a qual o diálogo deve abrir. Nem sempre é a janela pai: com a saída destacada, a janela do KIP
// ou o Kai em outra tela, quem abriu o diálogo usava OUTRA janela. Vale a janela ativa (visível, não minimizada,
// não o próprio diálogo); depois a do pai; senão, nenhuma.
inline QWidget *centerReferenceFor(QWidget *dialog, QWidget *activeWindow)
{
    auto usable = [dialog](QWidget *w) {
        return w && w != dialog && !dialog->isAncestorOf(w) && w->isVisible() && !w->isMinimized();
    };
    if (usable(activeWindow)) {
        return activeWindow;
    }
    QWidget *parentWindow = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
    return usable(parentWindow) ? parentWindow : nullptr;
}

// Centraliza `dialog` sobre `ref` (a janela de referência), pinçado na área disponível da TELA DELA; sem referência,
// na tela onde está o cursor (o Kai pode estar só na bandeja).
inline void moveCenteredOn(QWidget *dialog, QWidget *ref)
{
    if (!dialog) {
        return;
    }
    QScreen *screen = ref ? ref->screen() : QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = dialog->screen();
    }
    const QRect refGeom = ref ? ref->frameGeometry() : (screen ? screen->availableGeometry() : dialog->frameGeometry());
    const QSize size = dialog->size();
    int x = refGeom.center().x() - size.width() / 2;
    int y = refGeom.center().y() - size.height() / 2;

    if (screen) {
        // Não usar qBound: faz Q_ASSERT(!(max < min)) no Qt6 e aborta em Debug quando o diálogo é maior que a área
        // disponível. qMax(min, qMin(...)) cai no canto superior-esquerdo.
        const QRect avail = screen->availableGeometry();
        x = qMax(avail.left(), qMin(x, avail.right() - size.width() + 1));
        y = qMax(avail.top(), qMin(y, avail.bottom() - size.height() + 1));
    } else {
        x = qMax(0, x);
        y = qMax(0, y);
    }
    dialog->move(x, y);
}

// A janela de referência ESCOLHIDA para este diálogo, lembrada entre a primeira centralização e o ajuste logo após o
// Show (nessa hora o próprio diálogo já pode ser a janela ativa, e a ativa de antes se perderia).
class CenterReference : public QObject {
public:
    explicit CenterReference(QWidget *dialog) : QObject(dialog) { setObjectName(QStringLiteral("kaiCenterReference")); }
    QPointer<QWidget> window;
};

inline CenterReference *centerReferenceHolder(QWidget *dialog)
{
    auto *holder = dialog->findChild<CenterReference *>(QStringLiteral("kaiCenterReference"), Qt::FindDirectChildrenOnly);
    return holder ? holder : new CenterReference(dialog);
}

inline void moveCenteredOnParent(QWidget *dialog)
{
    // Um diálogo embutido num painel (sem moldura de janela) é posicionado pelo layout/área de rolagem do painel: centralizá-lo
    // o deslocava dentro do cartão a cada vez que o quadro o mostrava de novo.
    if (!dialog || !dialog->isWindow()) {
        return;
    }
    CenterReference *holder = centerReferenceHolder(dialog);
    QWidget *ref = holder->window ? holder->window.data() : centerReferenceFor(dialog, QApplication::activeWindow());
    moveCenteredOn(dialog, ref);
}

// Chamado quando o diálogo vai ser MOSTRADO: escolhe a janela de referência agora (a que o usuário está usando) e
// centraliza; o ajuste logo depois do Show reaproveita a mesma escolha.
inline void centerDialogOnShow(QWidget *dialog)
{
    if (!dialog || !dialog->isWindow()) {
        return;
    }
    CenterReference *holder = centerReferenceHolder(dialog);
    holder->window = centerReferenceFor(dialog, QApplication::activeWindow());
    moveCenteredOnParent(dialog);
    QPointer<QWidget> guard(dialog);
    QTimer::singleShot(0, dialog, [guard]() {
        if (guard) {
            moveCenteredOnParent(guard);
        }
    });
}

// Re-centraliza no Show: o centerOnParent() do construtor roda antes do layout, com o tamanho provisório de
// um QDialog ainda não mostrado. O singleShot(0) pega ajustes de tamanho logo após mostrar.
class CenterOnShowFilter : public QObject {
public:
    explicit CenterOnShowFilter(QWidget *dialog) : QObject(dialog) {}

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show && watched == parent()) {
            centerDialogOnShow(static_cast<QWidget *>(watched));
        }
        return false;
    }
};

// Centraliza sobre o pai (sem pai, na tela do próprio widget) e repete no Show. O ponto central é pinçado na
// availableGeometry da tela (multi-monitor: não começa em (0,0)); se maior que a área, alinha ao canto superior-esquerdo.
inline void centerOnParent(QWidget *dialog)
{
    if (!dialog) {
        return;
    }
    moveCenteredOnParent(dialog);
    if (!dialog->property("kaiCenterOnShow").toBool()) {
        dialog->setProperty("kaiCenterOnShow", true);
        dialog->installEventFilter(new CenterOnShowFilter(dialog));
    }
}

// Combo editável com filtro por substring (case-insensitive) e popup de completamento. Chamar depois de popular.
// NoInsert: ler a escolha por currentData()/itemData(), nunca pelo texto digitado.
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
    // O popup do completer é um QAbstractItemView separado que não herda o QSS de lista: aplica os tokens com o
    // mesmo critério (cores, raio, hover) de "QComboBox QAbstractItemView" em app-stylesheet.cpp.
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
            "QAbstractItemView::item:selected { background-color: %6; color: %2; }"
            "QAbstractItemView::item:hover { background-color: %7; }")
            .arg(tokens::surface2(), tokens::fg(), tokens::borderColor())
            .arg(tokens::radiusMd()).arg(tokens::radiusSm())
            .arg(tokens::selBg(), tokens::hoverBg()));
    }
    combo->setCompleter(completer);
}

// Atalho remapeável (action.toggle_edit_mode) que alterna modo simples <-> avançado clicando em `advancedButton`.
// Chamar após criar o botão.
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