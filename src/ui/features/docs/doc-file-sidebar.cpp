#include "ui/features/docs/doc-file-sidebar.h"

#include "ui/features/docs/doc-links.h"
#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShortcut>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <functional>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kPathRole = Qt::UserRole + 1; // caminho do arquivo (vazio nas pastas)
constexpr int kDirRole = Qt::UserRole + 2;  // caminho da pasta (só nas pastas)
constexpr int kTypeRole = Qt::UserRole + 3; // tipo da nota (só na seção de notas)

// A seleção/hover de uma linha da árvore é pintada aqui, na linha INTEIRA (setinha + ícone + nome) e com cantos
// arredondados. Pelo stylesheet só o texto era pintado e a área da setinha ficava com um retângulo quadrado.
class FileTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    // O hover cobre a linha toda (inclusive a área da setinha): repinta a viewport em vez de só o retângulo do item.
    void mouseMoveEvent(QMouseEvent *event) override
    {
        QTreeWidget::mouseMoveEvent(event);
        viewport()->update();
    }
    void leaveEvent(QEvent *event) override
    {
        QTreeWidget::leaveEvent(event);
        viewport()->update();
    }
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        // As flags de seleção/hover do `option` só valem dentro do drawRow base: aqui se consulta o modelo e o mouse.
        const bool selected = selectionModel() && selectionModel()->isSelected(index);
        const bool hovered = !selected && viewport()->underMouse()
            && indexAt(viewport()->mapFromGlobal(QCursor::pos())).siblingAtColumn(0) == index.siblingAtColumn(0);
        if (selected || hovered) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            const QRectF row(4, option.rect.top() + 1, viewport()->width() - 8, option.rect.height() - 2);
            painter->drawRoundedRect(row, tk::radiusSm(), tk::radiusSm());
            painter->restore();
        }
        QTreeWidget::drawRow(painter, option, index);
    }
};

// O cabeçalho de uma seção: clicar em qualquer ponto livre dele abre/recolhe (os botões tratam o próprio clique).
class SectionHeader : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QWidget::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && onClick) {
            onClick();
        }
    }
};

// A linha de uma nota leva à direita o tipo dela (MD, JSON...) numa pastilha discreta.
class NoteRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QString tag = index.data(kTypeRole).toString();
        QStyleOptionViewItem textOption(option);
        QFont tagFont = option.font;
        tagFont.setPointSizeF(std::max(7.0, option.font.pointSizeF() * 0.78));
        tagFont.setBold(true);
        const int tagWidth = tag.isEmpty() ? 0 : QFontMetrics(tagFont).horizontalAdvance(tag) + tk::space(2);
        if (tagWidth > 0) {
            textOption.rect.setRight(option.rect.right() - tagWidth - tk::space(2));
        }
        QStyledItemDelegate::paint(painter, textOption, index);
        if (tagWidth == 0) {
            return;
        }
        const int height = QFontMetrics(tagFont).height() + 2;
        const QRect pill(option.rect.right() - tagWidth - tk::space(1), option.rect.center().y() - height / 2 + 1, tagWidth, height);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QColor fill(tk::mutedFg());
        fill.setAlpha(36);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(pill, tk::radiusSm(), tk::radiusSm());
        painter->setFont(tagFont);
        painter->setPen(QColor(tk::mutedFg()));
        painter->drawText(pill, Qt::AlignCenter, tag);
        painter->restore();
    }
};

// Quantas linhas a árvore mostra agora (as pastas recolhidas escondem os filhos).
int visibleRowCount(const QTreeWidget *tree)
{
    int rows = 0;
    std::function<void(const QTreeWidgetItem *)> walk = [&](const QTreeWidgetItem *item) {
        ++rows;
        if (item->isExpanded()) {
            for (int i = 0; i < item->childCount(); ++i) {
                walk(item->child(i));
            }
        }
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        walk(tree->topLevelItem(i));
    }
    return rows;
}

int treeContentHeight(const QTreeWidget *tree)
{
    const int rows = visibleRowCount(tree);
    if (rows == 0) {
        return 0;
    }
    const int rowHeight = std::max(tree->sizeHintForRow(0), tree->fontMetrics().height() + 8);
    return rows * rowHeight + 2 * tree->frameWidth() + 4;
}

QString noteTypeLabel(const QString &type)
{
    if (type == QLatin1String("markdown")) return QStringLiteral("MD");
    if (type == QLatin1String("text")) return QStringLiteral("TXT");
    return type.toUpper();
}

