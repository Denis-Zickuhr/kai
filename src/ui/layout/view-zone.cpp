#include "ui/layout/view-zone.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"

#include <QApplication>
#include <QCursor>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFontMetrics>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace kai::ui::layout {

namespace tk = kai::utils::tokens;

namespace {
void setCardGlue(QWidget *root, bool glued); // definida mais abaixo
} // namespace

namespace {
constexpr int kTabPadding = 10;
constexpr int kIconSize = 14;
constexpr int kGap = 6;
constexpr int kCloseSize = 14;
constexpr int kMinTabWidth = 72;
constexpr int kMenuWidth = 38;
} // namespace

// ------------------------------------------------------------------------------------------------ ZoneTabStrip

ZoneTabStrip::ZoneTabStrip(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("zoneTabStrip"));
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(m_barHeight);
}

void ZoneTabStrip::setBarHeight(int height)
{
    m_barHeight = qMax(22, height);
    setFixedHeight(m_barHeight);
    relayout();
    update();
}

void ZoneTabStrip::setZoneFocused(bool focused)
{
    if (m_zoneFocused != focused) {
        m_zoneFocused = focused;
        update();
    }
}

void ZoneTabStrip::setTabs(const QList<Tab> &tabs, const QString &active)
{
    m_tabs = tabs;
    m_active = active;
    m_hover = -1;
    relayout();
    update();
}

QSize ZoneTabStrip::sizeHint() const
{
    return QSize(200, m_barHeight);
}

QSize ZoneTabStrip::minimumSizeHint() const
{
    return QSize(kMenuWidth + kMinTabWidth, m_barHeight);
}

void ZoneTabStrip::relayout()
{
    m_rects.clear();
    if (m_tabs.isEmpty()) {
        return;
    }
    const QFontMetrics metrics = fontMetrics();
    QList<int> widths;
    int total = 0;
    for (const Tab &tab : std::as_const(m_tabs)) {
        // +4: horizontalAdvance arredonda para baixo e o elidedText compara com a largura fracionária ("Notificatio…").
        const int w = kTabPadding * 2 + kIconSize + kGap + metrics.horizontalAdvance(tab.title) + 4
                      + (tab.closable ? kGap + kCloseSize : 0);
        widths.append(w);
        total += w;
    }
    const int available = qMax(kMinTabWidth, width() - kMenuWidth - 4);
    if (total > available) {
        // Aperta as abas proporcionalmente, sem passar do mínimo.
        const double scale = double(available) / double(total);
        for (int &w : widths) {
            w = qMax(kMinTabWidth, int(w * scale));
        }
    }
    int x = 2;
    for (int w : std::as_const(widths)) {
        m_rects.append(QRect(x, 2, w, m_barHeight - 2));
        x += w;
    }
}

QRect ZoneTabStrip::tabRect(int index) const
{
    return index >= 0 && index < m_rects.size() ? m_rects.at(index) : QRect();
}

QRect ZoneTabStrip::closeRect(int index) const
{
    if (index < 0 || index >= m_tabs.size() || !m_tabs.at(index).closable) {
        return QRect();
    }
    const QRect tab = m_rects.at(index);
    return QRect(tab.right() - kTabPadding - kCloseSize + 1, tab.top() + (tab.height() - kCloseSize) / 2, kCloseSize,
                 kCloseSize);
}

QRect ZoneTabStrip::menuRect() const
{
    return QRect(width() - kMenuWidth, 0, kMenuWidth, m_barHeight);
}

