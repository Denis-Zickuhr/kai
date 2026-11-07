#include "ui/features/command-editor/pipeline-step-dialog.h"

#include "ui/shared/collapsible-section-card.h"
#include "ui/shared/dialog-utils.h"
#include "ui/features/command-editor/step-rows-editor.h"
#include "ui/shared/inline-code-field.h"
#include "ui/shared/parameter-field-factory.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kai::ui {

namespace {
namespace tk = kai::utils::tokens;

QWidget *transparentHost(QWidget *parent, const QString &objectName)
{
    auto *host = new QWidget(parent);
    host->setObjectName(objectName);
    host->setStyleSheet(QStringLiteral("QWidget#%1 { background: transparent; }").arg(objectName));
    return host;
}

QLabel *errorLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    label->setVisible(false);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(tk::errorFg()));
    return label;
}

// Rótulo empilhado acima do campo + (opcional) mensagem de erro abaixo.
QWidget *fieldBlock(QWidget *parent, const QString &label, QWidget *field, bool required = false,
                    QLabel *error = nullptr, const QString &tip = QString())
{
    auto *host = transparentHost(parent, QStringLiteral("stepRowBlock"));
    auto *vbox = new QVBoxLayout(host);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(tk::space(1));
    if (!tip.isEmpty()) {
        field->setToolTip(tip);
    }
    vbox->addWidget(fields::wrapWithLabel(host, label, field, required));
    if (error) {
        error->setParent(host);
        vbox->addWidget(error);
    }
    return host;
}

bool validStepId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9_]+$"));
    return re.match(id).hasMatch();
}

QSpinBox *spin(QWidget *parent, int min, int max, int value)
{
    auto *box = new QSpinBox(parent);
    box->setRange(min, max);
    box->setValue(value);
    return box;
}

} // namespace