QString iconNameFor(const QString &extension)
{
    static const QSet<QString> code = {QStringLiteral("json"), QStringLiteral("yml"), QStringLiteral("yaml"),
                                       QStringLiteral("xml"), QStringLiteral("html"), QStringLiteral("js"),
                                       QStringLiteral("ts"), QStringLiteral("py"), QStringLiteral("sh"),
                                       QStringLiteral("php"), QStringLiteral("css"), QStringLiteral("toml"),
                                       QStringLiteral("ini"), QStringLiteral("cpp"), QStringLiteral("h")};
    if (extension.isEmpty() || extension == QLatin1String("md") || extension == QLatin1String("markdown")
        || extension == QLatin1String("mdown") || extension == QLatin1String("txt")) {
        return QStringLiteral("file-text");
    }
    return code.contains(extension) ? QStringLiteral("file-code") : QStringLiteral("file");
}

} // namespace

QSet<QString> DocFileSidebar::defaultExtensions()
{
    return {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"), QStringLiteral("txt"), QString()};
}

DocFileSidebar::DocFileSidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("docFileSidebar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(190);
    m_enabled = defaultExtensions();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Cabeçalho de seção: seta, título, quantidade e o "+" (o mesmo ícone nas duas seções).
    const auto buildHeader = [this](QWidget *section, const QString &objectName, const QString &titleKey, QLabel *&chevron,
                                    QLabel *&title, QLabel *&count, QToolButton *&add, const QString &addObjectName,
                                    const QString &addTooltipKey, bool notes) {
        auto *header = new SectionHeader(section);
        header->setObjectName(objectName);
        header->setAttribute(Qt::WA_StyledBackground, true);
        header->setCursor(Qt::PointingHandCursor);
        header->onClick = [this, notes]() { setSectionExpanded(notes, notes ? !m_notesExpanded : !m_docsExpanded); };
        auto *row = new QHBoxLayout(header);
        row->setContentsMargins(tk::space(2), tk::space(1) / 2, tk::space(1), tk::space(1) / 2);
        row->setSpacing(tk::space(1));
        chevron = new QLabel(header);
        chevron->setObjectName(QStringLiteral("docSectionChevron"));
        chevron->setFixedSize(14, 14);
        title = new QLabel(utils::tr(titleKey), header);
        title->setObjectName(QStringLiteral("docSectionTitle"));
        count = new QLabel(header);
        count->setObjectName(QStringLiteral("docSectionCount"));
        add = makeIconButton(header, QStringLiteral("plus"), utils::tr(addTooltipKey), QColor(tk::mutedFg()));
        add->setObjectName(addObjectName);
        row->addWidget(chevron);
        row->addWidget(title);
        row->addWidget(count);
        row->addStretch(1);
        row->addWidget(add);
        return header;
    };

    m_docsSection = new QWidget(this);
    m_docsSection->setObjectName(QStringLiteral("docDocsSection"));
    auto *docsLayout = new QVBoxLayout(m_docsSection);
    docsLayout->setContentsMargins(0, 0, 0, 0);
    docsLayout->setSpacing(0);
    m_docsHeader = buildHeader(m_docsSection, QStringLiteral("docDocsHeader"), QStringLiteral("doc.files.title"), m_docsChevron,
                               m_docsTitle, m_docsCount, m_new, QStringLiteral("docFileNew"),
                               QStringLiteral("doc.files.new_tooltip"), false);
    docsLayout->addWidget(m_docsHeader);
    m_docsBody = new QWidget(m_docsSection);
    m_docsBody->setObjectName(QStringLiteral("docDocsBody"));
    auto *docsBodyLayout = new QVBoxLayout(m_docsBody);
    docsBodyLayout->setContentsMargins(0, 0, 0, 0);
    docsBodyLayout->setSpacing(0);

    auto *toolbar = new QWidget(m_docsBody);
    toolbar->setObjectName(QStringLiteral("docFilterRow"));
    auto *row = new QHBoxLayout(toolbar);
    row->setContentsMargins(tk::space(2), tk::space(1), tk::space(1), tk::space(1));
    row->setSpacing(tk::space(1));

    m_filter = new QLineEdit(toolbar);
    m_filter->setObjectName(QStringLiteral("docFileFilter"));
    m_filter->setPlaceholderText(utils::tr(QStringLiteral("doc.files.search_placeholder")));
    m_filter->setClearButtonEnabled(true);
    m_filter->setFixedHeight(tk::iconButtonSize());
    row->addWidget(m_filter, 1);

    m_types = makeIconButton(toolbar, QStringLiteral("funnel"), utils::tr(QStringLiteral("doc.files.types_tooltip")),
                             QColor(tk::mutedFg()));
    m_types->setObjectName(QStringLiteral("docFileTypes"));
    row->addWidget(m_types);
    docsBodyLayout->addWidget(toolbar);

    m_tree = new FileTree(m_docsBody);
    m_tree->setMouseTracking(true);
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    // A altura mínima padrão de uma árvore passa de uma lista curta: sem isto o cabeçalho e o campo de busca é que encolhiam.
    m_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    docsBodyLayout->addWidget(m_tree, 1);

    m_empty = new QLabel(m_docsBody);
    m_empty->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_empty->setWordWrap(true);
    m_empty->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    m_empty->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_empty->hide();
    docsBodyLayout->addWidget(m_empty);
    docsLayout->addWidget(m_docsBody, 1);
    layout->addWidget(m_docsSection);

    m_notesSection = new QWidget(this);
    m_notesSection->setObjectName(QStringLiteral("docNotesSection"));
    auto *notesLayout = new QVBoxLayout(m_notesSection);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->setSpacing(0);
    m_notesHeader = buildHeader(m_notesSection, QStringLiteral("docNotesHeader"), QStringLiteral("doc.notes.title"),
                                m_notesChevron, m_notesTitle, m_notesCount, m_newNote, QStringLiteral("docNotesNew"),
                                QStringLiteral("doc.notes.new_tooltip"), true);
    notesLayout->addWidget(m_notesHeader);
    m_notesBody = new QWidget(m_notesSection);
    m_notesBody->setObjectName(QStringLiteral("docNotesBody"));
    auto *notesBodyLayout = new QVBoxLayout(m_notesBody);
    notesBodyLayout->setContentsMargins(0, 0, 0, 0);
    notesBodyLayout->setSpacing(0);
    m_notesTree = new FileTree(m_notesBody);
    m_notesTree->setObjectName(QStringLiteral("docNotesTree"));
    m_notesTree->setMouseTracking(true);
    m_notesTree->setHeaderHidden(true);
    m_notesTree->setFrameShape(QFrame::NoFrame);
    m_notesTree->setRootIsDecorated(false);
    m_notesTree->setIndentation(0);
    m_notesTree->setUniformRowHeights(true);
    m_notesTree->setItemDelegate(new NoteRowDelegate(m_notesTree));
    m_notesTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    notesBodyLayout->addWidget(m_notesTree, 1);
    m_notesEmpty = new QLabel(utils::tr(QStringLiteral("doc.notes.empty")), m_notesBody);
    m_notesEmpty->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_notesEmpty->setWordWrap(true);
    m_notesEmpty->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    m_notesEmpty->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    notesBodyLayout->addWidget(m_notesEmpty);
    notesLayout->addWidget(m_notesBody, 1);
    m_notesSection->hide();
    layout->addWidget(m_notesSection);
    // O que sobra fica vazio embaixo (sem isto o layout centraliza as seções). O peso é mínimo: só ele absorve a sobra
    // quando as seções já têm a altura que pedem.
    layout->addStretch(1);

    connect(m_newNote, &QToolButton::clicked, this, [this]() {
        setNotesExpanded(true); // a nota nova vai aparecer: a seção abre
        emit newNoteRequested();
    });
    connect(m_notesTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        emit noteActivated(item->data(0, kPathRole).toString());
    });
    m_notesTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_notesTree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_notesTree->itemAt(pos);
        if (!item) {
            return;
        }
        QMenu menu(this);
        QAction *remove = menu.addAction(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::errorFg()), 16),
                                         utils::tr(QStringLiteral("doc.files.delete")));
        remove->setShortcut(QKeySequence::Delete);
        if (menu.exec(m_notesTree->viewport()->mapToGlobal(pos)) == remove) {
            emit deleteNoteRequested(item->data(0, kPathRole).toString());
        }
    });
    auto *deleteNoteKey = new QShortcut(QKeySequence::Delete, m_notesTree);
    deleteNoteKey->setContext(Qt::WidgetShortcut);
    connect(deleteNoteKey, &QShortcut::activated, this, [this]() {
        if (QTreeWidgetItem *item = m_notesTree->currentItem()) {
            emit deleteNoteRequested(item->data(0, kPathRole).toString());
        }
    });

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &DocFileSidebar::rebuild);
    connect(m_filter, &QLineEdit::textChanged, this, [this]() { m_debounce->start(); });
    connect(m_types, &QToolButton::clicked, this, &DocFileSidebar::showTypesMenu);
    connect(m_new, &QToolButton::clicked, this, [this]() {
        setDocsExpanded(true);
        emit newFileRequested(targetDirectory());
    });
    // Abrir/fechar uma pasta muda a altura que a árvore pede.
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this]() { updateSectionLimits(); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this]() { updateSectionLimits(); });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        const QString path = item ? item->data(0, kPathRole).toString() : QString();
        if (path.isEmpty()) {
            return; // pastas não têm ação aqui
        }
        QMenu menu(this);
        QAction *remove = menu.addAction(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::errorFg()), 16),
                                         utils::tr(QStringLiteral("doc.files.delete")));
        remove->setShortcut(QKeySequence::Delete);
        if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == remove) {
            emit deleteFileRequested(path);
        }
    });
    auto *deleteKey = new QShortcut(QKeySequence::Delete, m_tree);
    deleteKey->setContext(Qt::WidgetShortcut);
    connect(deleteKey, &QShortcut::activated, this, [this]() {
        QTreeWidgetItem *item = m_tree->currentItem();
        const QString path = item ? item->data(0, kPathRole).toString() : QString();
        if (!path.isEmpty()) {
            emit deleteFileRequested(path);
        }
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        const QString path = item->data(0, kPathRole).toString();
        if (path.isEmpty()) {
            item->setExpanded(!item->isExpanded()); // clicar numa pasta abre/fecha
        } else {
            emit fileActivated(path);
        }
    });
    m_notesTree->hide(); // sem notas, o aviso curto ocupa o lugar
    rebuild();           // sem arquivos: o aviso no lugar da árvore
    refreshStyle();
    applySectionStates();
}