int ZoneTabStrip::indexAt(const QPoint &pos) const
{
    for (int i = 0; i < m_rects.size(); ++i) {
        if (m_rects.at(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

int ZoneTabStrip::insertionIndexAt(int x) const
{
    for (int i = 0; i < m_rects.size(); ++i) {
        if (x < m_rects.at(i).center().x()) {
            return i;
        }
    }
    return int(m_rects.size());
}

void ZoneTabStrip::paintEvent(QPaintEvent *)
{
    // A fonte final só chega depois do polimento do QSS (maior que a do construtor): as larguras saem sempre da fonte
    // com que se desenha, senão os títulos viram "Comm…" com a faixa quase vazia.
    relayout();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // A faixa é a borda de cima do painel que está logo abaixo: cantos de cima pelo raio escolhido (os mesmos dos cartões),
    // borda em cima e dos lados, e a linha de baixo separa dela o painel colado.
    const double radius = qMin(tk::radiusMd(), height() / 2);
    p.setClipRect(rect());
    p.setPen(QPen(QColor(tk::borderColor()), 1));
    p.setBrush(QColor(tk::altBg()));
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() + radius), radius, radius);
    p.drawLine(QPointF(0.5, height() - 0.5), QPointF(width() - 0.5, height() - 0.5));

    const QFontMetrics metrics = fontMetrics();
    for (int i = 0; i < m_tabs.size(); ++i) {
        const Tab &tab = m_tabs.at(i);
        const QRect r = m_rects.at(i);
        const bool active = tab.id == m_active;
        const bool hover = i == m_hover;
        if (hover && !active) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(tk::hoverBg()));
            p.drawRoundedRect(r.adjusted(1, 3, -1, -3), tk::radiusSm(), tk::radiusSm());
        }
        const QColor fg(active ? tk::fg() : tk::mutedFg());
        const QIcon icon = LucideIcons::icon(tab.icon, fg, kIconSize);
        if (!icon.isNull()) {
            icon.paint(&p, QRect(r.left() + kTabPadding, r.top() + (r.height() - kIconSize) / 2, kIconSize, kIconSize));
        }
        const int textLeft = r.left() + kTabPadding + kIconSize + kGap;
        const int textRight = r.left() + r.width() - kTabPadding - (tab.closable ? kGap + kCloseSize : 0);
        p.setPen(fg);
        p.drawText(QRect(textLeft, r.top(), qMax(0, textRight - textLeft), r.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(tab.title, Qt::ElideRight, textRight - textLeft));
        if (tab.closable && (active || hover)) {
            const QRect c = closeRect(i);
            if (i == m_hover && m_hoverClose) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(tk::selBg()));
                p.drawRoundedRect(c.adjusted(-2, -2, 2, 2), tk::radiusSm(), tk::radiusSm());
            }
            const QIcon x = LucideIcons::icon(QStringLiteral("x"), QColor(tk::mutedFg()), kCloseSize - 2);
            if (!x.isNull()) {
                x.paint(&p, c.adjusted(1, 1, -1, -1));
            }
        }
        if (active) {
            // O destaque forte só na zona que tem o foco; nas outras a aba da frente leva um sublinhado discreto.
            QColor underline(m_zoneFocused ? tk::accent() : tk::mutedFg());
            if (!m_zoneFocused) {
                underline.setAlpha(110);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(underline);
            p.drawRect(QRect(r.left() + 2, height() - 3, r.width() - 4, 2));
        }
    }

    // O "mais" (três pontos): menu do painel da frente.
    const QRect m = menuRect();
    p.setPen(Qt::NoPen);
    if (m_hoverMenu) {
        p.setBrush(QColor(tk::hoverBg()));
        p.drawRoundedRect(m.adjusted(5, 5, -9, -5), tk::radiusSm(), tk::radiusSm());
    }
    p.setBrush(QColor(tk::mutedFg()));
    const int cy = m.center().y();
    for (int dx : {-5, 0, 5}) {
        p.drawEllipse(QPointF(m.center().x() + dx, cy), 1.4, 1.4);
    }
}

void ZoneTabStrip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressIndex = indexAt(event->pos());
    m_pressPos = event->pos();
    m_pressOnClose = m_pressIndex >= 0 && closeRect(m_pressIndex).adjusted(-3, -3, 3, 3).contains(event->pos());
    if (m_pressIndex >= 0 && !m_pressOnClose) {
        // A faixa mostra já a aba clicada (quem troca o painel é o ViewZone, em resposta ao sinal).
        m_active = m_tabs.at(m_pressIndex).id;
        update();
        emit tabActivated(m_active);
    }
}

