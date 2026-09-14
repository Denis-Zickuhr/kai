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
#include <QShortcut>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kPathRole = Qt::UserRole + 1; // caminho do arquivo (vazio nas pastas)
constexpr int kDirRole = Qt::UserRole + 2;  // caminho da pasta (só nas pastas)

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
    setMinimumWidth(190);
    m_enabled = defaultExtensions();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QWidget(this);
    auto *row = new QHBoxLayout(toolbar);
    row->setContentsMargins(tk::space(2), tk::space(2), tk::space(1), tk::space(2));
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

    m_new = makeIconButton(toolbar, QStringLiteral("file-plus"), utils::tr(QStringLiteral("doc.files.new_tooltip")),
                           QColor(tk::mutedFg()));
    m_new->setObjectName(QStringLiteral("docFileNew"));
    row->addWidget(m_new);
    layout->addWidget(toolbar);

    m_tree = new FileTree(this);
    m_tree->setMouseTracking(true);
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);

    m_empty = new QLabel(this);
    m_empty->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_empty->setWordWrap(true);
    m_empty->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), 0);
    m_empty->hide();
    layout->addWidget(m_empty, 1); // sem arquivos para mostrar, o aviso fica no topo e a seção de notas no pé

    // Seção "Notas": um cabeçalho com o "+" e a lista das notas da pasta. Escondida até o leitor habilitar.
    m_notesSection = new QWidget(this);
    m_notesSection->setObjectName(QStringLiteral("docNotesSection"));
    auto *notesLayout = new QVBoxLayout(m_notesSection);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->setSpacing(0);
    auto *notesHeader = new QWidget(m_notesSection);
    notesHeader->setObjectName(QStringLiteral("docNotesHeader"));
    auto *notesRow = new QHBoxLayout(notesHeader);
    notesRow->setContentsMargins(tk::space(3), tk::space(1), tk::space(1), tk::space(1));
    auto *notesTitle = new QLabel(utils::tr(QStringLiteral("doc.notes.title")), notesHeader);
    notesTitle->setObjectName(QStringLiteral("docNotesTitle"));
    notesRow->addWidget(notesTitle, 1);
    m_newNote = makeIconButton(notesHeader, QStringLiteral("plus"), utils::tr(QStringLiteral("doc.notes.new_tooltip")),
                               QColor(tk::mutedFg()));
    m_newNote->setObjectName(QStringLiteral("docNotesNew"));
    notesRow->addWidget(m_newNote);
    notesLayout->addWidget(notesHeader);
    m_notesTree = new FileTree(m_notesSection);
    m_notesTree->setObjectName(QStringLiteral("docNotesTree"));
    m_notesTree->setMouseTracking(true);
    m_notesTree->setHeaderHidden(true);
    m_notesTree->setFrameShape(QFrame::NoFrame);
    m_notesTree->setRootIsDecorated(false);
    m_notesTree->setIndentation(0);
    m_notesTree->setUniformRowHeights(true);
    m_notesTree->setMaximumHeight(190);
    notesLayout->addWidget(m_notesTree);
    m_notesSection->hide();
    m_notesSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum); // nunca estica: o cabeçalho não "flutua"
    layout->addWidget(m_notesSection);
    connect(m_newNote, &QToolButton::clicked, this, &DocFileSidebar::newNoteRequested);
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
    connect(m_new, &QToolButton::clicked, this, [this]() { emit newFileRequested(targetDirectory()); });
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
    refreshStyle();
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
    m_empty->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    QPalette notesPalette = m_notesTree->palette();
    notesPalette.setColor(QPalette::Text, QColor(tk::fg()));
    notesPalette.setColor(QPalette::Highlight, Qt::transparent);
    notesPalette.setColor(QPalette::HighlightedText, QColor(tk::fg()));
    m_notesTree->setPalette(notesPalette);
    m_notesTree->setStyleSheet(m_tree->styleSheet());
    m_notesSection->setStyleSheet(QStringLiteral(
        "QWidget#docNotesHeader { background-color: %1; border-top: 1px solid %2; }"
        "QLabel#docNotesTitle { color: %3; font-weight: 600; background: transparent; }")
        .arg(tk::bg(), tk::borderColor(), tk::mutedFg()));
    m_newNote->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::mutedFg()), 17));
    m_new->setIcon(LucideIcons::icon(QStringLiteral("file-plus"), QColor(tk::mutedFg()), 17));
    updateTypesButton();
    if (!m_files.isEmpty()) {
        rebuild(); // os ícones das linhas levam as cores do tema
    }
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
        item->setToolTip(0, utils::tr(note.local ? QStringLiteral("note.tooltip.local") : QStringLiteral("note.tooltip.synced")));
    }
    m_notesSection->setVisible(enabled);
    m_notesTree->setVisible(!notes.isEmpty());
    setCurrentFile(m_current);
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
    m_empty->setVisible(shown == 0 && !m_files.isEmpty());
    m_empty->setText(utils::tr(searching ? QStringLiteral("doc.files.no_match") : QStringLiteral("doc.files.none_of_type")));
    m_tree->setVisible(shown > 0 || m_files.isEmpty());
    setCurrentFile(m_current);
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