void DocFileSidebar::refreshStyle()
{
    // O realce de seleção/hover é do FileTree::drawRow: o item não pinta fundo, nem na área da setinha.
    QPalette treePalette = m_tree->palette();
    treePalette.setColor(QPalette::Text, QColor(tk::fg()));
    treePalette.setColor(QPalette::Highlight, Qt::transparent);
    treePalette.setColor(QPalette::HighlightedText, QColor(tk::fg()));
    m_tree->setPalette(treePalette);
    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background-color: %1; color: %2; border: none; outline: none; }"
        "QTreeWidget::item { padding: 3px 2px; }"
        "QTreeWidget::item:hover, QTreeWidget::item:selected { background: transparent; color: %2; }")
        .arg(tk::bg(), tk::fg()));
    // O raio não pode passar da metade da altura real do campo, senão o Qt o desenha QUADRADO (com "Arredondado" o
    // radiusMd é 16 e o campo tem ~27px).
    const int radius = tk::radiusMdForHeight(m_filter->maximumHeight());
    // Borda e raio redeclarados aqui (inclusive no foco): o campo segue o canto escolhido pelo usuário em qualquer estado.
    m_filter->setStyleSheet(QStringLiteral(
        "QLineEdit { min-height: 0px; padding: 1px %1px; border: 1px solid %3; border-radius: %2px; }"
        "QLineEdit:focus { border: 1px solid %4; border-radius: %2px; }")
        .arg(tk::space(2)).arg(radius).arg(tk::borderColor(), tk::accent()));
    for (QLabel *label : {m_empty, m_notesEmpty}) {
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    }
    QPalette notesPalette = m_notesTree->palette();
    notesPalette.setColor(QPalette::Text, QColor(tk::fg()));
    notesPalette.setColor(QPalette::Highlight, Qt::transparent);
    notesPalette.setColor(QPalette::HighlightedText, QColor(tk::fg()));
    m_notesTree->setPalette(notesPalette);
    m_notesTree->setStyleSheet(m_tree->styleSheet());
    // Os contêineres e rótulos assentam sobre o fundo da barra: o QSS global pinta todo QWidget/QLabel, daí o transparente.
    setStyleSheet(QStringLiteral(
        "QWidget#docFileSidebar { background-color: %1; }"
        "QWidget#docDocsSection, QWidget#docNotesSection, QWidget#docDocsBody, QWidget#docNotesBody, QWidget#docFilterRow"
        " { background: transparent; }"
        "QWidget#docDocsHeader { background-color: %1; }"
        "QWidget#docNotesHeader { background-color: %1; border-top: 1px solid %2; }"
        "QLabel#docSectionTitle { color: %3; font-weight: 600; background: transparent; }"
        "QLabel#docSectionCount, QLabel#docSectionChevron { color: %3; background: transparent; }")
        .arg(tk::bg(), tk::borderColor(), tk::mutedFg()));
    m_newNote->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::mutedFg()), 17));
    m_new->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::mutedFg()), 17));
    updateTypesButton();
    applySectionStates();
    if (!m_files.isEmpty()) {
        rebuild(); // os ícones das linhas levam as cores do tema
    }
}

