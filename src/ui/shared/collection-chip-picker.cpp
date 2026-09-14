#include "ui/shared/collection-chip-picker.h"
#include "ui/shared/flow-layout.h"
#include "ui/shared/fuzzy-search.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QLineEdit>
#include <QCompleter>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QAbstractItemView>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QColor>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QKeyEvent>
#include <QEvent>
#include <QTimer>

#include <QAbstractButton>
#include <QPainter>
#include <algorithm>
#include <functional>

namespace kai::ui {

namespace {
// Máximo de sugestões no popup: uma coleção grande faria o completer montar um popup gigante a cada tecla.
constexpr int kMaxSuggestions = 30;
// Lado (px) do botão "x" de remover e do ícone dentro dele, definidos JUNTOS: um ícone fixo de 17px num botão
// encolhido para 16px ficava maior que a área clicável e era cortado de um lado.
constexpr int kChipCloseButtonSide = 18;
constexpr int kChipCloseIconSide = 12;

// O "x" de remover da chip, pintado por nós (não QToolButton): a folha de estilo global de QToolButton e o indicador
// de menu do Fusion deixavam o ícone torto. Ícone no centro exato; hover é um círculo.
class ChipCloseButton : public QAbstractButton {
public:
    explicit ChipCloseButton(QWidget *parent)
        : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(kChipCloseButtonSide, kChipCloseButtonSide);
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor fg(utils::tokens::fg());
        if (underMouse()) {
            QColor hover = fg;
            hover.setAlpha(isDown() ? 70 : 42);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover);
            painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
        }
        const QColor iconColor = underMouse() ? fg : QColor(utils::tokens::mutedFg());
        const QRect box((width() - kChipCloseIconSide) / 2, (height() - kChipCloseIconSide) / 2,
                        kChipCloseIconSide, kChipCloseIconSide);
        LucideIcons::icon(QStringLiteral("x"), iconColor, kChipCloseIconSide).paint(&painter, box);
    }
};

// A pílula: fundo suave na cor de destaque com uma borda fina na mesma cor, rótulo e o "x". Pintada aqui (e não por
// QSS) para o raio seguir o token de cantos (AGENTS.md regra 9) e o texto/ícone ficarem centrados na mesma linha.
class ChipFrame : public QWidget {
public:
    explicit ChipFrame(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("collectionChip"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor accent(utils::tokens::accent());
        QColor fill = accent;
        fill.setAlpha(46);
        QColor border = accent;
        border.setAlpha(130);
        painter.setPen(QPen(border, 1));
        painter.setBrush(fill);
        const int radius = utils::tokens::radiusSm();
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }
};

QWidget *makeChip(QWidget *parent, const QString &label, const QString &entryId,
                   const std::function<void(const QString &)> &onRemove)
{
    auto *chip = new ChipFrame(parent);
    auto *layout = new QHBoxLayout(chip);
    // Respiro maior do lado do texto que do lado do "x" (o botão já tem o próprio círculo de hover).
    layout->setContentsMargins(utils::tokens::space(2) + 2, 2, utils::tokens::space(1) + 1, 2);
    layout->setSpacing(utils::tokens::space(1));

    auto *text = new QLabel(label, chip);
    text->setStyleSheet(QStringLiteral("QLabel { color: %1; border: none; background: transparent; }")
        .arg(utils::tokens::fg()));
    layout->addWidget(text, 0, Qt::AlignVCenter);

    auto *closeBtn = new ChipCloseButton(chip);
    closeBtn->setToolTip(utils::tr(QStringLiteral("params.collection.chip.remove")));
    QObject::connect(closeBtn, &QAbstractButton::clicked, chip, [onRemove, entryId]() { onRemove(entryId); });
    layout->addWidget(closeBtn, 0, Qt::AlignVCenter);

    return chip;
}
} // namespace

