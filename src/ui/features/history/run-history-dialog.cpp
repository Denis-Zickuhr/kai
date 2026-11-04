#include "ui/features/history/run-history-dialog.h"
#include "ui/shared/token-style.h"

#include <QApplication>
#include <QClipboard>
#include <QDate>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/shared/adaptive-button.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/search-line-edit.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

enum Roles { RecordRole = Qt::UserRole, TitleRole, MetaRole, SuccessRole, HeaderRole };

constexpr int kRowHeight = 52;
constexpr int kHeaderHeight = 28;
// Abaixo disto o painel empilha a lista sobre o detalhe (um painel lateral estreito).
constexpr int kStackBelowWidth = 560;

// "1.4 s", "230 ms", "2 min 5 s": curto, sem tradução (as unidades são as mesmas em qualquer idioma).
QString durationText(qint64 ms)
{
    if (ms < 1000) {
        return QStringLiteral("%1 ms").arg(ms);
    }
    if (ms < 60000) {
        return QStringLiteral("%1 s").arg(QString::number(ms / 1000.0, 'f', ms < 10000 ? 1 : 0));
    }
    return QStringLiteral("%1 min %2 s").arg(ms / 60000).arg((ms / 1000) % 60);
}

// O que a saída guardada mostra: sem as sequências de cor/cursor (ANSI), que viram lixo num campo de texto.
QString plainOutput(const QString &raw)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;?]*[ -/]*[@-~]|\x1b\\][^\x07]*\x07"));
    QString text = raw;
    text.remove(ansi);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.remove(QLatin1Char('\r'));
    return text;
}

QString dayLabel(const QDate &day)
{
    const QDate today = QDate::currentDate();
    if (day == today) {
        return utils::tr(QStringLiteral("runs.group.today"));
    }
    if (day == today.addDays(-1)) {
        return utils::tr(QStringLiteral("runs.group.yesterday"));
    }
    return day.toString(QStringLiteral("dd/MM/yyyy"));
}

// Uma linha da lista: bolinha do resultado, nome do comando e, embaixo, tipo · hora · duração. Os cabeçalhos de dia são
// só um texto pequeno em maiúsculas.
class RunItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        return QSize(100, index.data(RecordRole).toInt() < 0 ? kHeaderHeight : kRowHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRect rect = option.rect;
        if (index.data(RecordRole).toInt() < 0) {
            QFont font = option.font;
            font.setPointSizeF(font.pointSizeF() * 0.85);
            font.setBold(true);
            font.setCapitalization(QFont::AllUppercase);
            font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
            painter->setFont(font);
            painter->setPen(QColor(tk::mutedFg()));
            painter->drawText(rect.adjusted(tk::space(3), tk::space(1), -tk::space(2), 0), Qt::AlignLeft | Qt::AlignVCenter,
                              index.data(HeaderRole).toString());
            painter->restore();
            return;
        }
        const QRect box = rect.adjusted(tk::space(1), 2, -tk::space(1), -2);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        if (selected || hover) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            painter->drawRoundedRect(box, tk::radiusMd(), tk::radiusMd());
        }
        const bool success = index.data(SuccessRole).toBool();
        const QColor mark(success ? tk::successFg() : tk::errorFg());
        const int dot = 8;
        const QPoint dotCenter(box.left() + tk::space(3), box.center().y());
        painter->setPen(Qt::NoPen);
        painter->setBrush(mark);
        painter->drawEllipse(dotCenter, dot / 2, dot / 2);
        const int textLeft = dotCenter.x() + dot + tk::space(2);
        const int textWidth = box.right() - tk::space(2) - textLeft;
        QFont title = option.font;
        title.setBold(true);
        painter->setFont(title);
        painter->setPen(QColor(tk::fg()));
        const QFontMetrics titleMetrics(title);
        const QRect titleRect(textLeft, box.top() + tk::space(1) + 1, textWidth, titleMetrics.height());
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          titleMetrics.elidedText(index.data(TitleRole).toString(), Qt::ElideRight, textWidth));
        QFont meta = option.font;
        meta.setPointSizeF(meta.pointSizeF() * 0.9);
        painter->setFont(meta);
        painter->setPen(QColor(tk::mutedFg()));
        const QFontMetrics metaMetrics(meta);
        const QRect metaRect(textLeft, titleRect.bottom() + 2, textWidth, metaMetrics.height());
        painter->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                          metaMetrics.elidedText(index.data(MetaRole).toString(), Qt::ElideRight, textWidth));
        painter->restore();
    }
};

