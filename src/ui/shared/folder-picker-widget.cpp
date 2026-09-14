#include "ui/shared/folder-picker-widget.h"
#include "core/models.h"
#include "utils/design-tokens.h"

#include <QCompleter>
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

// Tom do accent para o segmento na profundidade `depth` (0 = pasta raiz):
// quanto mais fundo, mais claro — pedido explícito da spec ("níveis mais
// profundos usam tons progressivamente mais claros").
QColor pillColorForDepth(int depth)
{
    QColor base(tk::accent());
    // +14% de luminosidade por nível, com teto (evita virar branco puro em
    // hierarquias muito profundas).
    const int lighten = 100 + qMin(depth, 6) * 14;
    return base.lighter(lighten);
}

// Cor de texto legível sobre `bg` (claro sobre escuro, escuro sobre claro) —
// mesmo critério simples de luminância usado noutros badges do Kai.
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

    // Fundo do item (seleção/hover) — mesmo comportamento padrão do estilo.
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

    int x = option.rect.left() + kRowHPad;
    const int chipHeight = fm.height() + kRowVPad;
    const int y = option.rect.top() + (option.rect.height() - chipHeight) / 2;

    for (int depth = 0; depth < segments.size(); ++depth) {
        const QString &text = segments.at(depth);
        const int textWidth = fm.horizontalAdvance(text);
        const int chipWidth = textWidth + kPillHPad * 2;

        const QRect chipRect(x, y, chipWidth, chipHeight);
        const QColor bg = pillColorForDepth(depth);

        QPainterPath path;
        path.addRoundedRect(chipRect, radius, radius);
        painter->fillPath(path, bg);

        painter->setPen(readableTextColor(bg));
        painter->drawText(chipRect, Qt::AlignCenter, text);

        x += chipWidth;
        if (depth < segments.size() - 1) {
            // Separador "›" entre pills, na cor de texto padrão do item.
            painter->setPen(option.state & QStyle::State_Selected
                ? option.palette.highlightedText().color()
                : QColor(tk::mutedFg()));
            const QRect sepRect(x, option.rect.top(), fm.horizontalAdvance(QStringLiteral(" › ")) + kPillGap,
                                option.rect.height());
            painter->drawText(sepRect, Qt::AlignCenter, QStringLiteral("›"));
            x += sepRect.width();
        }
    }

    painter->restore();
}

QSize FolderPickerPillDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QStringList segments = index.data(Qt::UserRole + 1).toStringList();
    if (segments.isEmpty()) {
        return QStyledItemDelegate::sizeHint(option, index);
    }

    const QFontMetrics fm(option.font);
    int width = kRowHPad * 2;
    for (int depth = 0; depth < segments.size(); ++depth) {
        width += fm.horizontalAdvance(segments.at(depth)) + kPillHPad * 2;
        if (depth < segments.size() - 1) {
            width += fm.horizontalAdvance(QStringLiteral(" › ")) + kPillGap;
        }
    }
    const int height = fm.height() + kRowVPad * 2;
    return QSize(width, height);
}

FolderPickerWidget::FolderPickerWidget(QWidget *parent)
    : QComboBox(parent)
{
    setItemDelegate(new FolderPickerPillDelegate(this));

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
    while (!currentId.isEmpty()) {
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

    int index = 0;
    for (const auto &folder : m_folders) {
        const QStringList segments = pathSegments(folder.id);
        // Texto plano (fallback do delegate + o que o QCompleter busca —
        // inclui TODOS os segmentos, não só o nome final, então buscar
        // "Projeto A" também acha subpastas dela mesmo filtrando pelo meio
        // do caminho).
        addItem(segments.join(QStringLiteral(" › ")));
        // Qt::UserRole (role PADRÃO de QComboBox::itemData/currentData): o
        // folderId em si — callers existentes (project-import-options-
        // dialog, testes) leem por aqui via currentData(), API nativa do
        // QComboBox, não só via selectedFolderId() (API própria deste
        // widget). Qt::UserRole+1 seguem os segmentos, só para o delegate
        // desenhar as pills.
        setItemData(index, folder.id, Qt::UserRole);
        setItemData(index, segments, Qt::UserRole + 1);
        m_indexToFolderId[index] = folder.id;
        index++;
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
    // BUG real corrigido (achado ao rodar a suíte de testes): QComboBox::
    // insertItem(0, ...) preserva a seleção do ITEM que já estava
    // selecionado (agora deslocado pro índice 1), não do índice 0 — sem
    // isto, um picker recém-criado com "None" habilitado acabava com a
    // PRIMEIRA pasta real selecionada em vez de "None", mesmo sem nenhuma
    // seleção explícita do chamador (bug relatado por 3 testes:
    // ProjectImportOptionsDialog, CollectionEditorDialog, CommandEditorDialog
    // — "parentFolderId()"/"currentData()" vinha com uma pasta em vez de
    // vazio/raiz).
    setCurrentIndex(0);
    blockSignals(false);
}

} // namespace kai::ui
