#include "ui/features/output/output-panel.h"

#include "ui/shared/lucide-icons.h"

#include "ui/features/kip/kip-view.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QClipboard>

#include <QFileDialog>

#include <QGuiApplication>

#include <QLabel>
#include <QLineEdit>

#include <QScrollArea>
#include <QScrollBar>

#include <QStackedWidget>
#include <QPushButton>

#include <QToolButton>
#include <QVBoxLayout>

#include "ui/features/output/output-panel-views.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace outputview;

void OutputPanel::setFormattedOutputEnabled(bool enabled)
{
    if (m_formattedOutputEnabled == enabled) {
        return;
    }
    const bool wasFormatted = m_formattedOutputEnabled;
    m_formattedOutputEnabled = enabled;
    updateOutputStackPage();
    if (m_outputFieldFilterButton) {
        m_outputFieldFilterButton->setVisible(m_outputSearchExpanded && enabled);
    }
    if (m_outputSearchExpanded) {
        const QString needle = m_outputSearchField->text();
        if (enabled) {
            m_formattedView->setFilterText(needle);
            applyOutputSearchFilter(QString());
        } else if (wasFormatted) {
            m_formattedView->setFilterText(QString());
            applyOutputSearchFilter(needle);
        }
        updateOutputMatchCounterLabel();
    }
}

void OutputPanel::updateOutputStackPage()
{
    QWidget *page = m_formattedOutputEnabled ? static_cast<QWidget *>(m_formattedView)
                                             : static_cast<QWidget *>(m_outputView);
    m_outputStack->setCurrentWidget(page);
}

void OutputPanel::copyStateFrom(const OutputPanel &source)
{
    setViewOptions(source.m_options);
    setCommandId(source.m_commandId);
    setCommandName(source.m_titleLabel->text());
    setWorkingDirectory(source.m_workingDirectory);
    setFormattedOutputEnabled(source.m_formattedOutputEnabled);
    // A aba "Saída" some em HTTP: sem copiar, a janela mostraria só ela em vez de Resposta/Requisição/Headers.
    setStdoutTabVisible(source.m_stdoutTabEnabled);
    seedOutput(source.plainOutput());
    if (source.m_lastHttpResult) {
        setHttpResult(*source.m_lastHttpResult);
    }
    setProcessPid(source.m_processPid);
    setSkipped(source.m_skipped, source.m_skippedReasonLabel ? source.m_skippedReasonLabel->text() : QString());
    // KIP: a janela destacada ganha a PRÓPRIA view, ligada à mesma sessão (o estado vive nela, §13.4).
    setKipSession(source.m_kipSession);
    setStatus(source.m_status);
}

void OutputPanel::setCommandName(const QString &name)
{
    m_titleLabel->setText(name.isEmpty() ? utils::tr(QStringLiteral("output.title")) : name);
    if (m_kipView) {
        m_kipView->setCommandName(name);
    }
}

void OutputPanel::createKipView()
{
    m_kipView = new KipView(this);
    m_kipView->setCommandName(m_titleLabel ? m_titleLabel->text() : QString());
    connect(m_kipView, &KipView::runAgainRequested, this, &OutputPanel::kipRunAgainRequested);
    connect(m_kipView, &KipView::detachRequested, this, &OutputPanel::kipDetachRequested);
    m_kipView->setDetachable(m_kipDetachable);
    m_bodyStack->insertWidget(4, m_kipView);

    // Cartão exibido enquanto a view vive na janela destacada.
    m_kipPlaceholder = new QWidget(this);
    auto *layout = new QVBoxLayout(m_kipPlaceholder);
    layout->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(4));
    layout->setSpacing(tk::space(2));
    layout->addStretch(1);
    auto *icon = new QLabel(m_kipPlaceholder);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(LucideIcons::icon(QStringLiteral("external-link"), QColor(tk::mutedFg()), 32).pixmap(32, 32));
    layout->addWidget(icon);
    auto *title = new QLabel(utils::tr(QStringLiteral("kip.placeholder.title")), m_kipPlaceholder);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto *text = new QLabel(utils::tr(QStringLiteral("kip.placeholder.text")), m_kipPlaceholder);
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    layout->addWidget(text);
    auto *focus = new QPushButton(utils::tr(QStringLiteral("kip.placeholder.focus")), m_kipPlaceholder);
    focus->setCursor(Qt::PointingHandCursor);
    focus->setProperty("kaiRole", QStringLiteral("primary"));
    connect(focus, &QPushButton::clicked, this, &OutputPanel::kipFocusWindowRequested);
    layout->addWidget(focus, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    m_bodyStack->insertWidget(5, m_kipPlaceholder);
}

