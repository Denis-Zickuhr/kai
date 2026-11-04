#include "ui/shared/folder-picker-widget.h"
#include "core/models.h"
#include "utils/design-tokens.h"

#include <QCompleter>
#include <QHash>
#include <QPen>
#include <QSet>
#include <QVariantList>
#include <functional>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QAbstractItemView>
#include <QStandardItemModel>
#include <QStandardItem>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
constexpr int kPillHPad = 8;   // padding horizontal interno de cada pill
constexpr int kPillGap = 4;    // espaço ENTRE pills
constexpr int kRowVPad = 6;    // padding vertical da linha do item (espaçamento pedido pela spec)
constexpr int kRowHPad = 8;    // padding horizontal da linha do item
constexpr int kIndent = 18;    // recuo por nível de profundidade (árvore)
constexpr int kDepthRole = Qt::UserRole + 2;   // int: profundidade (0 = raiz)
constexpr int kGuidesRole = Qt::UserRole + 3;  // QVariantList<bool>: por nível, "ainda há irmãos abaixo"

// Tom do accent para o segmento na profundidade `depth` (0 = raiz): mais claro quanto mais fundo.
QColor pillColorForDepth(int depth)
{
    QColor base(tk::accent());
    // +14% de luminosidade por nível, com teto (evita branco puro em hierarquias profundas).
    const int lighten = 100 + qMin(depth, 6) * 14;
    return base.lighter(lighten);
}

// Cor de texto legível sobre `bg` (critério simples de luminância).
QColor readableTextColor(const QColor &bg)
{
    const double luminance = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
    return luminance > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
}
} // namespace

FolderPickerPillDelegate::FolderPickerPillDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void FolderPickerPillDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    const QStringList segments = index.data(Qt::UserRole + 1).toStringList();
    if (segments.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    } else if (option.state & QStyle::State_MouseOver) {
        QColor hover(tk::hoverBg());
        painter->fillRect(option.rect, hover);
    }

    const QFont font = option.font;
    painter->setFont(font);
    const QFontMetrics fm(font);
    const int radius = tk::radiusSm();

    const int depth = index.data(kDepthRole).toInt();
    const QVariantList guides = index.data(kGuidesRole).toList();
    const bool selected = option.state & QStyle::State_Selected;

    // Uma coluna de recuo por ancestral, com a linha vertical se ele ainda tem irmãos abaixo; o último nível
    // é o conector ├/└ até a pill da pasta.
    QColor guide(selected ? option.palette.highlightedText().color() : QColor(tk::mutedFg()));
    guide.setAlpha(110);
    painter->setPen(QPen(guide, 1));
    const int left = option.rect.left() + kRowHPad;
    const int midY = option.rect.center().y();
    for (int level = 0; level < depth; ++level) {
        const int cx = left + level * kIndent + kIndent / 2;
        const bool more = level < guides.size() && guides.at(level).toBool();
        if (level < depth - 1) {
            if (more) {
                painter->drawLine(cx, option.rect.top(), cx, option.rect.bottom());
            }
        } else {
            painter->drawLine(cx, option.rect.top(), cx, more ? option.rect.bottom() : midY);
            painter->drawLine(cx, midY, cx + kIndent / 2 - 2, midY);
        }
    }

    const int x = left + depth * kIndent;
    const int chipHeight = fm.height() + kRowVPad;
    const int y = option.rect.top() + (option.rect.height() - chipHeight) / 2;
    const int maxTextWidth = qMax(24, option.rect.right() - kRowHPad - x - kPillHPad * 2);
    const QString text = fm.elidedText(segments.last(), Qt::ElideRight, maxTextWidth);
    const QRect chipRect(x, y, fm.horizontalAdvance(text) + kPillHPad * 2, chipHeight);
    const QColor bg = pillColorForDepth(depth);

    QPainterPath path;
    path.addRoundedRect(chipRect, radius, radius);
    painter->fillPath(path, bg);
    painter->setPen(readableTextColor(bg));
    painter->drawText(chipRect, Qt::AlignCenter, text);

    painter->restore();
}

QSize FolderPickerPillDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QStringList segments = index.data(Qt::UserRole + 1).toStringList();
    if (segments.isEmpty()) {
        return QStyledItemDelegate::sizeHint(option, index);
    }

    const QFontMetrics fm(option.font);
    const int width = kRowHPad * 2 + index.data(kDepthRole).toInt() * kIndent
        + fm.horizontalAdvance(segments.last()) + kPillHPad * 2;
    return QSize(width, fm.height() + kRowVPad * 2);
}