CollectionChipPickerWidget::CollectionChipPickerWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("collectionChipPicker"));
    // Sem WA_StyledBackground o border/background-color do QSS abaixo nunca pinta num QWidget puro (mesma causa de
    // rootContainer/TopUtilityBar).
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#collectionChipPicker { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(utils::tokens::bg(), utils::tokens::borderColor())
        .arg(utils::tokens::radiusMd()));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // --- Barra de busca (linha própria, acima das chips) ---
    auto *searchBar = new QWidget(this);
    searchBar->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *searchLayout = new QHBoxLayout(searchBar);
    // Margem/espaçamento zero (como o "fileInputGroup" em parameter-form-dialog.cpp): o botão fica colado no campo e
    // o border-left dele desenha o separador "pipe".
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(0);

    m_input = new QLineEdit(searchBar);
    m_input->setPlaceholderText(utils::tr(QStringLiteral("params.collection.chip.placeholder")));
    m_input->setFrame(false);
    m_input->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; color: %1; border: none; padding: %2px %3px; }")
        .arg(utils::tokens::fg())
        .arg(utils::tokens::space(1)).arg(utils::tokens::space(3)));
    connect(m_input, &QLineEdit::textEdited, this, &CollectionChipPickerWidget::updateSuggestions);
    // Backspace apaga a última chip com o campo vazio; textEdited não basta, é preciso interceptar a tecla.
    m_input->installEventFilter(this);
    searchLayout->addWidget(m_input, 1);

    m_pickButton = makeIconButton(searchBar, QStringLiteral("search"),
        utils::tr(QStringLiteral("params.collection.pick")), QColor(utils::tokens::accent()));
    // Mesmo "pipe" separador do campo de arquivo entre o texto e o botão.
    m_pickButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-left: 1px solid %1;"
        " border-radius: 0px; }")
        .arg(utils::tokens::borderColor()));
    searchLayout->addWidget(m_pickButton);
    outerLayout->addWidget(searchBar);

    // Separador entre a busca e as chips; só aparece com ao menos uma chip (rebuildChips).
    m_separator = new QFrame(this);
    m_separator->setFrameShape(QFrame::HLine);
    m_separator->setStyleSheet(QStringLiteral("QFrame { color: %1; background-color: %1; max-height: 1px; }")
        .arg(utils::tokens::borderColor()));
    m_separator->setVisible(false);
    outerLayout->addWidget(m_separator);

    // --- Área de chips: QScrollArea sem moldura (a borda é a do card externo), altura travada em ~3 linhas; rola em
    // vez de crescer o formulário ---
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    // 3 linhas: botão de remover (o elemento mais alto da chip) + padding vertical + espaçamento do flow.
    const int chipLineHeight = kChipCloseButtonSide + utils::tokens::space(1) * 2;
    m_scrollArea->setMaximumHeight(3 * chipLineHeight + 2 * utils::tokens::space(1));
    m_scrollArea->setVisible(false); // some quando não há nenhuma chip ainda

    m_flowHost = new QWidget(m_scrollArea);
    m_flowHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_flow = new FlowLayout(m_flowHost, utils::tokens::space(1),
        utils::tokens::space(1), utils::tokens::space(1));
    m_scrollArea->setWidget(m_flowHost);
    outerLayout->addWidget(m_scrollArea);

    m_completerModel = new QStandardItemModel(this);
    m_completer = new QCompleter(m_completerModel, this);
    // UnfilteredPopupCompletion: nós decidimos o modelo a cada tecla (fuzzy, ver updateSuggestions); o completer só
    // cuida do popup/navegação.
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_input->setCompleter(m_completer);
    // O popup do QCompleter é uma janela top-level (Qt::Popup) fora da cascata de QSS do app (cairia no render
    // nativo): estiliza localmente com o mesmo critério do popup do QComboBox (surface2/border/radiusMd).
    m_completer->popup()->setStyleSheet(QStringLiteral(
        "QAbstractItemView { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; outline: none; padding: %5px; }"
        "QAbstractItemView::item { padding: %6px %7px; border-radius: %8px; }"
        "QAbstractItemView::item:selected { background-color: %9; color: %2; }")
        .arg(utils::tokens::surface2(), utils::tokens::fg(), utils::tokens::borderColor())
        .arg(utils::tokens::radiusMd()).arg(utils::tokens::space(1))
        .arg(utils::tokens::space(1)).arg(utils::tokens::space(2)).arg(utils::tokens::radiusSm())
        .arg(utils::tokens::selBg()));
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated), this,
        [this](const QModelIndex &index) {
            const QString entryId = index.data(Qt::UserRole).toString();
            if (entryId.isEmpty()) {
                return;
            }
            addChip(entryId);
            // QLineEdit::setCompleter() liga internamente activated() para escrever a sugestão de volta no campo, e
            // essa ligação roda DEPOIS da nossa: um clear() síncrono seria sobrescrito. Agenda para o próximo ciclo.
            QTimer::singleShot(0, this, [this]() { m_input->clear(); });
            emit selectionChanged();
        });
}