void OutputPanel::setKipSession(engine::KipSession *session)
{
    m_kipSession = session;
    m_kipMode = session != nullptr;
    if (!m_kipMode) {
        m_kipDetachedPlaceholder = false;
    }
    if (session && !m_kipView) {
        createKipView();
    }
    if (m_kipView) {
        m_kipView->setSession(session);
    }
    updateBodyStackPage();
}

engine::KipSession *OutputPanel::kipSession() const
{
    return m_kipSession.data();
}

void OutputPanel::setKipDetachable(bool detachable)
{
    m_kipDetachable = detachable;
    if (m_kipView) {
        m_kipView->setDetachable(detachable);
    }
}

void OutputPanel::setKipDetachedPlaceholder(bool detached)
{
    m_kipDetachedPlaceholder = detached;
    updateBodyStackPage();
}

void OutputPanel::setCommandId(const QString &id)
{
    m_commandId = id;
}

void OutputPanel::setWorkingDirectory(const QString &dir)
{
    m_workingDirectory = dir;
    QString where = dir.trimmed();
    if (where.isEmpty()) {
        m_promptLabel->setText(userName());
        return;
    }
    const QString home = QDir::homePath();
    if (!home.isEmpty() && where.startsWith(home)) {
        where = QStringLiteral("~") + where.mid(home.length());
    }
    where.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (where.length() > 1 && where.endsWith(QLatin1Char('/'))) {
        where.chop(1);
    }
    m_promptLabel->setText(utils::tr(QStringLiteral("output.prompt.format")).arg(userName(), where));
}

QString OutputPanel::userName()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString user = env.value(QStringLiteral("USER"));
    if (user.isEmpty()) {
        user = env.value(QStringLiteral("USERNAME"));
    }
    return user;
}

