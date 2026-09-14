#include "ui/features/kip/kip-view.h"

#include "core/kip-settings.h"
#include "ui/features/kip/kip-blocks.h"
#include "ui/features/kip/kip-chips.h"
#include "ui/features/kip/kip-details-drawer.h"
#include "ui/features/kip/kip-result-card.h"
#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/parameter-field-factory.h"
#include "utils/design-tokens.h"
#include "utils/path-format.h"
#include "utils/translation-manager.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QStyle>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {

const core::KipScreenState &emptyState()
{
    static const core::KipScreenState state;
    return state;
}

bool isBadTerminal(core::KipSessionState state)
{
    switch (state) {
    case core::KipSessionState::Unsupported:
    case core::KipSessionState::ProtocolError:
    case core::KipSessionState::Failed:
    case core::KipSessionState::Cancelled:
        return true;
    default:
        return false;
    }
}

QByteArray fieldSignature(const core::KipField &field)
{
    return QJsonDocument(field.toJson()).toJson(QJsonDocument::Compact);
}

void clearLayout(QLayout *layout, bool deleteWidgets)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (deleteWidgets) {
            kipDiscard(item->widget());
        }
        delete item;
    }
}

// Caminho no formato `format` -> caminho nativo deste SO (§12.1 `reveal`).
QString nativePath(const QString &path, const QString &format, const QString &wslDistro)
{
#if defined(Q_OS_WIN)
    const QString host = QStringLiteral("windows");
#else
    const QString host = QStringLiteral("posix");
#endif
    if (format.isEmpty() || format == QLatin1String("native") || format == host) {
        return path;
    }
    return utils::convertFilePathFormat(path, host, wslDistro);
}

// Pasta a abrir para um caminho: arquivo -> a pasta dele (§12.1).
QString folderToReveal(const QString &native)
{
    const QFileInfo info(native);
    return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
}

} // namespace

KipView::KipView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("kipView"));
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &KipView::refresh);
    m_changeTimer.setSingleShot(true);
    m_changeTimer.setInterval(300); // debounce do `watch` em campos de texto (§9)
    connect(&m_changeTimer, &QTimer::timeout, this, &KipView::sendPendingChange);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(4), tk::space(3), tk::space(4), tk::space(3));
    root->setSpacing(tk::space(3));

    // ---- cabeçalho: título do app + versão discreta + "trabalhando" ----
    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(tk::space(2));
    m_title = new QLabel(this);
    m_title->setProperty("kaiRole", QStringLiteral("title"));
    m_version = new QLabel(this);
    m_version->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;").arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
    m_spinner = new KipSpinner(this);
    m_statusText = new QLabel(this);
    m_statusText->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    header->addWidget(m_title);
    header->addWidget(m_version, 0, Qt::AlignBottom);
    header->addStretch(1);
    header->addWidget(m_spinner);
    header->addWidget(m_statusText);
    // Sem a barra do painel (some nos comandos KIP), abrir em janela própria vive aqui.
    m_detach = new QToolButton(this);
    m_detach->setObjectName(QStringLiteral("kipDetachButton"));
    m_detach->setAutoRaise(true);
    m_detach->setCursor(Qt::PointingHandCursor);
    m_detach->setIcon(LucideIcons::icon(QStringLiteral("external-link"), QColor(tk::mutedFg()), 16));
    m_detach->setToolTip(utils::tr(QStringLiteral("kip.action.detach")));
    connect(m_detach, &QToolButton::clicked, this, &KipView::detachRequested);
    header->addWidget(m_detach);
    root->addLayout(header);

    // ---- conteúdo rolável ----
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_scroll->viewport()->setObjectName(QStringLiteral("kipScrollViewport"));
    m_scroll->viewport()->setStyleSheet(QStringLiteral("QWidget#kipScrollViewport { background: transparent; }"));
    auto *content = new QWidget();
    content->setObjectName(QStringLiteral("kipScrollContent"));
    content->setStyleSheet(QStringLiteral("QWidget#kipScrollContent { background: transparent; }"));
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(0, 0, tk::space(2), 0);
    m_contentLayout->setSpacing(tk::space(3));

    m_summary = new KipAnswerSummary(content);
    m_contentLayout->addWidget(m_summary);

    auto *blocksHost = new QWidget(content);
    blocksHost->setObjectName(QStringLiteral("kipBlocksHost"));
    blocksHost->setStyleSheet(QStringLiteral("QWidget#kipBlocksHost { background: transparent; }"));
    m_blocksLayout = new QVBoxLayout(blocksHost);
    m_blocksLayout->setContentsMargins(0, 0, 0, 0);
    m_blocksLayout->setSpacing(tk::space(3));
    m_contentLayout->addWidget(blocksHost);

    m_promptHost = new QWidget(content);
    m_promptHost->setObjectName(QStringLiteral("kipPromptHost"));
    m_promptHost->setStyleSheet(QStringLiteral("QWidget#kipPromptHost { background: transparent; }"));
    m_promptHostLayout = new QVBoxLayout(m_promptHost);
    m_promptHostLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->addWidget(m_promptHost);

    m_result = new KipResultCard(content);
    m_result->setVisible(false);
    m_contentLayout->addWidget(m_result);
    m_contentLayout->addStretch(1);
    m_scroll->setWidget(content);
    root->addWidget(m_scroll, 1);

    // ---- detalhes (recolhido) ----
    m_details = new KipDetailsDrawer(this);
    root->addWidget(m_details);

    // ---- rodapé: Back | Cancel | (Decline Confirm | Submit) ----
    m_footer = new QWidget(this);
    m_footer->setObjectName(QStringLiteral("kipFooter"));
    m_footer->setStyleSheet(QStringLiteral("QWidget#kipFooter { background: transparent; }") + kipPrimaryDisabledQss());
    auto *footer = new QHBoxLayout(m_footer);
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(tk::space(2));
    m_back = new QPushButton(utils::tr(QStringLiteral("kip.action.back")), m_footer);
    m_back->setIcon(LucideIcons::icon(QStringLiteral("arrow-left"), QColor(tk::fg()), 16));
    m_cancel = new QPushButton(utils::tr(QStringLiteral("kip.action.cancel")), m_footer);
    m_decline = new QPushButton(m_footer);
    m_confirm = new QPushButton(m_footer);
    m_submit = new QPushButton(m_footer);
    m_submit->setProperty("kaiRole", QStringLiteral("primary"));
    footer->addWidget(m_back);
    footer->addStretch(1);
    footer->addWidget(m_cancel);
    footer->addWidget(m_decline);
    footer->addWidget(m_confirm);
    footer->addWidget(m_submit);
    root->addWidget(m_footer);
    for (QPushButton *b : {m_back, m_cancel, m_decline, m_confirm, m_submit}) {
        b->setCursor(Qt::PointingHandCursor);
        b->installEventFilter(this);
    }

    connect(m_back, &QPushButton::clicked, this, [this]() {
        m_changeTimer.stop();
        if (m_session) m_session->back();
    });
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        if (m_session) m_session->cancel();
    });
    connect(m_submit, &QPushButton::clicked, this, [this]() {
        m_changeTimer.stop();
        if (m_session) m_session->submit();
    });
    connect(m_decline, &QPushButton::clicked, this, [this]() {
        if (m_session) m_session->confirm(false);
    });
    connect(m_confirm, &QPushButton::clicked, this, [this]() {
        if (m_session) m_session->confirm(true);
    });

    refresh();
}

