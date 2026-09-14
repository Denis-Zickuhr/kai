#include "ui/features/docs/doc-editor-pane.h"

#include "ui/features/docs/doc-code-editor.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <functional>

namespace kai::ui {

namespace tk = utils::tokens;

// O rótulo do veredito: clicável quando aponta um erro.
class ClickableLabel : public QLabel {
public:
    using QLabel::QLabel;
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QLabel::mouseReleaseEvent(event);
        if (onClick && event->button() == Qt::LeftButton) {
            onClick();
        }
    }
};

DocEditorPane::DocEditorPane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("docEditorPane"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_editor = new DocCodeEditor(this);
    layout->addWidget(m_editor, 1);

    auto *status = new QWidget(this);
    status->setObjectName(QStringLiteral("docEditorStatus"));
    auto *row = new QHBoxLayout(status);
    row->setContentsMargins(tk::space(3), tk::space(1), tk::space(3), tk::space(1));
    row->setSpacing(tk::space(4));
    auto *issue = new ClickableLabel(status);
    issue->onClick = [this]() {
        const texttools::Issue found = m_editor->issue();
        if (!found.ok && found.line > 0) {
            m_editor->goToLine(found.line, std::max(1, found.column));
        }
    };
    m_issue = issue;
    m_position = new QLabel(status);
    m_language = new QLabel(status);
    row->addWidget(m_issue, 1);
    row->addWidget(m_position);
    row->addWidget(m_language);
    layout->addWidget(status);

    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        m_message.clear(); // a mensagem rápida vale até o cursor se mexer
        updateStatus();
    });
    connect(m_editor, &DocCodeEditor::issueChanged, this, [this]() { updateStatus(); });
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &DocEditorPane::modifiedChanged);
    refreshStyle();
}

void DocEditorPane::setContent(const QString &text, texttools::Language language)
{
    m_message.clear();
    m_editor->blockSignals(true);
    m_editor->setPlainText(text);
    m_editor->document()->clearUndoRedoStacks();
    m_editor->blockSignals(false);
    m_editor->document()->setModified(false);
    m_editor->setLanguage(language);
    m_editor->moveCursor(QTextCursor::Start);
    updateStatus();
}

QString DocEditorPane::text() const
{
    return m_editor->toPlainText();
}

bool DocEditorPane::isModified() const
{
    return m_editor->document()->isModified();
}

void DocEditorPane::markSaved()
{
    m_editor->document()->setModified(false);
}

void DocEditorPane::showMessage(const QString &text, bool error)
{
    m_message = text;
    m_messageIsError = error;
    updateStatus();
}

void DocEditorPane::refreshStyle()
{
    m_editor->refreshStyle();
    if (QWidget *status = findChild<QWidget *>(QStringLiteral("docEditorStatus"))) {
        status->setStyleSheet(QStringLiteral("QWidget#docEditorStatus { background-color: %1; border-top: 1px solid %2; }")
                                  .arg(tk::altBg(), tk::borderColor()));
    }
    for (QLabel *label : {m_position, m_language}) {
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(tk::mutedFg()));
    }
    updateStatus();
}

QString DocEditorPane::statusText() const
{
    return m_issue->text();
}

void DocEditorPane::updateStatus()
{
    const QTextCursor cursor = m_editor->textCursor();
    m_position->setText(utils::tr(QStringLiteral("doc.edit.position")).arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    m_language->setText(texttools::languageName(m_editor->language()));
    QString text;
    QString color = tk::mutedFg();
    const texttools::Issue issue = m_editor->issue();
    if (!m_message.isEmpty()) {
        text = m_message;
        color = m_messageIsError ? tk::errorFg() : tk::successFg();
    } else if (!issue.ok) {
        text = issue.line > 0 ? utils::tr(QStringLiteral("doc.edit.error_at")).arg(issue.line).arg(std::max(1, issue.column)).arg(issue.message)
                              : issue.message;
        color = tk::errorFg();
        m_issue->setCursor(Qt::PointingHandCursor);
    } else if (m_editor->language() == texttools::Language::Json || m_editor->language() == texttools::Language::Yaml
               || m_editor->language() == texttools::Language::Xml) {
        text = utils::tr(QStringLiteral("doc.edit.valid")).arg(texttools::languageName(m_editor->language()));
        color = tk::successFg();
    }
    if (issue.ok || !m_message.isEmpty()) {
        m_issue->unsetCursor();
    }
    m_issue->setText(text);
    m_issue->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(color));
}

} // namespace kai::ui