void DocFileSidebar::setDocsExpanded(bool expanded)
{
    setSectionExpanded(false, expanded);
}

void DocFileSidebar::setNotesExpanded(bool expanded)
{
    setSectionExpanded(true, expanded);
}

void DocFileSidebar::setSectionExpanded(bool notes, bool expanded)
{
    (notes ? m_notesChosen : m_docsChosen) = true;
    (notes ? m_notesExpanded : m_docsExpanded) = expanded;
    applySectionStates();
}

// Seção sem conteúdo nasce recolhida; com a escolha do usuário, vale a dele. Seção recolhida só mostra o cabeçalho e a
// outra fica com o espaço.
void DocFileSidebar::applySectionStates()
{
    if (!m_docsChosen) {
        m_docsExpanded = !m_files.isEmpty();
    }
    if (!m_notesChosen) {
        m_notesExpanded = !m_noteItems.isEmpty();
    }
    m_docsBody->setVisible(m_docsExpanded);
    m_notesBody->setVisible(m_notesExpanded);
    const QColor muted(tk::mutedFg());
    m_docsChevron->setPixmap(LucideIcons::icon(m_docsExpanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                                               muted, 14).pixmap(14, 14));
    m_notesChevron->setPixmap(LucideIcons::icon(m_notesExpanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                                                muted, 14).pixmap(14, 14));
    m_docsCount->setText(m_shownCount > 0 ? QString::number(m_shownCount) : QString());
    m_notesCount->setText(m_noteItems.isEmpty() ? QString() : QString::number(m_noteItems.size()));
    if (auto *box = qobject_cast<QVBoxLayout *>(layout())) {
        box->setStretchFactor(m_docsSection, m_docsExpanded ? 1000 : 0);
        box->setStretchFactor(m_notesSection, m_notesExpanded ? 1000 : 0);
    }
    updateSectionLimits();
}