void ZoneTabStrip::mouseMoveEvent(QMouseEvent *event)
{
    const int index = indexAt(event->pos());
    const bool onClose = index >= 0 && closeRect(index).adjusted(-3, -3, 3, 3).contains(event->pos());
    const bool onMenu = menuRect().contains(event->pos());
    if (index != m_hover || onClose != m_hoverClose || onMenu != m_hoverMenu) {
        m_hover = index;
        m_hoverClose = onClose;
        m_hoverMenu = onMenu;
        update();
    }
    if ((event->buttons() & Qt::LeftButton) && m_pressIndex >= 0 && !m_pressOnClose
        && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        const int index = m_pressIndex;
        m_pressIndex = -1;
        startDrag(index);
    }
}

void ZoneTabStrip::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (menuRect().contains(event->pos()) && m_pressIndex < 0 && !m_active.isEmpty()) {
            emit tabMenuRequested(m_active, mapToGlobal(QPoint(menuRect().left(), menuRect().bottom())));
        } else if (m_pressIndex >= 0 && m_pressOnClose && closeRect(m_pressIndex).adjusted(-3, -3, 3, 3).contains(event->pos())) {
            emit tabCloseRequested(m_tabs.at(m_pressIndex).id);
        }
    }
    m_pressIndex = -1;
    m_pressOnClose = false;
}

void ZoneTabStrip::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int index = indexAt(event->pos());
    if (event->button() == Qt::LeftButton && index >= 0 && !closeRect(index).contains(event->pos())) {
        emit tabDoubleClicked(m_tabs.at(index).id);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ZoneTabStrip::leaveEvent(QEvent *event)
{
    if (m_hover != -1 || m_hoverMenu) {
        m_hover = -1;
        m_hoverClose = false;
        m_hoverMenu = false;
        update();
    }
    QWidget::leaveEvent(event);
}

void ZoneTabStrip::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = indexAt(event->pos());
    const QString id = index >= 0 ? m_tabs.at(index).id : m_active;
    if (!id.isEmpty()) {
        emit tabMenuRequested(id, event->globalPos());
    }
}

void ZoneTabStrip::resizeEvent(QResizeEvent *event)
{
    relayout();
    QWidget::resizeEvent(event);
}

void ZoneTabStrip::startDrag(int index)
{
    const Tab tab = m_tabs.at(index);
    auto *mime = new QMimeData;
    mime->setData(kViewMimeType, tab.id.toUtf8());
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    QPixmap pixmap(m_rects.at(index).size() * devicePixelRatioF());
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    pixmap.fill(Qt::transparent);
    render(&pixmap, QPoint(), QRegion(m_rects.at(index)));
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(pixmap.width() / int(2 * devicePixelRatioF()), pixmap.height() / int(2 * devicePixelRatioF())));
    emit dragStarted(tab.id);
    const Qt::DropAction action = drag->exec(Qt::MoveAction);
    emit dragFinished(tab.id, action, QCursor::pos());
}

// ------------------------------------------------------------------------------------------------------ ZoneStack

bool ZoneStack::event(QEvent *event)
{
    const bool handled = QStackedWidget::event(event);
    if (event->type() == QEvent::LayoutRequest) {
        emit layoutRequested();
    }
    return handled;
}

// -------------------------------------------------------------------------------------------------------- ViewZone

