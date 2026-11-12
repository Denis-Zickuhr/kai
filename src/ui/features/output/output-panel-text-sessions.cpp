#include "ui/features/output/output-panel.h"

#include "ui/features/output/code-output-view.h"
#include "ui/features/output/log-line-view.h"

#include <QLineEdit>
#include <QPlainTextDocumentLayout>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>

#include <utility>

namespace kai::ui {

namespace {
constexpr int kMaxTextSessions = 6; // comandos recentes com o texto montado
constexpr int kMaxBlocks = 20000;   // o mesmo teto de linhas do texto cru (ver setupUi)
}

OutputPanel::~OutputPanel()
{
    for (TextSession *session : std::as_const(m_sessions)) {
        if (session->ownsViews) {
            delete session->document;
            delete session->model;
        }
        delete session;
    }
}

void OutputPanel::initTextSessions()
{
    auto *session = new TextSession;
    session->document = m_outputView->document();
    session->model = m_formattedView->logModel();
    session->ownsViews = false; // criados pelas vistas, que os apagam
    m_sessions.append(session);
    m_session = session;
}

bool OutputPanel::hasSession(const QString &key) const
{
    for (const TextSession *session : m_sessions) {
        if (session->key == key) {
            return true;
        }
    }
    return false;
}

QString OutputPanel::activeSessionKey() const
{
    return m_session ? m_session->key : QString();
}

qint64 OutputPanel::sessionShownTotal() const
{
    return m_session ? m_session->shownTotal : 0;
}

int OutputPanel::sessionGeneration() const
{
    return m_session ? m_session->generation : 0;
}

void OutputPanel::setSessionShown(qint64 total, int generation)
{
    if (m_session) {
        m_session->shownTotal = total;
        m_session->generation = generation;
    }
}

void OutputPanel::addSessionShown(qint64 chars)
{
    if (m_session) {
        m_session->shownTotal += chars;
    }
}

// O que a sessão que sai guarda para quando voltar: onde a rolagem estava e o estado do parser de cores.
void OutputPanel::saveActiveSession()
{
    if (!m_session) {
        return;
    }
    m_formattedView->flush(); // o texto em espera é do modelo que sai
    m_session->parser = m_parser;
    m_session->atLineStart = m_atLineStart;
    m_session->lastLineWasBlank = m_lastLineWasBlank;
    if (const QScrollBar *bar = m_outputView->verticalScrollBar()) {
        m_session->outputAtBottom = bar->value() >= bar->maximum() - 4;
        m_session->outputScroll = bar->value();
    }
    if (const QScrollBar *bar = m_formattedView->verticalScrollBar()) {
        m_session->formattedAtBottom = bar->value() >= bar->maximum() - 4;
        m_session->formattedScroll = bar->value();
    }
}

bool OutputPanel::activateSession(const QString &key)
{
    if (m_session && m_session->key == key) {
        return true;
    }
    saveActiveSession();
    // A sessão inicial (sem comando) é só um rascunho: não guarda o que mostrou.
    if (m_session && m_session->key.isEmpty()) {
        m_outputView->clear();
        m_formattedView->clearLog();
        m_session->parser.resetFormat();
        m_session->atLineStart = true;
        m_session->lastLineWasBlank = false;
        m_session->shownTotal = 0;
    }

    TextSession *target = nullptr;
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i)->key == key) {
            target = m_sessions.at(i);
            m_sessions.move(i, 0); // a mais recente primeiro
            break;
        }
    }
    const bool existed = target != nullptr;
    if (!target) {
        target = new TextSession;
        target->key = key;
        target->document = new QTextDocument(this);
        target->document->setDocumentLayout(new QPlainTextDocumentLayout(target->document));
        target->document->setDocumentMargin(0);
        target->document->setMaximumBlockCount(kMaxBlocks);
        target->model = new LogLineModel(this);
        m_sessions.prepend(target);
        // Passou do limite: sai a mais antiga que não é a que está entrando nem o rascunho inicial.
        int kept = 0;
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (!m_sessions.at(i)->key.isEmpty()) {
                ++kept;
            }
        }
        for (int i = int(m_sessions.size()) - 1; kept > kMaxTextSessions && i > 0; --i) {
            TextSession *old = m_sessions.at(i);
            if (old == target || old->key.isEmpty()) {
                continue;
            }
            m_sessions.removeAt(i);
            if (old->ownsViews) {
                delete old->document;
                delete old->model;
            }
            delete old;
            --kept;
        }
    }

    // Troca o que a tela mostra e o estado de leitura que anda junto com o texto.
    m_session = target;
    target->document->setDefaultFont(m_outputView->font()); // o tamanho da fonte pode ter mudado com ele fora de cena
    m_outputView->setDocument(target->document);
    m_outputView->documentSwapped();
    m_formattedView->setActiveModel(target->model);
    m_parser = target->parser;
    m_atLineStart = target->atLineStart;
    m_lastLineWasBlank = target->lastLineWasBlank;

    // O resto do painel era do comando que saiu: zera (o dono reaplica o do novo) e refaz a busca sobre o texto novo.
    clearSharedState();
    m_outputMatches.clear();
    m_outputCurrentMatchIndex = -1;
    if (m_outputSearchField && !m_outputSearchField->text().trimmed().isEmpty()) {
        applyOutputSearchFilter(m_outputSearchField->text());
    }
    updateTabVisibility();
    updateBodyStackPage();

    // A rolagem em que ele ficou, depois do layout (a barra só ganha o alcance quando a vista mede o conteúdo).
    if (existed) {
        const bool outputAtBottom = target->outputAtBottom;
        const int outputScroll = target->outputScroll;
        const bool formattedAtBottom = target->formattedAtBottom;
        const int formattedScroll = target->formattedScroll;
        QTimer::singleShot(0, this, [this, outputAtBottom, outputScroll, formattedAtBottom, formattedScroll]() {
            if (QScrollBar *bar = m_outputView->verticalScrollBar()) {
                bar->setValue(outputAtBottom ? bar->maximum() : qMin(outputScroll, bar->maximum()));
            }
            if (QScrollBar *bar = m_formattedView->verticalScrollBar()) {
                bar->setValue(formattedAtBottom ? bar->maximum() : qMin(formattedScroll, bar->maximum()));
            }
        });
    }
    return existed;
}

void OutputPanel::dropSession(const QString &key)
{
    if (key.isEmpty() || (m_session && m_session->key == key)) {
        return; // a que está na tela se limpa pelo clearAll
    }
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i)->key == key) {
            TextSession *old = m_sessions.takeAt(i);
            if (old->ownsViews) {
                delete old->document;
                delete old->model;
            }
            delete old;
            return;
        }
    }
}

} // namespace kai::ui