// Cada seção pede só a altura do que tem (cabeçalho + linhas): sobra vai para a outra, e o resto da barra fica vazio.
void DocFileSidebar::updateSectionLimits()
{
    const auto headerHeight = [](const QWidget *header) { return header->sizeHint().height(); };
    int docsMax = headerHeight(m_docsHeader);
    if (m_docsExpanded) {
        const QWidget *toolbar = m_docsBody->findChild<QWidget *>(QStringLiteral("docFilterRow"));
        docsMax += toolbar ? toolbar->sizeHint().height() : 0;
        docsMax += m_tree->isHidden() ? 0 : treeContentHeight(m_tree);
        if (!m_empty->isHidden()) {
            docsMax += m_empty->heightForWidth(std::max(1, width())) > 0 ? m_empty->heightForWidth(std::max(1, width()))
                                                                          : m_empty->sizeHint().height();
        }
    }
    docsMax += 2; // folga de arredondamento
    m_docsSection->setMaximumHeight(std::max(docsMax, m_docsSection->minimumSizeHint().height()));
    int notesMax = headerHeight(m_notesHeader);
    if (m_notesExpanded) {
        if (m_noteItems.isEmpty()) {
            notesMax += m_notesEmpty->heightForWidth(std::max(1, width())) > 0 ? m_notesEmpty->heightForWidth(std::max(1, width()))
                                                                                : m_notesEmpty->sizeHint().height();
        } else {
            notesMax += treeContentHeight(m_notesTree);
        }
    }
    notesMax += 2;
    m_notesSection->setMaximumHeight(std::max(notesMax, m_notesSection->minimumSizeHint().height()));
}

// Estilo, fonte e layout mudam a altura dos cabeçalhos e das linhas: os limites partem sempre do que está valendo agora.
bool DocFileSidebar::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::FontChange:
    case QEvent::LayoutRequest:
    case QEvent::ShowToParent:
        updateSectionLimits();
        break;
    default:
        break;
    }
    return handled;
}

void DocFileSidebar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateSectionLimits(); // o aviso de lista vazia muda de altura com a largura
}

void DocFileSidebar::clear()
{
    m_root.clear();
    m_current.clear();
    m_files.clear();
    m_counts.clear();
    m_tree->clear();
    m_empty->hide();
    m_filter->clear();
    m_noteItems.clear();
    m_notesTree->clear();
    m_notesSection->hide();
    m_shownCount = 0;
    m_docsChosen = false; // a pasta seguinte começa do zero
    m_notesChosen = false;
    applySectionStates();
}