QPushButton *makeSegment(QWidget *parent, const QString &text)
{
    auto *button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

RunHistoryDialog::RunHistoryDialog(core::RunHistory *history, QWidget *parent, bool embedded)
    : QDialog(parent)
    , m_history(history)
{
    if (embedded) {
        setWindowFlags(Qt::Widget);
    }
    setWindowTitle(utils::tr(QStringLiteral("runs.title")));
    setupUi();
    reload();
    if (embedded) {
        setEmbedded(true);
        return;
    }
    resize(900, 560);
    centerOnParent(this);
}

void RunHistoryDialog::setupUi()
{
    setObjectName(QStringLiteral("runHistory"));
    auto *outer = new QVBoxLayout(this);
    outer->setSpacing(tk::space(2));

    auto *top = new QHBoxLayout();
    top->setSpacing(tk::space(2));
    m_search = new SearchLineEdit(this);
    m_search->setObjectName(QStringLiteral("runSearch"));
    m_search->setPlaceholderText(utils::tr(QStringLiteral("runs.search")));
    bindTokenStyle(m_search, [=, this]() { return QStringLiteral("QLineEdit { padding: 0px; border-radius: %1px; }").arg(tk::radiusMd()); });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuildList(); });
    top->addWidget(m_search, 1);
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("runCount"));
    top->addWidget(m_count);
    m_clearButton = new AdaptiveButton(utils::tr(QStringLiteral("runs.action.clear")), this);
    m_clearButton->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::mutedFg()), 16));
    m_clearButton->setCursor(Qt::PointingHandCursor);
    connect(m_clearButton, &QPushButton::clicked, this, &RunHistoryDialog::handleClear);
    top->addWidget(m_clearButton);
    outer->addLayout(top);

    auto *segmentRow = new QHBoxLayout();
    auto *segmented = new QWidget(this);
    segmented->setObjectName(QStringLiteral("runSegments"));
    segmented->setAttribute(Qt::WA_StyledBackground, true);
    auto *segmentLayout = new QHBoxLayout(segmented);
    segmentLayout->setContentsMargins(2, 2, 2, 2);
    segmentLayout->setSpacing(2);
    const QStringList labels = {utils::tr(QStringLiteral("runs.filter.all")), utils::tr(QStringLiteral("runs.filter.failed")),
                                utils::tr(QStringLiteral("runs.filter.succeeded"))};
    for (int i = 0; i < labels.size(); ++i) {
        QPushButton *button = makeSegment(segmented, labels.at(i));
        button->setChecked(i == 0);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            m_filter = static_cast<Filter>(i);
            for (int k = 0; k < m_filters.size(); ++k) {
                m_filters.at(k)->setChecked(k == i);
            }
            rebuildList();
        });
        m_filters.append(button);
        segmentLayout->addWidget(button);
    }
    segmentRow->addWidget(segmented);
    segmentRow->addStretch(1);
    outer->addLayout(segmentRow);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setObjectName(QStringLiteral("runSplitter"));

    auto *listCard = new QWidget(m_splitter);
    listCard->setObjectName(QStringLiteral("runListCard"));
    listCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(0, 0, 0, 0);
    m_listPages = new QStackedWidget(listCard);
    m_list = new QListWidget(m_listPages);
    m_list->setObjectName(QStringLiteral("runList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setItemDelegate(new RunItemDelegate(m_list));
    m_list->setMinimumWidth(220);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { handleSelectionChanged(); });
    m_listPages->addWidget(m_list);
    m_listEmpty = new QLabel(m_listPages);
    m_listEmpty->setObjectName(QStringLiteral("runListEmpty"));
    m_listEmpty->setAlignment(Qt::AlignCenter);
    m_listEmpty->setWordWrap(true);
    m_listPages->addWidget(m_listEmpty);
    listLayout->addWidget(m_listPages);
    m_splitter->addWidget(listCard);

    auto *detailCard = new QWidget(m_splitter);
    detailCard->setObjectName(QStringLiteral("runDetailCard"));
    detailCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *detailLayout = new QVBoxLayout(detailCard);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    m_detailPages = new QStackedWidget(detailCard);
    m_detailEmpty = new QLabel(m_detailPages);
    m_detailEmpty->setObjectName(QStringLiteral("runDetailEmpty"));
    m_detailEmpty->setAlignment(Qt::AlignCenter);
    m_detailEmpty->setWordWrap(true);
    m_detailPages->addWidget(m_detailEmpty);

    auto *detail = new QWidget(m_detailPages);
    detail->setObjectName(QStringLiteral("runDetail"));
    auto *dl = new QVBoxLayout(detail);
    dl->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    dl->setSpacing(tk::space(2));
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(tk::space(2));
    m_title = new QLabel(detail);
    m_title->setObjectName(QStringLiteral("runTitle"));
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleRow->addWidget(m_title, 1);
    m_status = new QLabel(detail);
    m_status->setObjectName(QStringLiteral("runStatus"));
    m_status->setAlignment(Qt::AlignCenter);
    titleRow->addWidget(m_status, 0, Qt::AlignTop);
    dl->addLayout(titleRow);
    m_meta = new QLabel(detail);
    m_meta->setObjectName(QStringLiteral("runMeta"));
    m_meta->setWordWrap(true);
    dl->addWidget(m_meta);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(tk::space(2));
    m_rerun = new AdaptiveButton(utils::tr(QStringLiteral("runs.action.rerun")), detail);
    m_rerun->setIcon(LucideIcons::icon(QStringLiteral("play"), QColor(tk::buttonFg()), 16));
    m_rerun->setProperty("kaiRole", QStringLiteral("primary"));
    m_rerun->setCursor(Qt::PointingHandCursor);
    connect(m_rerun, &QPushButton::clicked, this, &RunHistoryDialog::handleRerun);
    m_copy = new AdaptiveButton(utils::tr(QStringLiteral("runs.action.copy")), detail);
    m_copy->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 16));
    m_copy->setCursor(Qt::PointingHandCursor);
    connect(m_copy, &QPushButton::clicked, this, &RunHistoryDialog::handleCopy);
    m_wrap = new QToolButton(detail);
    m_wrap->setObjectName(QStringLiteral("runWrap"));
    m_wrap->setCheckable(true);
    m_wrap->setChecked(true);
    m_wrap->setText(utils::tr(QStringLiteral("runs.action.wrap")));
    m_wrap->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_rerun);
    actions->addWidget(m_copy);
    actions->addStretch(1);
    actions->addWidget(m_wrap);
    dl->addLayout(actions);

    m_output = new QPlainTextEdit(detail);
    m_output->setObjectName(QStringLiteral("runOutput"));
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_output->setPlaceholderText(utils::tr(QStringLiteral("runs.no_output")));
    m_output->setFrameShape(QFrame::NoFrame);
    connect(m_wrap, &QToolButton::toggled, this, [this](bool on) {
        m_output->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });
    dl->addWidget(m_output, 1);
    m_detailPages->addWidget(detail);
    detailLayout->addWidget(m_detailPages);
    m_splitter->addWidget(detailCard);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({320, 560});
    outer->addWidget(m_splitter, 1);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(m_buttonBox);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(m_buttonBox);

    applyStyle();
}