KipView::~KipView() = default;

const core::KipScreenState *KipView::screen() const
{
    if (m_session) return &m_session->screen();
    if (m_staticState) return m_staticState;
    return &emptyState();
}

void KipView::setCommandName(const QString &name)
{
    m_commandName = name;
    scheduleRefresh();
}

void KipView::setSession(engine::KipSession *session)
{
    if (m_session == session && !m_staticState) {
        return;
    }
    if (m_session) {
        m_session->disconnect(this);
    }
    m_session = session;
    m_staticState = nullptr;
    m_summaryCount = -1;
    m_promptSerial = 0;
    m_autoExpanded = false;
    // Cada execução começa com o Detalhes recolhido: uma falha da rodada anterior
    // (ou um clique do usuário) não pode deixar o log aberto numa tela nova.
    m_details->setExpanded(false);
    m_changeTimer.stop();
    if (m_session) {
        connect(m_session, &engine::KipSession::screenChanged, this, &KipView::scheduleRefresh);
        connect(m_session, &engine::KipSession::stateChanged, this, &KipView::scheduleRefresh);
        connect(m_session, &engine::KipSession::logLine, this,
                [this](const QString &text, bool) { m_details->appendLog(text); });
        connect(m_session, &engine::KipSession::protocolTrace, m_details, &KipDetailsDrawer::appendTrace);
    }
    m_details->reload(m_session);
    flushRefresh();
}

void KipView::setScreenState(const core::KipScreenState *state)
{
    if (m_session) {
        m_session->disconnect(this);
        m_session = nullptr;
    }
    m_staticState = state;
    m_summaryCount = -1;
    m_promptSerial = 0;
    m_autoExpanded = false;
    m_details->reload(nullptr);
    flushRefresh();
}

void KipView::scheduleRefresh()
{
    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    }
}

void KipView::flushRefresh()
{
    m_refreshTimer.stop();
    refresh();
}

void KipView::setDetachable(bool detachable)
{
    m_detach->setVisible(detachable);
}

KipFieldEditor *KipView::editorFor(const QString &fieldName) const
{
    for (const FieldSlot &slot : m_slots) {
        if (slot.name == fieldName) return slot.editor.data();
    }
    return nullptr;
}