void DocFileSidebar::setFiles(const QString &root, const QStringList &files)
{
    m_root = QDir::cleanPath(root);
    m_files = files;
    m_counts.clear();
    for (const QString &path : files) {
        ++m_counts[fileExtension(path)];
    }
    updateTypesButton();
    rebuild();
}

void DocFileSidebar::setNotes(const QVector<NoteItem> &notes, bool enabled)
{
    m_noteItems = notes;
    m_notesTree->clear();
    for (const NoteItem &note : notes) {
        auto *item = new QTreeWidgetItem(m_notesTree);
        item->setText(0, note.title);
        item->setIcon(0, IconPickerWidget::iconForName(note.icon.isEmpty() ? QStringLiteral("notebook") : note.icon));
        item->setData(0, kPathRole, note.id);
        item->setData(0, kTypeRole, noteTypeLabel(note.type));
        item->setToolTip(0, utils::tr(note.local ? QStringLiteral("note.tooltip.local") : QStringLiteral("note.tooltip.synced")));
    }
    m_notesSection->setVisible(enabled);
    m_notesTree->setVisible(!notes.isEmpty());
    m_notesEmpty->setVisible(notes.isEmpty());
    applyDraftMarks();
    setCurrentFile(m_current);
    applySectionStates();
}

QString DocFileSidebar::noteTypeTag(int row) const
{
    QTreeWidgetItem *item = m_notesTree->topLevelItem(row);
    return item ? item->data(0, kTypeRole).toString() : QString();
}

void DocFileSidebar::setDraftKeys(const QSet<QString> &keys)
{
    if (keys == m_drafts) {
        return;
    }
    m_drafts = keys;
    applyDraftMarks();
}

// Itálico + cor de aviso nos documentos com alterações não salvas (arquivos e notas).
void DocFileSidebar::applyDraftMarks()
{
    const QString draftTip = utils::tr(QStringLiteral("doc.draft.tooltip"));
    const auto mark = [&draftTip](QTreeWidgetItem *item, bool draft, const QString &normalTip) {
        QFont font = item->font(0);
        font.setItalic(draft);
        item->setFont(0, font);
        if (draft) {
            item->setForeground(0, QColor(tk::warningFg()));
            item->setToolTip(0, draftTip);
        } else {
            item->setData(0, Qt::ForegroundRole, QVariant());
            item->setToolTip(0, normalTip);
        }
    };
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const QString path = (*it)->data(0, kPathRole).toString();
        if (!path.isEmpty()) {
            mark(*it, m_drafts.contains(QDir::cleanPath(path)), QString());
        }
    }
    for (QTreeWidgetItemIterator it(m_notesTree); *it; ++it) {
        const QString id = (*it)->data(0, kPathRole).toString();
        if (id.isEmpty()) {
            continue;
        }
        bool local = true;
        for (const NoteItem &note : std::as_const(m_noteItems)) {
            if (note.id == id) {
                local = note.local;
            }
        }
        mark(*it, m_drafts.contains(QStringLiteral("note:") + id),
             utils::tr(local ? QStringLiteral("note.tooltip.local") : QStringLiteral("note.tooltip.synced")));
    }
}

void DocFileSidebar::setCurrentFile(const QString &file)
{
    m_current = QDir::cleanPath(file);
    // Uma nota aberta ("note:<id>") se marca na seção de notas e solta a seleção da árvore de arquivos, e vice-versa.
    const QString noteKey = QStringLiteral("note:");
    const bool isNote = m_current.startsWith(noteKey);
    {
        const QSignalBlocker notesBlocker(m_notesTree);
        m_notesTree->clearSelection();
        if (isNote) {
            const QString id = m_current.mid(noteKey.size());
            for (QTreeWidgetItemIterator it(m_notesTree); *it; ++it) {
                if ((*it)->data(0, kPathRole).toString() == id) {
                    m_notesTree->setCurrentItem(*it);
                    break;
                }
            }
        }
    }
    if (isNote) {
        const QSignalBlocker blocker(m_tree);
        m_tree->clearSelection();
        return;
    }
    const QSignalBlocker blocker(m_tree);
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if ((*it)->data(0, kPathRole).toString() == m_current) {
            m_tree->setCurrentItem(*it);
            // As pastas nascem recolhidas; só as do documento atual abrem, para ele aparecer.
            for (QTreeWidgetItem *parent = (*it)->parent(); parent; parent = parent->parent()) {
                parent->setExpanded(true);
            }
            return;
        }
        ++it;
    }
    m_tree->clearSelection();
}