// Os cartões, o segmentado e os rótulos seguem os tokens (cores e raio da preferência de cantos): nunca um raio fixo.
void RunHistoryDialog::applyStyle()
{
    const int radius = tk::radiusMd();
    const int small = tk::radiusSm();
    const QString card = QStringLiteral("QWidget#%1 { background-color: %2; border: 1px solid %3; border-radius: %4px; }");
    bindTokenStyle(this, [=, this]() { return 
        card.arg(QStringLiteral("runListCard"), tk::surface(), tk::borderColor()).arg(radius)
        + card.arg(QStringLiteral("runDetailCard"), tk::surface(), tk::borderColor()).arg(radius)
        + QStringLiteral("QListWidget#runList { background: transparent; border: none; outline: none; padding: %1px; }")
              .arg(tk::space(1))
        + QStringLiteral("QWidget#runDetail { background: transparent; }")
        + QStringLiteral("QLabel#runTitle { background: transparent; color: %1; font-size: %2pt; font-weight: 700; }")
              .arg(tk::fg()).arg(tk::fontSizeTitlePt())
        + QStringLiteral("QLabel#runMeta, QLabel#runCount { background: transparent; color: %1; font-size: %2pt; }")
              .arg(tk::mutedFg()).arg(tk::fontSizeSmallPt())
        + QStringLiteral("QLabel#runListEmpty, QLabel#runDetailEmpty { background: transparent; color: %1; padding: %2px; }")
              .arg(tk::mutedFg()).arg(tk::space(4))
        + QStringLiteral("QLabel#runStatus { border-radius: %1px; padding: 2px %2px; font-weight: 600; font-size: %3pt; }")
              .arg(small).arg(tk::space(2)).arg(tk::fontSizeSmallPt())
        + QStringLiteral("QPlainTextEdit#runOutput { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
                         " padding: %5px; selection-background-color: %6; }")
              .arg(tk::terminalBg(), tk::terminalFg(), tk::borderColor()).arg(small).arg(tk::space(2)).arg(tk::selBg())
        + QStringLiteral("QWidget#runSegments { background-color: %1; border-radius: %2px; }").arg(tk::bg()).arg(radius)
        + QStringLiteral("QWidget#runSegments QPushButton { border: none; border-radius: %1px; padding: %2px %3px; font-weight: 600;"
                         " background: transparent; color: %4; min-height: 0px; }"
                         "QWidget#runSegments QPushButton:checked { background-color: %5; color: %6; }"
                         "QWidget#runSegments QPushButton:hover:!checked { background-color: %7; }")
              .arg(small).arg(tk::space(1)).arg(tk::space(3)).arg(tk::mutedFg(), tk::accent(), tk::bg(), tk::hoverBg())
        + QStringLiteral("QToolButton#runWrap { border: 1px solid %1; border-radius: %2px; padding: 3px %3px; color: %4;"
                         " background: transparent; }"
                         "QToolButton#runWrap:checked { background-color: %5; color: %6; }")
              .arg(tk::borderColor()).arg(small).arg(tk::space(2)).arg(tk::mutedFg(), tk::selBg(), tk::fg()); });
    QFont mono(tk::monoFamily());
    mono.setPointSizeF(font().pointSizeF());
    m_output->setFont(mono);
    m_clearButton->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::mutedFg()), 16));
}