bool KipView::showsLockedSpinner() const
{
    const core::KipScreenState &s = *screen();
    return s.openScreen() && s.openScreen()->locked && !core::kipIsTerminalState(s.sessionState())
        && m_promptWorking && !m_promptWorking->isHidden();
}

// ============================================================== refresh

void KipView::refresh()
{
    if (m_rebuilding) {
        return;
    }
    m_rebuilding = true;
    const core::KipScreenState &s = *screen();
    const bool bad = isBadTerminal(s.sessionState());

    refreshHeader(s);
    if (m_summaryCount != s.answered().size()) {
        m_summary->setAnswers(s.answered());
        m_summaryCount = s.answered().size();
    }
    m_summary->setVisible(m_summary->rowCount() > 0);
    refreshBlocks(s, !bad);
    refreshPrompt(s, !bad && s.openScreen().has_value());
    refreshResult(s);
    refreshFooter(s);

    // Falhas abrem o Detalhes sozinhas, uma vez por estado (§4, §13.2).
    if (bad && core::kipSettings().expandDetailsOnFailure
        && (!m_autoExpanded || m_autoExpandedFor != s.sessionState())) {
        m_details->setExpanded(true);
        m_autoExpanded = true;
        m_autoExpandedFor = s.sessionState();
    }
    m_rebuilding = false;
    updateSubmitEnabled();
}

void KipView::refreshHeader(const core::KipScreenState &s)
{
    m_title->setText(s.title().isEmpty() ? m_commandName : s.title());
    m_title->setVisible(!m_title->text().isEmpty());
    m_version->setText(s.programVersion().isEmpty() ? QString()
                                                    : utils::tr(QStringLiteral("kip.view.version")).arg(s.programVersion()));
    m_version->setVisible(!s.programVersion().isEmpty());

    const core::KipSessionState state = s.sessionState();
    const bool busy = !core::kipIsTerminalState(state) && !s.done() && !s.awaitingInput()
        && (m_session || m_staticState) && state != core::KipSessionState::AwaitingInput;
    m_spinner->setVisible(busy);
    m_statusText->setVisible(busy);
    m_statusText->setText(utils::tr(state == core::KipSessionState::Handshaking
                                        ? QStringLiteral("kip.status.starting")
                                        : QStringLiteral("kip.status.working")));
}

void KipView::refreshBlocks(const core::KipScreenState &s, bool show)
{
    QVector<KipBlockWidget *> next;
    if (show) {
        for (int i = 0; i < s.blocks().size(); ++i) {
            const core::KipBlock &block = s.blocks().at(i);
            KipBlockWidget *reuse = (i < m_blocks.size() && m_blocks.at(i)->accepts(block)) ? m_blocks.at(i) : nullptr;
            if (reuse) {
                reuse->update(block);
                next.append(reuse);
            } else {
                next.append(createKipBlockWidget(block, m_blocksLayout->parentWidget()));
            }
        }
    }
    if (next == m_blocks) {
        return;
    }
    clearLayout(m_blocksLayout, false);
    for (KipBlockWidget *old : m_blocks) {
        if (!next.contains(old)) {
            kipDiscard(old);
        }
    }
    for (KipBlockWidget *w : next) {
        m_blocksLayout->addWidget(w);
        w->show();
    }
    m_blocks = next;
}

// ============================================================== prompt / confirm

void KipView::refreshPrompt(const core::KipScreenState &s, bool show)
{
    if (!show) {
        if (m_promptCard) {
            kipDiscard(m_promptCard);
            m_promptCard = nullptr;
            m_promptWorking = nullptr;
            m_promptWorkingText = nullptr;
            m_promptMessage = nullptr;
            m_fieldsHost = nullptr;
            m_fieldsLayout = nullptr;
            m_chipHost = nullptr;
            m_chipBar = nullptr;
            m_chipBox = nullptr;
            m_slots.clear();
            m_groups.clear();
            m_promptSerial = 0;
        }
        return;
    }
    const core::KipOpenScreen &open = *s.openScreen();
    if (!m_promptCard || m_promptSerial != open.serial) {
        rebuildPromptCard(open);
    }
    // Atualizações da MESMA tela: aviso do `invalid`, estado "trabalhando",
    // campos novos/trocados (patch) e erros por campo.
    m_promptMessage->setText(open.message);
    m_promptMessage->setVisible(!open.message.isEmpty());
    const bool working = (open.locked && !core::kipIsTerminalState(s.sessionState())) || open.changePending;
    m_promptWorking->setVisible(working);
    m_promptWorkingText->setText(utils::tr(open.locked ? QStringLiteral("kip.status.working")
                                                       : QStringLiteral("kip.status.updating")));
    if (open.kind == core::KipOpenScreen::Kind::Prompt) {
        syncFields(open);
        syncChips(open);
    }
}

