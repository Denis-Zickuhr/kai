#include "ui/features/output/run-mirror-panel.h"

#include "engine/kip-session.h"
#include "ui/features/kip/kip-view.h"
#include "ui/features/output/output-panel.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/panel-metrics.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

RunMirrorPanel::RunMirrorPanel(const QString &commandId, const QString &name, Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_commandId(commandId)
    , m_kind(kind)
{
    // Mesma moldura (borda + raio dos tokens) da lista de comandos e da Saída.
    setObjectName(QStringLiteral("panelCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *root = new QVBoxLayout(this);
    const int inset = panelFrameInset();
    root->setContentsMargins(inset, inset, inset, inset);
    root->setSpacing(0);

    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages);

    m_output = new OutputPanel(m_pages);
    m_output->setCommandId(commandId);
    m_output->setCommandName(name);
    m_output->setKipDetachable(false); // já é um painel próprio
    connect(m_output, &OutputPanel::kipRunAgainRequested, this, [this]() { emit runRequested(m_commandId); });

    // O cartão de "ainda não rodou" do KIP fixado: dá o ponto de partida sem rodar nada sozinho.
    m_idleCard = new QWidget(m_pages);
    m_idleCard->setObjectName(QStringLiteral("runMirrorIdle"));
    m_idleCard->setStyleSheet(QStringLiteral("QWidget#runMirrorIdle { background: transparent; }"));
    auto *idle = new QVBoxLayout(m_idleCard);
    idle->setAlignment(Qt::AlignCenter);
    idle->setSpacing(tk::space(3));
    auto *icon = new QLabel(m_idleCard);
    icon->setObjectName(QStringLiteral("runMirrorIdleIcon"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral("background: transparent;"));
    icon->setPixmap(LucideIcons::icon(QStringLiteral("sparkles"), QColor(tk::mutedFg()), 32).pixmap(32, 32));
    m_idleTitle = new QLabel(name, m_idleCard);
    m_idleTitle->setObjectName(QStringLiteral("runMirrorIdleTitle"));
    m_idleTitle->setAlignment(Qt::AlignCenter);
    m_idleTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1;").arg(tk::fg()));
    auto *run = new QPushButton(utils::tr(QStringLiteral("layout.kip_panel.run")), m_idleCard);
    run->setObjectName(QStringLiteral("runMirrorRun"));
    run->setProperty("kaiRole", QStringLiteral("primary"));
    run->setCursor(Qt::PointingHandCursor);
    connect(run, &QPushButton::clicked, this, [this]() { emit runRequested(m_commandId); });
    idle->addWidget(icon);
    idle->addWidget(m_idleTitle);
    idle->addWidget(run, 0, Qt::AlignHCenter);

    m_pages->addWidget(m_output);
    m_pages->addWidget(m_idleCard);
    m_pages->setCurrentWidget(kind == Kind::Kip ? m_idleCard : m_output);
}

bool RunMirrorPanel::showsIdleCard() const
{
    return m_pages->currentWidget() == m_idleCard;
}

void RunMirrorPanel::showOutput()
{
    m_pages->setCurrentWidget(m_output);
}

void RunMirrorPanel::seed(const OutputPanel *embedded, const QString &storedLog)
{
    if (embedded && embedded->commandId() == m_commandId) {
        // A sessão KIP vem junto (espelho de um app KIP = a view dele); quem cede o lugar é a Saída embutida.
        m_output->copyStateFrom(*embedded);
        m_output->setKipDetachable(false);
        showOutput();
        return;
    }
    if (!storedLog.isEmpty()) {
        m_output->appendOutput(storedLog, false);
        showOutput();
    }
}

void RunMirrorPanel::beginRun()
{
    m_output->clearAll();
    m_output->setStatus(OutputStatus::Running);
    showOutput();
}

void RunMirrorPanel::setInitialStatus(ExecutionStatus status)
{
    const OutputStatus next = toOutputStatus(status);
    m_output->setStatus(next);
    if (next != OutputStatus::Idle) {
        showOutput();
    }
}

void RunMirrorPanel::appendLog(const QString &text, bool isError)
{
    m_output->appendOutput(text, isError);
    showOutput();
}

void RunMirrorPanel::setHttpResult(const engine::HttpResult &result)
{
    m_output->setHttpResult(result);
    showOutput();
}

void RunMirrorPanel::setStatus(ExecutionStatus status)
{
    const OutputStatus next = toOutputStatus(status);
    // Sucesso só vale como FIM de uma execução em andamento: um painel ocioso não vira "Concluído" por si só.
    if (next == OutputStatus::Success && m_output->status() != OutputStatus::Running) {
        return;
    }
    m_output->setStatus(next);
    if (next != OutputStatus::Idle) {
        showOutput();
    }
}

void RunMirrorPanel::bindKipSession(engine::KipSession *session, bool takeFocus)
{
    m_output->setKipSession(session);
    if (KipView *view = m_output->kipView()) {
        view->setAutoFocus(takeFocus);
    }
    if (session) {
        showOutput();
    }
}

void RunMirrorPanel::setName(const QString &name)
{
    m_output->setCommandName(name);
    m_idleTitle->setText(name);
}

void RunMirrorPanel::applyBarHeight(int height)
{
    m_output->setBarHeight(height);
}

void RunMirrorPanel::refreshStyle()
{
    if (auto *root = qobject_cast<QVBoxLayout *>(layout())) {
        const int inset = panelFrameInset();
        root->setContentsMargins(inset, inset, inset, inset);
    }
    m_idleTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1;").arg(tk::fg()));
}

} // namespace kai::ui