void RunHistoryDialog::setEmbedded(bool embedded)
{
    m_embedded = embedded;
    setWindowFlags(embedded ? Qt::Widget : Qt::Dialog);
    setProperty("kaiEmbedded", embedded);
    m_buttonBox->setVisible(!embedded);
    layout()->setContentsMargins(embedded ? tk::space(2) : tk::space(4), embedded ? tk::space(2) : tk::space(4),
                                 embedded ? tk::space(2) : tk::space(4), embedded ? tk::space(2) : tk::space(4));
    m_list->setMinimumWidth(embedded ? 180 : 220);
    updateOrientation();
}

void RunHistoryDialog::done(int result)
{
    if (!m_embedded) {
        QDialog::done(result);
    }
}

// Esc fecha um diálogo, mas não um painel do quadro: quem fecha painel é o menu Exibir ou o menu da aba.
void RunHistoryDialog::keyPressEvent(QKeyEvent *event)
{
    if (m_embedded && event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

void RunHistoryDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    applyStyle();
    updateOrientation();
    if (m_embedded) {
        reload(); // o painel fica aberto enquanto novas execuções acontecem
    }
}

void RunHistoryDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateOrientation();
}

bool RunHistoryDialog::stacked() const
{
    return m_splitter->orientation() == Qt::Vertical;
}

// Painel lateral estreito: a lista em cima e o detalhe embaixo, em vez de dois pedaços ilegíveis lado a lado.
void RunHistoryDialog::updateOrientation()
{
    const Qt::Orientation wanted = width() < kStackBelowWidth ? Qt::Vertical : Qt::Horizontal;
    if (m_splitter->orientation() != wanted) {
        m_splitter->setOrientation(wanted);
        m_splitter->setSizes(wanted == Qt::Vertical ? QList<int>{220, 360} : QList<int>{320, 560});
    }
}

void RunHistoryDialog::reload()
{
    m_records = m_history ? m_history->load() : QVector<core::RunRecord>();
    rebuildList();
}

// Refaz a lista com a busca e o filtro de agora, agrupada por dia (do mais novo ao mais antigo), e mantém o run escolhido
// quando ele continua à vista.
void RunHistoryDialog::rebuildList()
{
    const QString wanted = m_current >= 0 && m_current < m_records.size() ? m_records.at(m_current).id : QString();
    const QString needle = m_search->text().trimmed().toLower();
    QSignalBlocker blocker(m_list);
    m_list->clear();
    int shown = 0;
    int keepRow = -1;
    QDate lastDay;
    for (int i = 0; i < m_records.size(); ++i) {
        const core::RunRecord &r = m_records.at(i);
        if ((m_filter == Filter::Failed && r.success) || (m_filter == Filter::Succeeded && !r.success)) {
            continue;
        }
        if (!needle.isEmpty() && !r.commandName.toLower().contains(needle) && !r.commandType.toLower().contains(needle)
            && !r.output.toLower().contains(needle)) {
            continue;
        }
        const QDate day = r.startedAt.date();
        if (day != lastDay) {
            lastDay = day;
            auto *header = new QListWidgetItem(m_list);
            header->setData(RecordRole, -1);
            header->setData(HeaderRole, dayLabel(day));
            header->setFlags(Qt::NoItemFlags);
        }
        auto *item = new QListWidgetItem(m_list);
        item->setData(RecordRole, i);
        item->setData(TitleRole, r.commandName);
        item->setData(SuccessRole, r.success);
        item->setData(MetaRole, QStringLiteral("%1  ·  %2  ·  %3")
                                    .arg(r.commandType.toUpper(), r.startedAt.toString(QStringLiteral("HH:mm:ss")),
                                         durationText(r.durationMs)));
        if (r.id == wanted) {
            keepRow = m_list->count() - 1;
        }
        ++shown;
    }
    blocker.unblock();

    m_count->setText(utils::tr(QStringLiteral("runs.count")).arg(shown));
    m_clearButton->setEnabled(!m_records.isEmpty());
    if (shown == 0) {
        m_listEmpty->setText(utils::tr(m_records.isEmpty() ? QStringLiteral("runs.empty.none")
                                                            : QStringLiteral("runs.empty.filtered")));
        m_listPages->setCurrentWidget(m_listEmpty);
        showDetail(-1);
        return;
    }
    m_listPages->setCurrentWidget(m_list);
    int row = keepRow;
    if (row < 0) {
        for (int k = 0; k < m_list->count(); ++k) { // o primeiro run (pula o cabeçalho do dia)
            if (m_list->item(k)->data(RecordRole).toInt() >= 0) {
                row = k;
                break;
            }
        }
    }
    m_list->setCurrentRow(row);
    handleSelectionChanged();
}