void KipView::syncChips(const core::KipOpenScreen &open)
{
    if (!m_chipBar || !m_chipBox) {
        return;
    }
    m_chipBar->sync(open, m_session != nullptr);
    const core::KipChip *chip = open.chipRun ? open.chipNamed(open.chipRun->chipId) : nullptr;
    if (open.chipRun && chip) {
        m_chipBox->setRun(*chip, *open.chipRun);
        m_chipBox->show();
    } else {
        m_chipBox->hide();
    }
}

void KipView::rebuildPromptCard(const core::KipOpenScreen &open)
{
    if (m_promptCard) {
        kipDiscard(m_promptCard);
    }
    m_slots.clear();
    m_groups.clear();
    m_promptSerial = open.serial;

    m_promptCard = layout_helpers::makeSurfaceCard(m_promptHost);
    auto *card = new QVBoxLayout(m_promptCard);
    card->setContentsMargins(tk::space(5), tk::space(4), tk::space(5), tk::space(4));
    card->setSpacing(tk::space(3));

    // título + indicador "trabalhando"
    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(tk::space(2));
    const bool confirm = open.kind == core::KipOpenScreen::Kind::Confirm;
    if (confirm && open.danger) {
        auto *icon = new QLabel(m_promptCard);
        icon->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        icon->setPixmap(LucideIcons::icon(QStringLiteral("triangle-alert"), QColor(tk::errorFg()), 22).pixmap(22, 22));
        titleRow->addWidget(icon);
    }
    auto *title = new QLabel(open.title, m_promptCard);
    title->setProperty("kaiRole", QStringLiteral("title"));
    title->setWordWrap(true);
    title->setVisible(!open.title.isEmpty());
    titleRow->addWidget(title, 1);
    m_promptWorking = new QWidget(m_promptCard);
    m_promptWorking->setObjectName(QStringLiteral("kipPromptWorking"));
    m_promptWorking->setStyleSheet(QStringLiteral("QWidget#kipPromptWorking { background: transparent; }"));
    auto *workingLayout = new QHBoxLayout(m_promptWorking);
    workingLayout->setContentsMargins(0, 0, 0, 0);
    workingLayout->setSpacing(tk::space(1));
    workingLayout->addWidget(new KipSpinner(m_promptWorking, 16));
    m_promptWorkingText = new QLabel(m_promptWorking);
    m_promptWorkingText->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
    workingLayout->addWidget(m_promptWorkingText);
    m_promptWorking->setVisible(false);
    titleRow->addWidget(m_promptWorking);
    card->addLayout(titleRow);

    if (!confirm && !open.description.isEmpty()) {
        card->addWidget(layout_helpers::makeHintBanner(m_promptCard, open.description));
    }

    // aviso geral do `invalid`
    m_promptMessage = new QLabel(m_promptCard);
    m_promptMessage->setWordWrap(true);
    m_promptMessage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_promptMessage->setContentsMargins(10, 8, 10, 8);
    m_promptMessage->setObjectName(QStringLiteral("kipPromptMessage"));
    {
        const QColor err(tk::errorFg());
        m_promptMessage->setStyleSheet(QStringLiteral(
            "QLabel#kipPromptMessage { background-color: rgba(%1,%2,%3,30); border: 1px solid %4;"
            " border-radius: %5px; color: %4; }")
            .arg(err.red()).arg(err.green()).arg(err.blue()).arg(tk::errorFg()).arg(tk::radiusMd()));
    }
    m_promptMessage->setVisible(false);
    card->addWidget(m_promptMessage);

    if (confirm) {
        auto *text = new QLabel(open.text, m_promptCard);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::fg()));
        card->addWidget(text);
    } else {
        m_fieldsHost = new QWidget(m_promptCard);
        m_fieldsHost->setObjectName(QStringLiteral("kipFieldsHost"));
        m_fieldsHost->setStyleSheet(QStringLiteral("QWidget#kipFieldsHost { background: transparent; }"));
        m_fieldsLayout = new QVBoxLayout(m_fieldsHost);
        m_fieldsLayout->setContentsMargins(0, 0, 0, 0);
        m_fieldsLayout->setSpacing(tk::space(4));
        card->addWidget(m_fieldsHost);
    }
    if (!confirm) {
        // chips (§21): botões efêmeros e a caixa da execução, logo abaixo dos campos
        m_chipHost = new QWidget(m_promptCard);
        m_chipHost->setObjectName(QStringLiteral("kipChipHost"));
        m_chipHost->setStyleSheet(QStringLiteral("QWidget#kipChipHost { background: transparent; }"));
        auto *chipLayout = new QVBoxLayout(m_chipHost);
        chipLayout->setContentsMargins(0, 0, 0, 0);
        chipLayout->setSpacing(tk::space(2));
        m_chipBar = new KipChipBar(m_chipHost);
        m_chipBox = new KipChipBox(m_chipHost);
        m_chipBox->hide();
        chipLayout->addWidget(m_chipBar);
        chipLayout->addWidget(m_chipBox);
        card->addWidget(m_chipHost);
        connect(m_chipBar, &KipChipBar::chipClicked, this, [this](const QString &id) {
            if (m_session) m_session->startChip(id);
        });
        connect(m_chipBox, &KipChipBox::confirmed, this, [this]() {
            if (m_session) m_session->confirmChip();
        });
        connect(m_chipBox, &KipChipBox::dismissed, this, [this]() {
            if (m_session) m_session->dismissChip();
        });
    }
    m_promptHostLayout->addWidget(m_promptCard);
    m_promptCard->show();

    QTimer::singleShot(0, this, [this, serial = open.serial]() {
        if (m_promptSerial == serial) {
            focusFirstField();
        }
    });
}