bool DocFileSidebar::passesFilter(const QString &path, const QStringList &terms) const
{
    if (!m_enabled.contains(fileExtension(path))) {
        return false;
    }
    if (terms.isEmpty()) {
        return true;
    }
    const QString relative = QDir(m_root).relativeFilePath(path).toLower();
    return std::all_of(terms.begin(), terms.end(), [&](const QString &term) { return relative.contains(term); });
}

void DocFileSidebar::rebuild()
{
    const QStringList terms = m_filter->text().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool searching = !terms.isEmpty();
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    QHash<QString, QTreeWidgetItem *> folders;
    const QIcon folderIcon = LucideIcons::icon(QStringLiteral("folder"), QColor(tk::mutedFg()), 14);
    QHash<QString, QIcon> fileIcons;
    int shown = 0;
    for (const QString &path : std::as_const(m_files)) {
        if (!passesFilter(path, terms)) {
            continue;
        }
        ++shown;
        const QString relative = QDir(m_root).relativeFilePath(path);
        const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QTreeWidgetItem *parent = nullptr;
        QString key;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            key += parts.at(i) + QLatin1Char('/');
            QTreeWidgetItem *&folder = folders[key];
            if (!folder) {
                folder = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
                folder->setText(0, parts.at(i));
                folder->setIcon(0, folderIcon);
                folder->setData(0, kDirRole, QDir::cleanPath(m_root + QLatin1Char('/') + key));
                folder->setExpanded(searching); // pesquisando, tudo que casou fica à vista
            }
            parent = folder;
        }
        const QString iconName = iconNameFor(fileExtension(path));
        if (!fileIcons.contains(iconName)) {
            fileIcons.insert(iconName, LucideIcons::icon(iconName, QColor(tk::accent()), 14));
        }
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, parts.isEmpty() ? QFileInfo(path).fileName() : parts.last());
        item->setIcon(0, fileIcons.value(iconName));
        item->setData(0, kPathRole, path);
    }
    m_tree->setUpdatesEnabled(true);
    m_shownCount = shown;
    m_empty->setVisible(shown == 0);
    m_empty->setText(utils::tr(m_files.isEmpty() ? QStringLiteral("doc.files.empty")
                                                 : searching ? QStringLiteral("doc.files.no_match")
                                                             : QStringLiteral("doc.files.none_of_type")));
    m_tree->setVisible(shown > 0);
    applyDraftMarks();
    setCurrentFile(m_current);
    applySectionStates();
}

int DocFileSidebar::visibleFileCount(const QString &except) const
{
    const QString skipped = QDir::cleanPath(except);
    const QStringList terms = m_filter->text().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int count = 0;
    for (const QString &path : m_files) {
        if (QDir::cleanPath(path) != skipped && passesFilter(path, terms)) {
            ++count;
        }
    }
    return count;
}

QString DocFileSidebar::targetDirectory() const
{
    if (QTreeWidgetItem *item = m_tree->currentItem()) {
        const QString dir = item->data(0, kDirRole).toString();
        if (!dir.isEmpty()) {
            return dir;
        }
        const QString file = item->data(0, kPathRole).toString();
        if (!file.isEmpty()) {
            return QFileInfo(file).absolutePath();
        }
    }
    return m_root;
}

QStringList DocFileSidebar::detectedExtensions() const
{
    QStringList list = m_counts.keys();
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
        if (a.isEmpty() != b.isEmpty()) {
            return a.isEmpty(); // "sem extensão" primeiro
        }
        return a < b;
    });
    return list;
}

void DocFileSidebar::setExtensionEnabled(const QString &extension, bool enabled)
{
    if (enabled) {
        m_enabled.insert(extension);
    } else {
        m_enabled.remove(extension);
    }
    updateTypesButton();
    rebuild();
}

void DocFileSidebar::resetExtensions()
{
    m_enabled = defaultExtensions();
    updateTypesButton();
    rebuild();
}

void DocFileSidebar::enableAllExtensions()
{
    for (auto it = m_counts.constBegin(); it != m_counts.constEnd(); ++it) {
        m_enabled.insert(it.key());
    }
    updateTypesButton();
    rebuild();
}

void DocFileSidebar::disableAllExtensions()
{
    m_enabled.clear();
    updateTypesButton();
    rebuild();
}

bool DocFileSidebar::isDefaultFilter() const
{
    return m_enabled == defaultExtensions();
}

// O botão do filtro acende (cor de destaque) quando os tipos mostrados não são os padrão.
void DocFileSidebar::updateTypesButton()
{
    m_types->setIcon(LucideIcons::icon(QStringLiteral("funnel"),
                                       QColor(isDefaultFilter() ? tk::mutedFg() : tk::accent()), 17));
}