void RunHistoryDialog::handleSelectionChanged()
{
    const QListWidgetItem *item = m_list->currentItem();
    showDetail(item ? item->data(RecordRole).toInt() : -1);
}

void RunHistoryDialog::showDetail(int recordIndex)
{
    m_current = recordIndex >= 0 && recordIndex < m_records.size() ? recordIndex : -1;
    if (m_current < 0) {
        m_detailEmpty->setText(utils::tr(QStringLiteral("runs.detail.empty")));
        m_detailPages->setCurrentWidget(m_detailEmpty);
        return;
    }
    const core::RunRecord &r = m_records.at(m_current);
    m_title->setText(r.commandName);
    const QColor mark(r.success ? tk::successFg() : tk::errorFg());
    m_status->setText(utils::tr(r.success ? QStringLiteral("runs.success") : QStringLiteral("runs.failed")));
    bindTokenStyle(m_status, [=, this]() { return QStringLiteral("QLabel#runStatus { background-color: rgba(%1,%2,%3,40); color: %4;"
                                           " border-radius: %5px; padding: 2px %6px; font-weight: 600; font-size: %7pt; }")
                                .arg(mark.red()).arg(mark.green()).arg(mark.blue()).arg(mark.name()).arg(tk::radiusSm())
                                .arg(tk::space(2)).arg(tk::fontSizeSmallPt()); });
    m_meta->setText(QStringLiteral("%1  ·  %2  ·  %3")
                        .arg(r.commandType.toUpper(), r.startedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")),
                             durationText(r.durationMs)));
    m_output->setPlainText(plainOutput(r.output));
    m_output->moveCursor(QTextCursor::Start);
    m_rerun->setEnabled(!r.commandId.isEmpty());
    m_copy->setEnabled(!r.output.isEmpty());
    m_detailPages->setCurrentIndex(1);
}

int RunHistoryDialog::visibleRunCount() const
{
    int count = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        count += m_list->item(i)->data(RecordRole).toInt() >= 0 ? 1 : 0;
    }
    return count;
}

QString RunHistoryDialog::detailTitle() const
{
    return m_current >= 0 ? m_title->text() : QString();
}

QString RunHistoryDialog::detailOutput() const
{
    return m_current >= 0 ? m_output->toPlainText() : QString();
}

void RunHistoryDialog::handleRerun()
{
    if (m_current < 0 || m_current >= m_records.size()) {
        return;
    }
    emit rerunRequested(m_records.at(m_current).commandId);
    if (!m_embedded) {
        accept();
    }
}

void RunHistoryDialog::handleCopy()
{
    if (m_current < 0 || m_current >= m_records.size()) {
        return;
    }
    QApplication::clipboard()->setText(plainOutput(m_records.at(m_current).output));
    m_copy->setText(utils::tr(QStringLiteral("runs.copied")));
    QTimer::singleShot(1500, this, [this]() { m_copy->setText(utils::tr(QStringLiteral("runs.action.copy"))); });
}

void RunHistoryDialog::handleClear()
{
    if (m_records.isEmpty()) {
        return;
    }
    if (!confirmYesNo(this, utils::tr(QStringLiteral("runs.action.clear")), utils::tr(QStringLiteral("runs.clear.confirm")))) {
        return;
    }
    if (m_history) {
        m_history->clear();
    }
    reload();
}

} // namespace kai::ui