KipView::FieldSlot KipView::makeFieldSlot(const core::KipField &field, const QJsonValue &value)
{
    FieldSlot slot;
    slot.name = field.name;
    slot.group = field.group.trimmed();
    slot.signature = fieldSignature(field);
    slot.editor = new KipFieldEditor(field, value, m_fieldsHost);
    const QString label = field.label.isEmpty() ? field.name : field.label;
    slot.wrap = fields::wrapWithLabel(m_fieldsHost, label, slot.editor->widget(), field.required);
    auto *wrapLayout = qobject_cast<QVBoxLayout *>(slot.wrap->layout());
    if (!field.description.isEmpty()) {
        auto *desc = new QLabel(field.description, slot.wrap);
        desc->setWordWrap(true);
        desc->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        desc->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;").arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
        wrapLayout->insertWidget(1, desc);
    }
    slot.error = new QLabel(slot.wrap);
    slot.error->setWordWrap(true);
    slot.error->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    slot.error->setStyleSheet(QStringLiteral("color: %1;").arg(tk::errorFg()));
    slot.error->setVisible(false);
    wrapLayout->addWidget(slot.error);

    KipFieldEditor *editor = slot.editor;
    connect(editor, &KipFieldEditor::valueChanged, this, [this, editor, name = field.name]() {
        if (m_session) {
            m_session->setFieldValue(name, editor->value());
        }
        updateSubmitEnabled();
    });
    if (field.watch) {
        connect(editor, &KipFieldEditor::userEdited, this, [this, name = field.name]() { handleFieldEdited(name); });
    }
    installSubmitFilter(editor->widget());
    return slot;
}

void KipView::syncFields(const core::KipOpenScreen &open)
{
    QVector<FieldSlot> next;
    bool structural = m_slots.size() != open.fields.size();
    for (int i = 0; i < open.fields.size(); ++i) {
        const core::KipField &field = open.fields.at(i);
        const QByteArray sig = fieldSignature(field);
        FieldSlot reuse;
        for (const FieldSlot &old : m_slots) {
            if (old.name == field.name) {
                reuse = old;
                break;
            }
        }
        if (reuse.wrap && reuse.signature == sig) {
            next.append(reuse);
            structural = structural || (i >= m_slots.size() || m_slots.at(i).name != field.name);
        } else {
            if (reuse.wrap) {
                kipDiscard(reuse.wrap);
            }
            next.append(makeFieldSlot(field, open.values.value(field.name)));
            structural = true;
        }
    }
    for (const FieldSlot &old : m_slots) {
        bool kept = false;
        for (const FieldSlot &n : next) {
            if (n.wrap == old.wrap) kept = true;
        }
        if (!kept && old.wrap) {
            kipDiscard(old.wrap);
            structural = true;
        }
    }
    m_slots = next;
    if (structural || m_fieldsLayout->count() == 0) {
        placeFields(open);
    }

    // erros por campo + travamento
    const bool locked = open.locked;
    for (FieldSlot &slot : m_slots) {
        const QString error = open.errors.value(slot.name);
        slot.error->setText(error);
        slot.error->setVisible(!error.isEmpty());
        if (slot.editor) slot.editor->setReadOnly(locked);
        if (!error.isEmpty() && !slot.group.isEmpty() && m_groups.contains(slot.group)) {
            m_groups[slot.group].card->setExpanded(true, false);
        }
    }
}