FolderPickerWidget::FolderPickerWidget(QWidget *parent)
    : QComboBox(parent)
{
    setItemDelegate(new FolderPickerPillDelegate(this));
    // Caminhos longos não podem alargar o diálogo: o combo fechado clipa.
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(16);

    connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_indexToFolderId.contains(index)) {
            emit selectionChanged(m_indexToFolderId[index]);
        }
    });
}

QStringList FolderPickerWidget::pathSegments(const QString &folderId) const
{
    QStringList path;
    QString currentId = folderId;
    QSet<QString> seen;
    while (!currentId.isEmpty() && !seen.contains(currentId)) {
        seen.insert(currentId);
        const kai::core::Folder *folder = nullptr;
        for (const auto &f : m_folders) {
            if (f.id == currentId) {
                folder = &f;
                break;
            }
        }
        if (!folder) break;
        path.prepend(folder->name);
        currentId = folder->parentId.value_or(QString());
    }
    return path;
}

void FolderPickerWidget::setFolders(const QVector<kai::core::Folder> &folders)
{
    m_folders = folders;
    populateItems();
}

void FolderPickerWidget::populateItems()
{
    blockSignals(true);
    clear();
    m_indexToFolderId.clear();

    // Ordem de árvore (pai antes dos filhos, irmãos na ordem recebida);
    // pastas cujo pai não está na lista viram raízes.
    QSet<QString> known;
    for (const auto &folder : m_folders) {
        known.insert(folder.id);
    }
    QHash<QString, QVector<const kai::core::Folder *>> children;
    QVector<const kai::core::Folder *> roots;
    for (const auto &folder : m_folders) {
        const QString parent = folder.parentId.value_or(QString());
        if (parent.isEmpty() || !known.contains(parent) || parent == folder.id) {
            roots.append(&folder);
        } else {
            children[parent].append(&folder);
        }
    }

    int index = 0;
    QSet<QString> visited;
    std::function<void(const kai::core::Folder *, QVariantList)> visit =
        [&](const kai::core::Folder *folder, QVariantList guides) {
            if (visited.contains(folder->id)) return;
            visited.insert(folder->id);
            const QStringList segments = pathSegments(folder->id);
            // Texto plano = caminho completo (fallback do delegate, combo fechado e busca).
            addItem(segments.join(QStringLiteral(" › ")));
            // Qt::UserRole (padrão de itemData/currentData) guarda o folderId; as outras roles servem só ao delegate.
            setItemData(index, folder->id, Qt::UserRole);
            setItemData(index, segments, Qt::UserRole + 1);
            setItemData(index, int(guides.size()), kDepthRole);
            setItemData(index, guides, kGuidesRole);
            m_indexToFolderId[index] = folder->id;
            index++;
            const auto &kids = children.value(folder->id);
            for (int i = 0; i < kids.size(); ++i) {
                QVariantList childGuides = guides;
                childGuides.append(i < kids.size() - 1);
                visit(kids.at(i), childGuides);
            }
        };
    for (int i = 0; i < roots.size(); ++i) {
        // Raízes não têm conector: o nível 0 só entra a partir dos filhos.
        visit(roots.at(i), {});
    }
    // Ciclos (A→B→A) nunca chegam a uma raiz; mostra o resto em vez de sumir.
    for (const auto &folder : m_folders) {
        visit(&folder, {});
    }

    blockSignals(false);
}

QString FolderPickerWidget::selectedFolderId() const
{
    int idx = currentIndex();
    return m_indexToFolderId.value(idx, QString());
}

void FolderPickerWidget::setSelectedFolderId(const QString &folderId)
{
    blockSignals(true);

    for (auto it = m_indexToFolderId.begin(); it != m_indexToFolderId.end(); ++it) {
        if (it.value() == folderId) {
            setCurrentIndex(it.key());
            break;
        }
    }

    blockSignals(false);
}

void FolderPickerWidget::enableNoneOption(const QString &noneText)
{
    m_noneOptionEnabled = true;
    populateItems();
    blockSignals(true);
    insertItem(0, noneText.isEmpty() ? QStringLiteral("None") : noneText, QString());
    QMap<int, QString> newMapping;
    for (auto it = m_indexToFolderId.begin(); it != m_indexToFolderId.end(); ++it) {
        newMapping[it.key() + 1] = it.value();
    }
    newMapping[0] = QString();
    m_indexToFolderId = newMapping;
    // QComboBox::insertItem(0, ...) mantém a seleção no item antigo (agora no índice 1): sem isto, "None" habilitado
    // ficava com a primeira pasta real selecionada.
    setCurrentIndex(0);
    blockSignals(false);
}

} // namespace kai::ui