PipelineStepDialog::PipelineStepDialog(const core::PipelineStep &step, const QStringList &otherIds,
                                       const QStringList &laterIds, const QStringList &commandRefs, QWidget *parent)
    : QDialog(parent)
    , m_otherIds(otherIds)
    , m_laterIds(laterIds)
{
    setWindowTitle(utils::tr(QStringLiteral("pipeline.step.edit")));
    setSizeGripEnabled(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(7), tk::space(6), tk::space(7), tk::space(5));
    outer->setSpacing(tk::space(4));
    outer->addWidget(layout_helpers::makeHintBanner(this, utils::tr(QStringLiteral("pipeline.step.hint"))));

    auto *scrollContent = transparentHost(this, QStringLiteral("stepRowScrollHost"));
    auto *contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setContentsMargins(0, 0, tk::space(1), 0);
    contentLayout->setSpacing(tk::space(4));

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    scrollArea->setWidget(scrollContent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->viewport()->setObjectName(QStringLiteral("stepRowScrollViewport"));
    scrollArea->viewport()->setStyleSheet(QStringLiteral("QWidget#stepRowScrollViewport { background: transparent; }"));
    outer->addWidget(scrollArea, 1);

    // Só Identificação e Execução nascem abertas; as demais abrem sozinhas apenas quando o passo já tem algo nelas.
    auto addSection = [&](const QString &title, QWidget *body, bool open = false) {
        auto *card = new CollapsibleSectionCard(title, this);
        card->setAlwaysShowBody(true);
        card->setShowCountBadge(false);
        card->setBody(body);
        card->setExpanded(open, false);
        contentLayout->addWidget(card);
        return card;
    };
    auto makeGrid = [&](QWidget *host) {
        auto *grid = new QGridLayout(host);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(tk::space(5));
        grid->setVerticalSpacing(tk::space(4));
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        return grid;
    };
    auto tr = [](const char *key) { return utils::tr(QString::fromLatin1(key)); };
    // Seletor de comando: lista os comandos do projeto, mas segue editável (referência relativa ou ainda inexistente).
    // O QLineEdit interno é o campo que o resto do diálogo lê e valida.
    auto makeRefPicker = [&](QLineEdit *&line, const QString &objectName, const QString &initial) {
        auto *combo = new QComboBox(this);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->addItems(commandRefs);
        combo->setCurrentText(initial);
        combo->setMaxVisibleItems(14);
        // Sem isso o combo pede a largura do maior "Pasta/Nome" e o conteúdo estoura o diálogo (cortava a direita).
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        // Pesquisa por qualquer trecho do nome, com o popup no estilo dos demais selects.
        makeSearchableCombo(combo);
        combo->setCurrentText(initial);
        if (combo->completer()) {
            combo->completer()->setMaxVisibleItems(12);
        }
        line = combo->lineEdit();
        line->setObjectName(objectName);
        return combo;
    };

    // Cabeçalho da lista (rótulo + "+ Adicionar") sobre a tabela de linhas.
    auto askBlock = [&](const QString &title, StepRowsEditor *editor, const QString &addText, const QString &tip) {
        auto *host = transparentHost(this, QStringLiteral("stepRowsBlock"));
        auto *vbox = new QVBoxLayout(host);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(tk::space(1));
        auto *head = new QHBoxLayout();
        auto *caption = new QLabel(title.toUpper(), host);
        caption->setToolTip(tip);
        caption->setStyleSheet(QStringLiteral("QLabel { background: transparent; font-weight: 600; }"));
        head->addWidget(caption, 1);
        auto *add = makeAccentAddButton(host, addText);
        connect(add, &QPushButton::clicked, editor, &StepRowsEditor::handleAddRowClicked);
        head->addWidget(add);
        vbox->addLayout(head);
        editor->setParent(host);
        vbox->addWidget(editor);
        return host;
    };

    // Identificação
    {
        auto *body = transparentHost(this, QStringLiteral("stepRowBody"));
        auto *grid = makeGrid(body);
        m_id = new QLineEdit(step.id, this);
        m_id->setObjectName(QStringLiteral("stepIdField"));
        m_idError = errorLabel(this);
        grid->addWidget(fieldBlock(this, tr("pipeline.step.id"), m_id, true, m_idError, tr("pipeline.step.id.tip")), 0, 0);
        m_label = new QLineEdit(step.label, this);
        m_label->setObjectName(QStringLiteral("stepLabelField"));
        m_label->setPlaceholderText(tr("pipeline.step.label.placeholder"));
        grid->addWidget(fieldBlock(this, tr("pipeline.step.label"), m_label), 0, 1);
        addSection(tr("pipeline.step.section.identity"), body, true);
    }

    // Execução
    {
        auto *body = transparentHost(this, QStringLiteral("stepRowBody"));
        auto *grid = makeGrid(body);
        QComboBox *runPicker = makeRefPicker(m_run, QStringLiteral("stepRunField"), step.run);
        m_run->setPlaceholderText(QStringLiteral("steps/deploy"));
        m_runError = errorLabel(this);
        grid->addWidget(fieldBlock(this, tr("pipeline.step.run"), runPicker, true, m_runError, tr("pipeline.step.run.tip")), 0, 0);
        QComboBox *checkPicker = makeRefPicker(m_check, QStringLiteral("stepCheckField"), step.check);
        grid->addWidget(fieldBlock(this, tr("pipeline.step.check"), checkPicker, false, nullptr, tr("pipeline.step.check.tip")), 0, 1);
        m_confirm = new QLineEdit(step.confirm, this);
        m_confirm->setObjectName(QStringLiteral("stepConfirmField"));
        grid->addWidget(fieldBlock(this, tr("pipeline.step.confirm"), m_confirm), 1, 0, 1, 2);
        addSection(tr("pipeline.step.section.execution"), body, true);
    }

    // Perguntas e parâmetros
    {
        auto *body = transparentHost(this, QStringLiteral("stepRowBody"));
        auto *vbox = new QVBoxLayout(body);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(tk::space(4));
        m_ask = new StepRowsEditor(StepRowsEditor::Kind::Ask, this);
        m_ask->setObjectName(QStringLiteral("stepAskField"));
        m_ask->setAskFields(step.ask);
        vbox->addWidget(askBlock(tr("pipeline.step.ask"), m_ask, tr("pipeline.step.ask.add"), tr("pipeline.step.ask.tip")));
        m_params = new StepRowsEditor(StepRowsEditor::Kind::Params, this);
        m_params->setObjectName(QStringLiteral("stepParamsField"));
        m_params->setParams(step.params);
        vbox->addWidget(askBlock(tr("pipeline.step.params"), m_params, tr("pipeline.step.params.add"),
                                 tr("pipeline.step.params.tip")));
        // O campo Valor dos parâmetros oferece as respostas das perguntas como {{ask.nome}}.
        auto refreshSuggestions = [this]() {
            QStringList refs;
            for (const QString &n : m_ask->names()) {
                if (!n.isEmpty()) refs << QStringLiteral("{{ask.%1}}").arg(n);
            }
            m_params->setValueSuggestions(refs);
        };
        connect(m_ask, &StepRowsEditor::changed, this, refreshSuggestions);
        refreshSuggestions();
        addSection(tr("pipeline.step.section.ask"), body, !step.ask.isEmpty() || !step.params.isEmpty());
    }

    // Condições
    {
        auto *body = transparentHost(this, QStringLiteral("stepRowBody"));
        auto *grid = makeGrid(body);
        m_when = new InlineCodeField(this);
        m_when->setObjectName(QStringLiteral("stepWhenField"));
        m_when->setPlainField(true);
        m_when->setEditorTitle(tr("pipeline.step.when"));
        m_when->setPlaceholderText(QStringLiteral("{{TARGET}} | eq | prod"));
        m_when->setLineRange(2, 8);
        m_when->setPlainText(core::pipelineWhenToText(step.when));
        grid->addWidget(fieldBlock(this, tr("pipeline.step.when"), m_when, false, nullptr, tr("pipeline.step.when.tip")), 0, 0, 1, 2);
        m_combinator = new QComboBox(this);
        m_combinator->setObjectName(QStringLiteral("stepCombinatorCombo"));
        m_combinator->addItems({QStringLiteral("and"), QStringLiteral("or")});
        m_combinator->setCurrentText(step.whenCombinator);
        grid->addWidget(fieldBlock(this, tr("pipeline.step.when_combinator"), m_combinator), 1, 0);
        addSection(tr("pipeline.step.section.conditions"), body, !step.when.isEmpty());
    }

    // Falha e tentativas
    {
        auto *body = transparentHost(this, QStringLiteral("stepRowBody"));
        auto *grid = makeGrid(body);
        m_onFail = new QComboBox(this);
        m_onFail->setObjectName(QStringLiteral("stepOnFailCombo"));
        m_onFail->addItem(tr("pipeline.step.on_fail.abort"), QStringLiteral("abort"));
        m_onFail->addItem(tr("pipeline.step.on_fail.continue"), QStringLiteral("continue"));
        m_onFail->addItem(tr("pipeline.step.on_fail.ask"), QStringLiteral("ask"));
        m_onFail->addItem(tr("pipeline.step.on_fail.goto"), QStringLiteral("goto"));
        const QString gotoTarget = step.gotoTarget();
        const int onFailIndex = m_onFail->findData(gotoTarget.isEmpty() ? step.onFail : QStringLiteral("goto"));
        m_onFail->setCurrentIndex(onFailIndex >= 0 ? onFailIndex : 0);
        grid->addWidget(fieldBlock(this, tr("pipeline.step.on_fail"), m_onFail), 0, 0);

        m_gotoTarget = new QComboBox(this);
        m_gotoTarget->setObjectName(QStringLiteral("stepGotoCombo"));
        m_gotoTarget->addItems(m_laterIds);
        if (!gotoTarget.isEmpty()) {
            if (m_gotoTarget->findText(gotoTarget) < 0) {
                m_gotoTarget->addItem(gotoTarget); // destino que deixou de ser válido: aparece e é apontado no erro
            }
            m_gotoTarget->setCurrentText(gotoTarget);
        }
        m_gotoError = errorLabel(this);
        m_gotoBlock = fieldBlock(this, tr("pipeline.step.on_fail.goto_target"), m_gotoTarget, true, m_gotoError);
        grid->addWidget(m_gotoBlock, 0, 1);

        m_retryTimes = spin(this, 0, 99, step.retry.times);
        m_retryTimes->setObjectName(QStringLiteral("stepRetryTimes"));
        grid->addWidget(fieldBlock(this, tr("pipeline.step.retry_times"), m_retryTimes), 1, 0);
        m_retryDelay = spin(this, 0, 3600, step.retry.delaySec);
        m_retryDelay->setObjectName(QStringLiteral("stepRetryDelay"));
        grid->addWidget(fieldBlock(this, tr("pipeline.step.retry_delay"), m_retryDelay), 1, 1);
        m_reask = new QCheckBox(tr("pipeline.step.retry_reask"), this);
        m_reask->setProperty("kaiRole", QStringLiteral("switch"));
        m_reask->setChecked(step.retry.reask);
        grid->addWidget(m_reask, 2, 0, 1, 2);
        addSection(tr("pipeline.step.section.failure"), body, step.onFail != QLatin1String("abort") || !step.retry.isDefault());
    }

    // Espera
    {
        auto *body = transparentHost(this, QStringLiteral("stepRowBody"));
        auto *grid = makeGrid(body);
        QComboBox *waitPicker = makeRefPicker(m_waitCheck, QStringLiteral("stepWaitCheck"), step.waitUntil.check);
        grid->addWidget(fieldBlock(this, tr("pipeline.step.wait_check"), waitPicker, false, nullptr,
                                   tr("pipeline.step.wait_check.tip")), 0, 0, 1, 2);
        m_waitTimeout = spin(this, 1, 86400, step.waitUntil.timeoutSec);
        m_waitTimeout->setObjectName(QStringLiteral("stepWaitTimeout"));
        grid->addWidget(fieldBlock(this, tr("pipeline.step.wait_timeout"), m_waitTimeout), 1, 0);
        m_waitInterval = spin(this, 0, 3600, step.waitUntil.intervalSec);
        m_waitInterval->setObjectName(QStringLiteral("stepWaitInterval"));
        grid->addWidget(fieldBlock(this, tr("pipeline.step.wait_interval"), m_waitInterval), 1, 1);
        addSection(tr("pipeline.step.section.wait"), body, step.waitUntil.isSet());
    }

    // Captura
    {
        auto *body = transparentHost(this, QStringLiteral("stepRowBody"));
        auto *vbox = new QVBoxLayout(body);
        vbox->setContentsMargins(0, 0, 0, 0);
        m_capture = new InlineCodeField(this);
        m_capture->setObjectName(QStringLiteral("stepCaptureField"));
        m_capture->setPlainField(true);
        m_capture->setEditorTitle(tr("pipeline.step.capture"));
        m_capture->setPlaceholderText(QStringLiteral("RELEASE_ID: json:data.id"));
        m_capture->setLineRange(2, 8);
        m_capture->setPlainText(core::pipelineCaptureToText(step.capture));
        vbox->addWidget(fieldBlock(this, tr("pipeline.step.capture"), m_capture, false, nullptr,
                                   tr("pipeline.step.capture.tip")));
        addSection(tr("pipeline.step.section.capture"), body, !step.capture.isEmpty());
    }
    contentLayout->addStretch(1);

    auto *footer = new QHBoxLayout();
    m_summary = errorLabel(this);
    footer->addWidget(m_summary, 1);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(m_buttons);
    outer->addLayout(footer);

    auto touch = [this](const QString &key) { if (!m_touched.contains(key)) m_touched << key; validate(); };
    connect(m_id, &QLineEdit::textEdited, this, [touch](const QString &) { touch(QStringLiteral("id")); });
    connect(m_id, &QLineEdit::textChanged, this, [this](const QString &) { validate(); });
    connect(m_run, &QLineEdit::textEdited, this, [touch](const QString &) { touch(QStringLiteral("run")); });
    connect(m_run, &QLineEdit::textChanged, this, [this](const QString &) { validate(); });
    connect(m_onFail, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyVisibility(); validate(); });
    connect(m_gotoTarget, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { validate(); });

    applyVisibility();
    validate();

    int width = 720;
    int height = 640;
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        if (avail.width() > 400) {
            width = qBound(640, static_cast<int>(avail.width() * 0.42), 780);
        }
        if (avail.height() > 400) {
            height = qBound(520, static_cast<int>(avail.height() * 0.68), 680);
            height = qMin(height, static_cast<int>(avail.height() * 0.92));
        }
        width = qMin(width, static_cast<int>(avail.width() * 0.92));
    }
    setMinimumSize(qMin(560, width), qMin(460, height));
    resize(width, height);
    centerOnParent(this);
}