void KipView::placeFields(const core::KipOpenScreen &open)
{
    clearLayout(m_fieldsLayout, false);
    for (GroupSlot &group : m_groups) {
        clearLayout(group.layout, false);
    }
    QSet<QString> used;
    for (const core::KipField &field : open.fields) {
        FieldSlot *slot = nullptr;
        for (FieldSlot &s : m_slots) {
            if (s.name == field.name) slot = &s;
        }
        if (!slot) continue;
        if (slot->group.isEmpty()) {
            m_fieldsLayout->addWidget(slot->wrap);
        } else {
            if (!m_groups.contains(slot->group)) {
                GroupSlot group;
                group.body = new QWidget(m_fieldsHost);
                group.body->setObjectName(QStringLiteral("kipGroupBody"));
                group.body->setStyleSheet(QStringLiteral("QWidget#kipGroupBody { background: transparent; }"));
                group.layout = new QVBoxLayout(group.body);
                group.layout->setContentsMargins(0, 0, 0, 0);
                group.layout->setSpacing(tk::space(4));
                group.card = new CollapsibleSectionCard(slot->group, m_fieldsHost);
                group.card->setAlwaysShowBody(true);
                group.card->setShowCountBadge(false);
                group.card->setExpanded(false, false); // recolhido por padrão (§6)
                group.card->setBody(group.body);
                m_groups.insert(slot->group, group);
            }
            if (!used.contains(slot->group)) {
                m_fieldsLayout->addWidget(m_groups[slot->group].card);
                used.insert(slot->group);
            }
            m_groups[slot->group].layout->addWidget(slot->wrap);
        }
        slot->wrap->show();
    }
    // grupos que ficaram sem campos somem
    for (auto it = m_groups.begin(); it != m_groups.end();) {
        if (!used.contains(it.key())) {
            kipDiscard(it->card);
            it = m_groups.erase(it);
        } else {
            ++it;
        }
    }
}