ViewZone::ViewZone(Zone zone, ViewRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , m_zone(zone)
    , m_registry(registry)
{
    setObjectName(QStringLiteral("viewZone"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_strip = new ZoneTabStrip(this);
    m_stack = new ZoneStack(this);
    layout->addWidget(m_strip);
    layout->addWidget(m_stack, 1);

    connect(m_strip, &ZoneTabStrip::tabActivated, this, [this](const QString &id) {
        if (m_ids.contains(id)) {
            m_stack->setCurrentWidget(m_registry->existingWidget(id));
            syncConstraints();
            emit viewActivated(id);
        }
    });
    connect(m_strip, &ZoneTabStrip::tabCloseRequested, this, &ViewZone::viewCloseRequested);
    connect(m_strip, &ZoneTabStrip::tabMenuRequested, this, &ViewZone::viewMenuRequested);
    connect(m_strip, &ZoneTabStrip::tabDoubleClicked, this, &ViewZone::viewMaximizeRequested);
    connect(m_strip, &ZoneTabStrip::dragStarted, this, &ViewZone::dragStarted);
    connect(m_strip, &ZoneTabStrip::dragFinished, this, &ViewZone::dragFinished);
    connect(m_stack, &ZoneStack::layoutRequested, this, &ViewZone::syncConstraints);
}

void ViewZone::setBarHeight(int height)
{
    m_barHeight = height;
    m_strip->setBarHeight(height);
    for (const Section &section : std::as_const(m_sections)) {
        section.header->setBarHeight(height);
    }
}

void ViewZone::setZoneFocused(bool focused)
{
    m_zoneFocused = focused;
    m_strip->setZoneFocused(focused);
    for (const Section &section : std::as_const(m_sections)) {
        section.header->setZoneFocused(focused);
    }
}

void ViewZone::setViews(const QStringList &ids, const QString &active, bool stripAlways)
{
    clearSplit(); // os painéis voltam à pilha; o modo dividir é refeito no fim (applySplit)
    m_ids = ids;
    for (const QString &id : ids) {
        QWidget *widget = m_registry->widget(id);
        if (widget && m_stack->indexOf(widget) < 0) {
            m_stack->addWidget(widget);
        }
    }
    rebuildStrip(active);
    m_strip->setVisible(!m_stripSuppressed && (stripAlways || ids.size() > 1));
    applyGlue();
    if (QWidget *front = m_registry->existingWidget(active)) {
        m_stack->setCurrentWidget(front);
        front->show();
    }
    applySplit();
    syncConstraints();
}

// O modo dividir: cada painel da zona numa caixa (cabeçalho com o nome + o painel), todas à vista. Numa célula estreita as
// caixas se empilham; numa larga ficam lado a lado (o formato da célula decide, e acompanha o redimensionamento).
void ViewZone::applySplit()
{
    if (!m_wantSplit || m_ids.size() < 2 || m_stripSuppressed) {
        return;
    }
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("zoneSplitter"));
    m_splitter->setChildrenCollapsible(false);
    static_cast<QVBoxLayout *>(layout())->addWidget(m_splitter, 1);
    for (const QString &id : std::as_const(m_ids)) {
        QWidget *content = m_registry->existingWidget(id);
        if (!content) {
            continue;
        }
        m_stack->removeWidget(content);
        auto *box = new QWidget(m_splitter);
        box->setObjectName(QStringLiteral("zoneSection"));
        auto *vbox = new QVBoxLayout(box);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);
        auto *header = new ZoneTabStrip(box);
        header->setBarHeight(m_barHeight);
        header->setZoneFocused(m_zoneFocused);
        header->setTabs({{id, m_registry->title(id), m_registry->icon(id), false}}, id);
        connect(header, &ZoneTabStrip::tabMenuRequested, this, &ViewZone::viewMenuRequested);
        connect(header, &ZoneTabStrip::tabDoubleClicked, this, &ViewZone::viewMaximizeRequested);
        connect(header, &ZoneTabStrip::dragStarted, this, &ViewZone::dragStarted);
        connect(header, &ZoneTabStrip::dragFinished, this, &ViewZone::dragFinished);
        vbox->addWidget(header);
        content->setParent(box);
        vbox->addWidget(content, 1);
        content->show();
        setCardGlue(content, true);
        m_splitter->addWidget(box);
        m_sections.append({box, header, content, id});
    }
    QList<int> sizes;
    for (int i = 0; i < m_splitter->count(); ++i) {
        sizes << 1000;
        m_splitter->setStretchFactor(i, 1);
    }
    m_splitter->setSizes(sizes);
    m_strip->hide();
    m_stack->hide();
    updateSplitOrientation();
}

// Larga o bastante: lado a lado; estreita: empilhadas. A faixa entre as duas medidas evita que a orientação fique piscando
// quando a célula tem quase o formato de um quadrado.
void ViewZone::updateSplitOrientation()
{
    if (!m_splitter || width() <= 0 || height() <= 0) {
        return;
    }
    const bool horizontal = m_splitter->orientation() == Qt::Horizontal;
    const bool wide = width() >= int(height() * 1.3);
    const bool narrow = width() < height();
    if (horizontal && narrow) {
        m_splitter->setOrientation(Qt::Vertical);
    } else if (!horizontal && wide) {
        m_splitter->setOrientation(Qt::Horizontal);
    }
}

void ViewZone::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateSplitOrientation();
}