// Menu dos tipos: atalhos no topo (os três mantêm o menu aberto e atualizam as caixas) e, abaixo, a lista de extensões
// numa área com rolagem (uma pasta de código passa de 25 tipos).
void DocFileSidebar::showTypesMenu()
{
    QMenu menu(this);
    const QStringList extensions = detectedExtensions();

    auto *list = new QWidget;
    auto *listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    QVector<QPair<QString, QCheckBox *>> boxes;
    for (const QString &extension : extensions) {
        const QString label = extension.isEmpty() ? utils::tr(QStringLiteral("doc.files.no_extension"))
                                                  : QStringLiteral(".%1").arg(extension);
        auto *box = new QCheckBox(QStringLiteral("%1  (%2)").arg(label).arg(m_counts.value(extension)), list);
        box->setChecked(m_enabled.contains(extension));
        connect(box, &QCheckBox::toggled, this, [this, extension](bool checked) { setExtensionEnabled(extension, checked); });
        listLayout->addWidget(box);
        boxes.append({extension, box});
    }
    auto syncBoxes = [this, boxes]() {
        for (const auto &entry : boxes) {
            const QSignalBlocker blocker(entry.second);
            entry.second->setChecked(m_enabled.contains(entry.first));
        }
    };

    auto *quick = new QWidget(&menu);
    quick->setObjectName(QStringLiteral("docTypesQuick"));
    quick->setStyleSheet(QStringLiteral("QWidget#docTypesQuick { background: transparent; }")); // o fundo do menu tem canto
    auto *quickLayout = new QHBoxLayout(quick);
    quickLayout->setContentsMargins(tk::space(1), tk::space(1), tk::space(1), tk::space(1));
    quickLayout->setSpacing(tk::space(1));
    auto addQuick = [&](const QString &objectName, const QString &text, const QString &tip, void (DocFileSidebar::*apply)()) {
        auto *button = new QToolButton(quick);
        button->setObjectName(objectName);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { color: %1; border: none; border-radius: %2px; padding: 3px %3px; background: transparent; }"
            "QToolButton:hover { background-color: %4; }")
            .arg(tk::fg()).arg(tk::radiusSm()).arg(tk::space(2)).arg(tk::hoverBg()));
        connect(button, &QToolButton::clicked, this, [this, apply, syncBoxes]() {
            (this->*apply)();
            syncBoxes();
        });
        quickLayout->addWidget(button);
    };
    addQuick(QStringLiteral("docTypesAll"), utils::tr(QStringLiteral("doc.files.types_btn_all")),
             utils::tr(QStringLiteral("doc.files.types_all")), &DocFileSidebar::enableAllExtensions);
    addQuick(QStringLiteral("docTypesNone"), utils::tr(QStringLiteral("doc.files.types_btn_none")),
             utils::tr(QStringLiteral("doc.files.types_none")), &DocFileSidebar::disableAllExtensions);
    addQuick(QStringLiteral("docTypesDefault"), utils::tr(QStringLiteral("doc.files.types_btn_default")),
             utils::tr(QStringLiteral("doc.files.types_default")), &DocFileSidebar::resetExtensions);
    quickLayout->addStretch();
    auto *quickAction = new QWidgetAction(&menu);
    quickAction->setDefaultWidget(quick);
    menu.addAction(quickAction);

    if (!extensions.isEmpty()) {
        menu.addSeparator();
        auto *scroll = new QScrollArea(&menu);
        scroll->setObjectName(QStringLiteral("docTypesScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Fundos transparentes: o menu é quem pinta o fundo (com o canto do usuário); um fundo quadrado aqui o vazaria.
        scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"
                                             "QScrollArea > QWidget, QScrollArea > QWidget > QWidget { background: transparent; }"));
        scroll->viewport()->setAutoFillBackground(false);
        list->setAutoFillBackground(false);
        scroll->setWidget(list);
        // Até ~10 linhas: acima disso a lista rola.
        const int rowHeight = list->sizeHint().height() / int(extensions.size());
        scroll->setFixedHeight(qMin(list->sizeHint().height() + 2, rowHeight * 10));
        scroll->setMinimumWidth(list->sizeHint().width() + tk::space(6));
        auto *listAction = new QWidgetAction(&menu);
        listAction->setDefaultWidget(scroll);
        menu.addAction(listAction);
    } else {
        delete list;
    }
    menu.exec(m_types->mapToGlobal(QPoint(0, m_types->height())));
}

} // namespace kai::ui