void KipView::focusFirstField()
{
    const core::KipScreenState &s = *screen();
    const auto &open = s.openScreen();
    if (!open || open->locked) {
        return;
    }
    // Não rouba o foco de quem está digitando em outro lugar (filosofia do app).
    QWidget *current = QApplication::focusWidget();
    const bool inside = current && (current == this || isAncestorOf(current));
    const bool typingElsewhere = current && !inside
        && (qobject_cast<QLineEdit *>(current) || qobject_cast<QPlainTextEdit *>(current)
            || qobject_cast<QAbstractSpinBox *>(current));
    if (typingElsewhere) {
        return;
    }
    if (open->kind == core::KipOpenScreen::Kind::Confirm) {
        (open->danger ? m_decline : m_confirm)->setFocus(Qt::OtherFocusReason);
        return;
    }
    for (const FieldSlot &slot : m_slots) {
        if (slot.editor && slot.editor->focusTarget() && slot.editor->widget()->isEnabled()) {
            slot.editor->focusTarget()->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
    m_submit->setFocus(Qt::OtherFocusReason);
}

// ============================================================== validação / watch

bool KipView::allRequiredFilled() const
{
    for (const FieldSlot &slot : m_slots) {
        if (slot.editor && slot.editor->field().required && !slot.editor->isFilled()) {
            return false;
        }
    }
    return true;
}

int KipView::pendingRequiredIn(const QString &group) const
{
    int pending = 0;
    for (const FieldSlot &slot : m_slots) {
        if (slot.group == group && slot.editor && slot.editor->field().required && !slot.editor->isFilled()) {
            ++pending;
        }
    }
    return pending;
}

void KipView::updateSubmitEnabled()
{
    const core::KipScreenState &s = *screen();
    const auto &open = s.openScreen();
    const bool prompt = open && open->kind == core::KipOpenScreen::Kind::Prompt;
    const bool unlocked = open && !open->locked && !core::kipIsTerminalState(s.sessionState());
    m_submit->setEnabled(prompt && unlocked && !open->changePending && !m_changeTimer.isActive()
                         && allRequiredFilled());
    if (prompt && m_chipBar) {
        m_chipBar->sync(*open, m_session != nullptr);
    }
    // Selo de obrigatórios pendentes nos grupos (só aparece quando há algum).
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        const int pending = pendingRequiredIn(it.key());
        it->card->setCount(pending);
        it->card->setShowCountBadge(pending > 0);
    }
}

void KipView::handleFieldEdited(const QString &name)
{
    if (!m_session) {
        return;
    }
    KipFieldEditor *editor = editorFor(name);
    if (editor && editor->isTextLike()) {
        m_pendingChangeField = name;
        m_changeTimer.start(); // 300 ms de debounce
        updateSubmitEnabled();
    } else {
        m_pendingChangeField = name;
        sendPendingChange();
    }
}

void KipView::sendPendingChange()
{
    if (m_session && !m_pendingChangeField.isEmpty()) {
        const QString field = m_pendingChangeField;
        m_pendingChangeField.clear();
        m_session->sendChange(field);
    }
    updateSubmitEnabled();
}

// ============================================================== resultado

void KipView::refreshResult(const core::KipScreenState &s)
{
    const core::KipSessionState state = s.sessionState();
    if (isBadTerminal(state)) {
        applyTerminalCard(s);
    } else if (s.done()) {
        applyDoneCard(*s.done(), state == core::KipSessionState::Finished);
    } else if (state == core::KipSessionState::Finished) {
        m_result->clearButtons();
        m_result->setContent(QColor(tk::successFg()), QStringLiteral("circle-check"),
                             utils::tr(QStringLiteral("kip.result.finished.title")),
                             utils::tr(QStringLiteral("kip.result.finished.text")));
        QPushButton *again = m_result->addButton(utils::tr(QStringLiteral("kip.action.run_again")),
                                                 QStringLiteral("refresh-cw"), true);
        connect(again, &QPushButton::clicked, this, &KipView::runAgainRequested);
        m_result->setVisible(true);
    } else {
        m_result->setVisible(false);
    }
}

void KipView::applyTerminalCard(const core::KipScreenState &s)
{
    const core::KipSessionState state = s.sessionState();
    const core::KipTerminalInfo &info = s.terminalInfo();
    m_result->clearButtons();
    QColor color(tk::errorFg());
    QString icon = QStringLiteral("circle-x");
    QString title;
    QString text;
    switch (state) {
    case core::KipSessionState::Unsupported:
        color = QColor(tk::warningFg());
        icon = QStringLiteral("triangle-alert");
        title = utils::tr(QStringLiteral("kip.result.unsupported.title"));
        text = utils::tr(info.reason == core::KipTerminalReason::HandshakeTimeout
                             ? QStringLiteral("kip.result.unsupported.timeout")
                             : QStringLiteral("kip.result.unsupported.exited"));
        break;
    case core::KipSessionState::ProtocolError:
        title = utils::tr(QStringLiteral("kip.result.protocol_error.title"));
        text = engine::kipTerminalMessage(state, info);
        break;
    case core::KipSessionState::Cancelled:
        color = QColor(tk::mutedFg());
        icon = QStringLiteral("circle-alert");
        title = utils::tr(QStringLiteral("kip.result.cancelled.title"));
        text = utils::tr(QStringLiteral("kip.result.cancelled.text"));
        break;
    default:
        title = utils::tr(QStringLiteral("kip.result.failed.title"));
        text = engine::kipTerminalMessage(state, info);
        break;
    }
    m_result->setContent(color, icon, title, text);
    QPushButton *again = m_result->addButton(utils::tr(QStringLiteral("kip.action.run_again")),
                                             QStringLiteral("refresh-cw"), true);
    connect(again, &QPushButton::clicked, this, &KipView::runAgainRequested);
    m_result->setVisible(true);
}

void KipView::applyDoneCard(const core::KipDone &done, bool finished)
{
    m_result->clearButtons();
    const QColor color = kipLevelColor(done.level);
    m_result->setContent(color, kipLevelIcon(done.level),
                         done.title.isEmpty() ? utils::tr(QStringLiteral("kip.result.finished.title")) : done.title,
                         done.text);
    for (const core::KipAction &action : done.actions) {
        const QString icon = action.type == core::KipActionType::OpenUrl ? QStringLiteral("external-link")
            : action.type == core::KipActionType::Reveal ? QStringLiteral("folder-open") : QStringLiteral("copy");
        QPushButton *button = m_result->addButton(action.label, icon, false);
        if (action.type == core::KipActionType::Reveal) {
            const bool exists = QFileInfo::exists(nativePath(action.path, action.pathFormat, m_session ? m_session->wslDistro() : QString()));
            button->setEnabled(exists);
            if (!exists) {
                button->setToolTip(utils::tr(QStringLiteral("kip.action.reveal_missing")));
            }
        }
        connect(button, &QPushButton::clicked, this, [this, action, button]() { runAction(action, button); });
    }
    // "Run again" sempre presente (§12.1); só vale com o processo já encerrado.
    QPushButton *again = m_result->addButton(utils::tr(QStringLiteral("kip.action.run_again")),
                                             QStringLiteral("refresh-cw"), true);
    again->setEnabled(finished || !m_session);
    connect(again, &QPushButton::clicked, this, &KipView::runAgainRequested);
    m_result->setVisible(true);
}

void KipView::runAction(const core::KipAction &action, QPushButton *button)
{
    switch (action.type) {
    case core::KipActionType::OpenUrl:
        // Já validado no parser; confere de novo — nunca abrir outra coisa que não http(s).
        if (core::kipIsSafeHttpUrl(action.url)) {
            QDesktopServices::openUrl(QUrl(action.url));
        }
        break;
    case core::KipActionType::Reveal: {
        const QString native = nativePath(action.path, action.pathFormat, m_session ? m_session->wslDistro() : QString());
        if (QFileInfo::exists(native)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(folderToReveal(native)));
        } else {
            button->setEnabled(false);
            button->setToolTip(utils::tr(QStringLiteral("kip.action.reveal_missing")));
        }
        break;
    }
    case core::KipActionType::Copy: {
        QApplication::clipboard()->setText(action.value);
        // Retorno breve: "Copiado" no próprio botão.
        const QString original = button->text();
        const QIcon originalIcon = button->icon();
        button->setText(utils::tr(QStringLiteral("kip.action.copied")));
        button->setIcon(LucideIcons::icon(QStringLiteral("check"), QColor(tk::successFg()), 16));
        QPointer<QPushButton> guard(button);
        QTimer::singleShot(1500, button, [guard, original, originalIcon]() {
            if (guard) {
                guard->setText(original);
                guard->setIcon(originalIcon);
            }
        });
        break;
    }
    }
}