// Desfaz o modo dividir: os painéis voltam à pilha da zona (que reaparece com a faixa de abas).
void ViewZone::clearSplit()
{
    if (!m_splitter) {
        return;
    }
    for (const Section &section : std::as_const(m_sections)) {
        if (section.content) {
            section.box->layout()->removeWidget(section.content);
            setCardGlue(section.content, false);
            section.content->hide();
            m_stack->addWidget(section.content);
        }
    }
    m_sections.clear();
    m_splitter->hide();
    m_splitter->deleteLater();
    m_splitter = nullptr;
    m_stack->show();
}

bool ViewZone::showsView(const QString &id) const
{
    return m_splitter ? m_ids.contains(id) : m_strip->activeId() == id;
}

QList<ZoneTabStrip *> ViewZone::sectionHeaders() const
{
    QList<ZoneTabStrip *> headers;
    for (const Section &section : m_sections) {
        headers.append(section.header);
    }
    return headers;
}

int ViewZone::dropIndexAt(const QPoint &pos) const
{
    if (!m_splitter) {
        if (m_strip->isVisibleTo(this)) {
            const QPoint local = m_strip->mapFrom(this, pos);
            if (m_strip->rect().contains(local)) {
                return m_strip->insertionIndexAt(local.x());
            }
        }
        return -1;
    }
    const bool stacked = m_splitter->orientation() == Qt::Vertical;
    for (int i = 0; i < m_sections.size(); ++i) {
        const QPoint local = m_sections.at(i).header->mapFrom(this, pos);
        if (m_sections.at(i).header->rect().contains(local)) {
            const QRect header = m_sections.at(i).header->rect();
            return (stacked ? local.y() < header.center().y() : local.x() < header.center().x()) ? i : i + 1;
        }
    }
    return -1;
}

void ViewZone::rebuildStrip(const QString &active)
{
    QList<ZoneTabStrip::Tab> tabs;
    for (const QString &id : std::as_const(m_ids)) {
        // Sem o "x" na aba: fechar é algo pontual e fica no menu de contexto (botão direito).
        tabs.append({id, m_registry->title(id), m_registry->icon(id), false});
    }
    m_strip->setTabs(tabs, active);
}

namespace {
bool isCardName(const QWidget *widget)
{
    return widget->objectName() == QLatin1String("panelCard") || widget->objectName() == QLatin1String("actionGroupContainer");
}

void setCardFlag(QWidget *card, const char *name, bool on)
{
    if (card->property(name).toBool() == on) {
        return;
    }
    card->setProperty(name, on);
    card->style()->unpolish(card);
    card->style()->polish(card);
    card->update();
}

// Os "cartões" de um painel (a moldura de borda + raio): o próprio painel e o que ele tiver dentro com esse nome.
void setCardGlue(QWidget *root, bool glued)
{
    QList<QWidget *> cards;
    if (root->objectName() == QLatin1String("panelCard") || root->objectName() == QLatin1String("actionGroupContainer")) {
        cards << root;
    }
    for (QWidget *child : root->findChildren<QWidget *>()) {
        if (child->objectName() == QLatin1String("panelCard") || child->objectName() == QLatin1String("actionGroupContainer")) {
            cards << child;
        }
    }
    // A lista de comandos é vários cartões lado a lado (árvore e colunas de ações). Colados na faixa, o vão entre eles
    // deixaria um recorte sob a linha da faixa: ficam encostados, e as duas bordas fazem de divisória.
    if (auto *actions = root->findChild<QSplitter *>(QStringLiteral("actionsSplitter"))) {
        actions->setHandleWidth(glued ? 0 : actions->style()->pixelMetric(QStyle::PM_SplitterWidth));
    }
    for (QWidget *card : std::as_const(cards)) {
        setCardFlag(card, "kaiTabbed", glued);
        setCardFlag(card, "kaiJoinLeft", false);
        setCardFlag(card, "kaiJoinRight", false);
    }
    // Colados na faixa e encostados um no outro, os cartões viram UMA moldura só: nas costuras os cantos são retos e a
    // borda é uma linha única (a do cartão da direita), em cima e embaixo do mesmo jeito.
    if (glued) {
        if (auto *actions = root->findChild<QSplitter *>(QStringLiteral("actionsSplitter"))) {
            QList<QWidget *> shown;
            for (int i = 0; i < actions->count(); ++i) {
                QWidget *child = actions->widget(i);
                if (!child->isVisibleTo(actions)) {
                    continue;
                }
                QWidget *card = nullptr;
                if (isCardName(child)) {
                    card = child;
                } else {
                    for (QWidget *inner : child->findChildren<QWidget *>()) {
                        if (isCardName(inner)) {
                            card = inner;
                            break;
                        }
                    }
                }
                if (card) {
                    shown << card;
                }
            }
            for (int i = 0; i + 1 < shown.size(); ++i) {
                setCardFlag(shown.at(i), "kaiJoinRight", true);
                setCardFlag(shown.at(i + 1), "kaiJoinLeft", true);
            }
        }
    }
}
} // namespace