void PipelineStepDialog::applyVisibility()
{
    m_gotoBlock->setVisible(m_onFail->currentData().toString() == QLatin1String("goto"));
}

void PipelineStepDialog::validate()
{
    int issues = 0;
    int shown = 0;
    auto report = [&](QLabel *label, const QString &touchKey, const QString &message, bool needsTouch) {
        const bool show = !message.isEmpty() && (!needsTouch || m_touched.contains(touchKey));
        label->setText(message);
        label->setVisible(show);
        if (!message.isEmpty()) ++issues;
        if (show) ++shown;
    };

    const QString id = m_id->text().trimmed();
    QString idMessage;
    if (id.isEmpty()) {
        idMessage = utils::tr(QStringLiteral("pipeline.step.error.id_required"));
    } else if (!validStepId(id)) {
        idMessage = utils::tr(QStringLiteral("pipeline.step.error.id_invalid"));
    } else if (m_otherIds.contains(id)) {
        idMessage = utils::tr(QStringLiteral("pipeline.step.error.id_duplicate"));
    }
    report(m_idError, QStringLiteral("id"), idMessage, id.isEmpty());

    report(m_runError, QStringLiteral("run"),
           m_run->text().trimmed().isEmpty() ? utils::tr(QStringLiteral("pipeline.step.error.run_required")) : QString(),
           true);

    QString gotoMessage;
    if (m_onFail->currentData().toString() == QLatin1String("goto") && !m_laterIds.contains(m_gotoTarget->currentText())) {
        gotoMessage = utils::tr(QStringLiteral("pipeline.step.error.goto_target"));
    }
    report(m_gotoError, QStringLiteral("goto"), gotoMessage, false);

    m_valid = issues == 0;
    if (auto *ok = m_buttons->button(QDialogButtonBox::Ok)) {
        ok->setEnabled(m_valid);
        ok->setToolTip(m_valid ? QString() : utils::tr(QStringLiteral("params.ok_disabled.tip")));
    }
    m_summary->setText(utils::tr(QStringLiteral("params.error.summary")).arg(issues));
    m_summary->setVisible(shown > 0);
}

core::PipelineStep PipelineStepDialog::result() const
{
    core::PipelineStep s;
    s.id = m_id->text().trimmed();
    s.label = m_label->text().trimmed();
    s.run = m_run->text().trimmed();
    s.check = m_check->text().trimmed();
    s.confirm = m_confirm->text().trimmed();
    s.ask = m_ask->askFields();
    s.params = m_params->params();
    s.when = core::pipelineWhenFromText(m_when->toPlainText());
    s.whenCombinator = m_combinator->currentText();
    const QString onFail = m_onFail->currentData().toString();
    s.onFail = onFail == QLatin1String("goto") ? QStringLiteral("goto:") + m_gotoTarget->currentText() : onFail;
    s.retry.times = m_retryTimes->value();
    s.retry.delaySec = m_retryDelay->value();
    s.retry.reask = m_reask->isChecked();
    s.waitUntil.check = m_waitCheck->text().trimmed();
    s.waitUntil.timeoutSec = m_waitTimeout->value();
    s.waitUntil.intervalSec = m_waitInterval->value();
    s.capture = core::pipelineCaptureFromText(m_capture->toPlainText());
    return s;
}

} // namespace kai::ui