bool CollectionChipPickerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Backspace && m_input->text().isEmpty() && !m_selectedIds.isEmpty()) {
            removeEntry(m_selectedIds.last());
            return true; // consumido — não deixa o QCompleter/QLineEdit reagir
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CollectionChipPickerWidget::setEntries(const QVector<core::CollectionEntry> &entries,
                                             const QString &displayField)
{
    m_entries = entries;
    m_displayField = displayField;
}

QString CollectionChipPickerWidget::labelFor(const QString &entryId) const
{
    const auto it = std::find_if(m_entries.constBegin(), m_entries.constEnd(),
        [&entryId](const core::CollectionEntry &e) { return e.id == entryId; });
    if (it == m_entries.constEnd()) {
        return entryId;
    }
    const QString lbl = it->values.value(m_displayField);
    return lbl.isEmpty() ? it->id : lbl;
}

void CollectionChipPickerWidget::setSelectedIds(const QStringList &ids)
{
    m_selectedIds.clear();
    for (const QString &id : ids) {
        if (!m_selectedIds.contains(id)) {
            m_selectedIds << id;
        }
    }
    rebuildChips();
}

void CollectionChipPickerWidget::addChip(const QString &entryId)
{
    if (m_selectedIds.contains(entryId)) {
        return;
    }
    m_selectedIds << entryId;
    rebuildChips();
}

void CollectionChipPickerWidget::removeEntry(const QString &entryId)
{
    m_selectedIds.removeAll(entryId);
    rebuildChips();
    emit selectionChanged();
}

void CollectionChipPickerWidget::rebuildChips()
{
    // m_flow só contém chips (a busca tem barra própria): remover e reconstruir tudo é seguro.
    while (m_flow->count() > 0) {
        QLayoutItem *item = m_flow->takeAt(0);
        delete item->widget();
        delete item;
    }
    for (const QString &id : m_selectedIds) {
        m_flow->addWidget(makeChip(m_flowHost, labelFor(id), id,
            [this](const QString &removedId) { removeEntry(removedId); }));
    }
    const bool hasChips = !m_selectedIds.isEmpty();
    m_scrollArea->setVisible(hasChips);
    m_separator->setVisible(hasChips);
}

void CollectionChipPickerWidget::updateSuggestions(const QString &query)
{
    m_completerModel->clear();
    if (query.trimmed().isEmpty()) {
        m_completer->popup()->hide();
        return;
    }

    // Score por entrada: o melhor entre todos os campos (mesmo critério do CollectionSelectorDialog).
    struct ScoredEntry { QString id; int score; };
    QVector<ScoredEntry> scored;
    for (const core::CollectionEntry &e : m_entries) {
        if (m_selectedIds.contains(e.id)) {
            continue; // já escolhida — não sugere de novo
        }
        int best = -1;
        for (auto it = e.values.constBegin(); it != e.values.constEnd(); ++it) {
            best = qMax(best, FuzzyMatcher::score(query, it.value()));
        }
        if (best >= 0) {
            scored << ScoredEntry{e.id, best};
        }
    }
    std::stable_sort(scored.begin(), scored.end(),
        [](const ScoredEntry &a, const ScoredEntry &b) { return a.score > b.score; });

    for (int i = 0; i < scored.size() && i < kMaxSuggestions; ++i) {
        const QString &entryId = scored.at(i).id;
        auto *item = new QStandardItem(labelFor(entryId));
        item->setData(entryId, Qt::UserRole);
        m_completerModel->appendRow(item);
    }
    if (m_completerModel->rowCount() > 0) {
        // O popup do QCompleter nasce com a largura do QLineEdit (m_input), que divide a barra com a lupa; complete(QRect)
        // alinha a esquerda e a largura com o card inteiro (this), origem em m_input.
        const QPoint inputOriginInCard = m_input->mapTo(this, QPoint(0, 0));
        const QRect popupRect(-inputOriginInCard.x(), m_input->height(), width(), 0);
        m_completer->complete(popupRect);
    } else {
        m_completer->popup()->hide();
    }
}

} // namespace kai::ui