void ViewZone::applyGlue()
{
    const bool glued = m_strip->isVisibleTo(this);
    for (const Section &section : std::as_const(m_sections)) {
        if (section.content) {
            setCardGlue(section.content, true);
        }
    }
    for (int i = 0; i < m_stack->count(); ++i) {
        QWidget *page = m_stack->widget(i);
        setCardGlue(page, glued);
        // As colunas de ações aparecem e somem sozinhas: a costura entre os cartões acompanha.
        if (auto *actions = page->findChild<QSplitter *>(QStringLiteral("actionsSplitter"))) {
            for (int k = 0; k < actions->count(); ++k) {
                actions->widget(k)->installEventFilter(this);
            }
        }
    }
}

bool ViewZone::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) && !m_regluePending && watched->isWidgetType()) {
        m_regluePending = true;
        QTimer::singleShot(0, this, [this]() {
            m_regluePending = false;
            applyGlue();
        });
    }
    return QWidget::eventFilter(watched, event);
}

void ViewZone::releaseViews(QWidget *newParent)
{
    for (const Section &section : std::as_const(m_sections)) {
        if (section.content) {
            section.box->layout()->removeWidget(section.content);
            setCardGlue(section.content, false);
            section.content->hide();
            section.content->setParent(newParent);
        }
    }
    m_sections.clear();
    if (m_splitter) {
        m_splitter->hide();
        m_splitter->deleteLater();
        m_splitter = nullptr;
    }
    while (m_stack->count() > 0) {
        QWidget *widget = m_stack->widget(0);
        m_stack->removeWidget(widget);
        setCardGlue(widget, false);
        widget->hide();
        widget->setParent(newParent);
    }
    m_ids.clear();
}

QSize ViewZone::minimumSizeHint() const
{
    constexpr int kMinContentHeight = 64;
    QSize hint = QWidget::minimumSizeHint();
    const int extra = m_strip->isVisibleTo(this) ? m_strip->height() : 0;
    hint.setHeight(qMin(hint.height(), extra + kMinContentHeight));
    return hint;
}

void ViewZone::syncConstraints()
{
    if (m_syncing) {
        return;
    }
    m_syncing = true;
    const QWidget *front = m_stack->currentWidget();
    const int extra = m_strip->isVisibleTo(this) ? m_strip->height() : 0;
    const int maxW = front ? front->maximumWidth() : QWIDGETSIZE_MAX;
    const int maxH = front ? front->maximumHeight() : QWIDGETSIZE_MAX;
    setMaximumWidth(maxW < QWIDGETSIZE_MAX ? maxW : QWIDGETSIZE_MAX);
    setMaximumHeight(maxH < QWIDGETSIZE_MAX ? maxH + extra : QWIDGETSIZE_MAX);
    m_syncing = false;
}

} // namespace kai::ui::layout
