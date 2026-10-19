#include "ui/features/kip/kip-details-drawer.h"

#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {

QPlainTextEdit *makeMonoView(QWidget *parent)
{
    auto *view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    view->setFont(tk::monoFont(tk::fontSizeSmallPt()));
    view->setMaximumBlockCount(5000);
    view->setMinimumHeight(110);
    view->setMaximumHeight(240);
    view->setFrameShape(QFrame::NoFrame);
    return view;
}

void appendFormatted(QPlainTextEdit *view, const QVector<QPair<QString, QColor>> &parts)
{
    QScrollBar *bar = view->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    QTextCursor cursor(view->document());
    cursor.movePosition(QTextCursor::End);
    if (!view->document()->isEmpty()) {
        cursor.insertBlock();
    }
    for (const auto &part : parts) {
        QTextCharFormat format;
        format.setForeground(part.second);
        cursor.insertText(part.first, format);
    }
    if (atBottom) {
        bar->setValue(bar->maximum());
    }
}

} // namespace

KipDetailsDrawer::KipDetailsDrawer(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("kipDetailsDrawer"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(tk::space(1));

    // Linha do botão: "Detalhes" e, aberto, o seletor Log | Protocolo ao lado (quem hospeda pode levar os dois para a
    // linha dos botões dele).
    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(tk::space(2));
    m_toggle = new QToolButton(this);
    m_toggle->setObjectName(QStringLiteral("kipDetailsToggle"));
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setText(utils::tr(QStringLiteral("kip.details.title")));
    m_toggle->setCursor(Qt::PointingHandCursor);
    m_toggle->setAutoRaise(true);
    connect(m_toggle, &QToolButton::clicked, this, [this]() { setExpanded(!m_expanded); });
    bar->addWidget(m_toggle);

    m_switch = new QFrame(this);
    m_switch->setObjectName(QStringLiteral("kipDetailsSwitch"));
    auto *switchLayout = new QHBoxLayout(m_switch);
    switchLayout->setContentsMargins(2, 2, 2, 2);
    switchLayout->setSpacing(2);
    auto *pages = new QButtonGroup(m_switch);
    pages->setExclusive(true);
    for (const QString &key : {QStringLiteral("kip.details.log"), QStringLiteral("kip.details.protocol")}) {
        auto *button = new QToolButton(m_switch);
        button->setObjectName(QStringLiteral("kipDetailsPage"));
        button->setText(utils::tr(key));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        pages->addButton(button, int(m_pageButtons.size()));
        m_pageButtons.append(button);
        switchLayout->addWidget(button);
    }
    m_pageButtons.first()->setChecked(true);
    m_switch->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_switch->setVisible(false);
    bar->addWidget(m_switch, 0, Qt::AlignVCenter);
    bar->addStretch(1);
    root->addLayout(bar);

    m_body = new QWidget(this);
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    m_pages = new QStackedWidget(m_body);
    m_log = makeMonoView(m_pages);
    m_log->setPlaceholderText(utils::tr(QStringLiteral("kip.details.empty")));
    m_protocol = makeMonoView(m_pages);
    m_protocol->setPlaceholderText(utils::tr(QStringLiteral("kip.details.empty")));
    m_pages->addWidget(m_log);
    m_pages->addWidget(m_protocol);
    bodyLayout->addWidget(m_pages);
    connect(pages, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);
    root->addWidget(m_body);

    m_body->setVisible(false);
    applyStyle();
}

void KipDetailsDrawer::applyStyle()
{
    m_toggle->setIcon(LucideIcons::icon(m_expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                                        QColor(tk::mutedFg()), 16));
    m_toggle->setStyleSheet(QStringLiteral(
        "QToolButton#kipDetailsToggle { color: %1; background: transparent; border: none; padding: %2px %3px;"
        " border-radius: %4px; }"
        "QToolButton#kipDetailsToggle:hover { background-color: %5; }")
        .arg(tk::mutedFg()).arg(tk::space(1)).arg(tk::space(2)).arg(tk::radiusSm()).arg(tk::hoverBg()));
    // Seletor Log | Protocolo: o segmentado pequeno do app (o mesmo do filtro de Processos), não uma barra de abas.
    m_switch->setStyleSheet(QStringLiteral(
        "QFrame#kipDetailsSwitch { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
        "QToolButton#kipDetailsPage { background: transparent; border: none; color: %4; padding: 1px %5px;"
        " border-radius: %6px; font-size: %7pt; min-height: 0px; }"
        "QToolButton#kipDetailsPage:checked { background-color: %8; color: %9; font-weight: 600; }")
        .arg(tk::surface(), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::mutedFg())
        .arg(tk::space(2))
        .arg(qMax(0, tk::radiusMd() - 3))
        .arg(tk::fontSizeSmallPt())
        .arg(tk::selBg(), tk::fg()));
    const QString viewStyle = QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: %5px; }")
        .arg(tk::terminalBg(), tk::terminalFg(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::space(2));
    m_log->setStyleSheet(viewStyle);
    m_protocol->setStyleSheet(viewStyle);
}

void KipDetailsDrawer::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    m_body->setVisible(expanded);
    m_switch->setVisible(expanded);
    applyStyle();
    emit expandedChanged(expanded);
}

void KipDetailsDrawer::reload(const engine::KipSession *session)
{
    m_log->clear();
    m_protocol->clear();
    if (!session) {
        return;
    }
    m_log->setPlainText(session->logText());
    // O texto do log já termina em '\n'; QPlainTextEdit acrescentaria um bloco vazio.
    for (const engine::KipTraceEntry &entry : session->trace()) {
        appendTrace(entry);
    }
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void KipDetailsDrawer::appendLog(const QString &text)
{
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (atBottom) {
        bar->setValue(bar->maximum());
    }
}

void KipDetailsDrawer::appendTrace(const engine::KipTraceEntry &entry)
{
    const bool incoming = entry.direction == engine::KipTraceEntry::Direction::FromProgram;
    QVector<QPair<QString, QColor>> parts;
    parts.append({incoming ? QStringLiteral("← ") : QStringLiteral("→ "),
                  QColor(incoming ? tk::infoFg() : tk::accent())});
    parts.append({entry.time.toString(QStringLiteral("HH:mm:ss.zzz")) + QStringLiteral("  "), QColor(tk::mutedFg())});
    parts.append({entry.text, QColor(tk::terminalFg())});
    if (!entry.note.isEmpty()) {
        parts.append({QStringLiteral("   ⚠ ") + entry.note, QColor(tk::warningFg())});
    }
    appendFormatted(m_protocol, parts);
}

QString KipDetailsDrawer::logText() const
{
    return m_log->toPlainText();
}

QString KipDetailsDrawer::protocolText() const
{
    return m_protocol->toPlainText();
}

} // namespace kai::ui