// ============================================================== rodapé

void KipView::refreshFooter(const core::KipScreenState &s)
{
    const core::KipSessionState state = s.sessionState();
    const auto &open = s.openScreen();
    const bool terminal = core::kipIsTerminalState(state) || s.done().has_value();
    const bool live = m_session != nullptr;
    const bool prompt = open && open->kind == core::KipOpenScreen::Kind::Prompt;
    const bool confirm = open && open->kind == core::KipOpenScreen::Kind::Confirm;
    const bool unlocked = open && !open->locked;
    const bool cancelling = m_session && m_session->cancelRequested();

    m_footer->setVisible(!terminal);
    m_back->setVisible(open && unlocked && open->back);
    m_back->setEnabled(live);

    m_cancel->setVisible(!terminal && s.cancellable());
    m_cancel->setText(utils::tr(cancelling ? QStringLiteral("kip.action.cancelling")
                                : confirm ? QStringLiteral("kip.action.cancel_run")
                                          : QStringLiteral("kip.action.cancel")));
    m_cancel->setEnabled(live && !cancelling);

    m_submit->setVisible(prompt);
    m_submit->setText(prompt && !open->submitLabel.isEmpty() ? open->submitLabel
                                                             : utils::tr(QStringLiteral("kip.action.continue")));
    m_decline->setVisible(confirm);
    m_confirm->setVisible(confirm);
    if (confirm) {
        m_decline->setText(open->cancelLabel.isEmpty() ? utils::tr(QStringLiteral("kip.action.decline"))
                                                       : open->cancelLabel);
        m_confirm->setText(open->confirmLabel.isEmpty() ? utils::tr(QStringLiteral("kip.action.confirm"))
                                                        : open->confirmLabel);
        m_decline->setEnabled(live && unlocked);
        m_confirm->setEnabled(live && unlocked);
        if (open->danger) {
            // Destrutivo: botão na cor de erro do tema; o foco padrão vai para
            // o "recusar" (focusFirstField) — Enter nunca confirma sem querer.
            m_confirm->setProperty("kaiRole", QVariant());
            m_confirm->setStyleSheet(QStringLiteral(
                "QPushButton { background-color: %1; color: #ffffff; border: 1px solid %1;"
                " border-radius: %2px; font-weight: 600; }"
                "QPushButton:hover { background-color: %1; border: 1px solid %3; }"
                "QPushButton:disabled { background-color: transparent; color: %4; border: 1px solid %4; }")
                .arg(tk::errorFg()).arg(tk::radiusMd()).arg(tk::fg(), tk::mutedFg()));
            m_confirm->setIcon(LucideIcons::icon(QStringLiteral("triangle-alert"), QColor(QStringLiteral("#ffffff")), 16));
        } else {
            m_confirm->setStyleSheet(QString());
            m_confirm->setProperty("kaiRole", QStringLiteral("primary"));
            m_confirm->setIcon(QIcon());
        }
        m_confirm->style()->unpolish(m_confirm);
        m_confirm->style()->polish(m_confirm);
    }
    if (prompt && !live) {
        m_submit->setEnabled(false);
    }
}

// ============================================================== teclado

void KipView::installSubmitFilter(QWidget *root)
{
    if (!root) return;
    root->installEventFilter(this);
    for (QWidget *child : root->findChildren<QWidget *>()) {
        if (child->focusPolicy() != Qt::NoFocus) {
            child->installEventFilter(this);
        }
    }
}

bool KipView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (QApplication::activePopupWidget()) {
                return QWidget::eventFilter(watched, event); // lista/completer abertos tratam a tecla
            }
            if (auto *button = qobject_cast<QPushButton *>(watched)) {
                button->click(); // botões simples não reagem a Enter fora de um QDialog
                return true;
            }
            // Em textarea, lista e tabela o Enter é do próprio campo (§7.1).
            if (qobject_cast<QPlainTextEdit *>(watched) || qobject_cast<QTextEdit *>(watched)
                || qobject_cast<QAbstractItemView *>(watched)) {
                return QWidget::eventFilter(watched, event);
            }
            if (m_submit->isVisible() && m_submit->isEnabled()) {
                m_submit->click();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace kai::ui