void OutputPanel::setProcessPid(qint64 pid)
{
    m_processPid = pid;
    const bool has = pid > 0;
    m_statusBadge->setToolTip(has
        ? utils::tr(QStringLiteral("output.copy_pid")).arg(pid)
        : QString());
    m_statusBadge->setCursor(has ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void OutputPanel::setStatus(OutputStatus status)
{
    m_status = status;
    updateBodyStackPage();
    if (status == OutputStatus::Running) {
        m_formattedView->flush(); // o que sobrou da execução anterior ainda conta para a trava dela
        m_firstErrorNotifiedThisRun = false;
    }
    QString label;
    QString state;
    switch (status) {
    case OutputStatus::Running: label = utils::tr(QStringLiteral("output.status.running")); state = QStringLiteral("running"); break;
    case OutputStatus::Success: label = utils::tr(QStringLiteral("output.status.success")); state = QStringLiteral("success"); break;
    case OutputStatus::Error:   label = utils::tr(QStringLiteral("output.status.error"));   state = QStringLiteral("error");   break;
    case OutputStatus::Idle:    label = utils::tr(QStringLiteral("output.status.idle"));    state = QStringLiteral("idle");    break;
    case OutputStatus::Skipped: label = utils::tr(QStringLiteral("output.status.skipped")); state = QStringLiteral("skipped"); break;
    }

    QColor dotColor;
    switch (status) {
    case OutputStatus::Running: dotColor = QColor(utils::tokens::infoFg());    break;
    case OutputStatus::Success: dotColor = QColor(utils::tokens::successFg()); break;
    case OutputStatus::Error:   dotColor = QColor(utils::tokens::errorFg());   break;
    case OutputStatus::Idle:    dotColor = QColor(utils::tokens::mutedFg());   break;
    case OutputStatus::Skipped: dotColor = QColor(utils::tokens::warningFg()); break;
    }
    // Selecionar um item repete o estado ocioso: refazer estilo/layout/repaint a cada clique travava a navegação;
    // só reaplica quando algo visível mudou (estado, texto, cor, raio).
    const QString appliedKey = QStringLiteral("%1|%2|%3|%4").arg(state, ucFirst(label), dotColor.name()).arg(tk::radiusSm());
    if (appliedKey == m_appliedStatusKey) {
        return;
    }
    m_appliedStatusKey = appliedKey;
    m_statusDot->setStyleSheet(QStringLiteral(
        "background-color: %1; border-radius: %2px;").arg(dotColor.name()).arg(tk::radiusSm()));
    m_statusLabel->setText(ucFirst(label));
    m_statusBadge->setProperty("kaiState", state);
    m_statusBadge->style()->unpolish(m_statusBadge);
    m_statusBadge->style()->polish(m_statusBadge);

    m_statusLabel->adjustSize();
    m_statusBadge->adjustSize();
    if (auto *badgeLayout = m_statusBadge->layout()) {
        badgeLayout->invalidate();
    }
    if (m_headerExtras && m_headerExtras->layout()) {
        m_headerExtras->layout()->invalidate();
        m_headerExtras->layout()->activate();
        m_headerExtras->setFixedWidth(m_headerExtras->layout()->sizeHint().width());
    }
    if (m_header && m_header->layout()) {
        m_header->layout()->invalidate();
        m_header->layout()->activate();
    }

    if (m_header) {
        m_header->updateGeometry();
        m_header->update();
        if (QWidget *p = m_header->parentWidget()) {
            p->update();
        }
    }
    update();

    QTimer::singleShot(0, this, [this]() {
        if (m_header) {
            m_header->update();
            if (QWidget *p = m_header->parentWidget()) {
                p->update();
            }
        }
        if (QWidget *top = window()) {
            top->repaint();
        }
    });
}

void OutputPanel::setInputEnabled(bool enabled)
{
    m_inputField->setEnabled(enabled);
    m_inputFooter->setVisible(m_bodyVisible && enabled);
    updateBodyStackPage();
    refreshInputPlaceholder();
    applyOptionsToOutput();
}

void OutputPanel::setInputShortcutHint(const QString &shortcutText)
{
    m_inputShortcutHint = shortcutText;
    refreshInputPlaceholder();
}

void OutputPanel::refreshInputPlaceholder()
{
    if (!m_inputField->isEnabled()) {
        m_inputField->setPlaceholderText(QString());
        return;
    }
    m_inputField->setPlaceholderText(m_inputShortcutHint.isEmpty()
        ? utils::tr(QStringLiteral("output.input.placeholder"))
        : utils::tr(QStringLiteral("output.input.placeholder_shortcut")).arg(m_inputShortcutHint));
}

void OutputPanel::focusInput()
{
    if (m_inputField->isEnabled()) {
        m_inputField->setFocus();
    }
}

bool OutputPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_outputContainer && event->type() == QEvent::Resize) {
        repositionOutputSearchOverlay();
    }
    // Roda do mouse na área livre do cabeçalho rola abas + ações na horizontal (barra escondida).
    if (m_headerScroll && watched == m_headerScroll->viewport() && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent *>(event);
        QScrollBar *bar = m_headerScroll->horizontalScrollBar();
        const int delta = we->angleDelta().x() != 0 ? we->angleDelta().x() : we->angleDelta().y();
        bar->setValue(bar->value() - delta);
        return true;
    }
    if (watched == m_inputField
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        m_inputFooter->setProperty("focused", event->type() == QEvent::FocusIn);
        m_inputFooter->style()->unpolish(m_inputFooter);
        m_inputFooter->style()->polish(m_inputFooter);
    }
    if (watched == m_inputField && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->modifiers().testFlag(Qt::ControlModifier)) {
            if (ke->key() == Qt::Key_C && !m_inputField->hasSelectedText()) {
                emit interruptRequested();
                return true;
            }
            if (ke->key() == Qt::Key_D) {
                emit eofRequested();
                return true;
            }
        }
    }
    // Ctrl+scroll ajusta a fonte no viewport da Saída e do corpo da Requisição.
    if (event->type() == QEvent::Wheel
        && (watched == m_outputView->viewport() || watched == m_requestBodyView->viewport())) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers().testFlag(Qt::ControlModifier)) {
            if (we->angleDelta().y() > 0) {
                increaseFontSize();
            } else if (we->angleDelta().y() < 0) {
                decreaseFontSize();
            }
            return true;
        }
    }
    // Clique no badge de status copia o PID (quando há um processo).
    if (watched == m_statusBadge && event->type() == QEvent::MouseButtonRelease) {
        if (m_processPid > 0) {
            QGuiApplication::clipboard()->setText(QString::number(m_processPid));
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace kai::ui
