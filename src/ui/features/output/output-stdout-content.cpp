#include "output-stdout-content.h"
#include "ui/features/output/code-output-view.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QTextOption>
#include <QFont>

namespace kai::ui {
namespace tk = utils::tokens;

OutputStdoutContent::OutputStdoutContent(QWidget *parent)
    : AbaContent(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new CodeOutputView(this);
    m_view->setReadOnly(true);
    layout->addWidget(m_view);

    setLayout(layout);
    applyTheme();
}

QString OutputStdoutContent::label() const
{
    return utils::tr(QStringLiteral("output.tab.stdout"));
}

QString OutputStdoutContent::iconName() const
{
    return QStringLiteral("terminal");
}

void OutputStdoutContent::append(const QString &text, bool /*isError*/)
{
    m_view->appendPlainText(text);
}

void OutputStdoutContent::clear()
{
    m_view->clear();
}

bool OutputStdoutContent::hasContent() const
{
    return !m_view->toPlainText().isEmpty();
}

void OutputStdoutContent::setViewOptions(const ViewOptions &opts)
{
    m_view->setLineNumbersVisible(opts.lineNumbers);
    if (opts.wrap) {
        m_view->setWordWrapMode(QTextOption::WordWrap);
    } else {
        m_view->setWordWrapMode(QTextOption::NoWrap);
    }
    m_view->setFont(QFont(QStringLiteral("Monospace"), opts.fontSize));
}

void OutputStdoutContent::applyTheme()
{
    this->setStyleSheet(QString(
        "OutputStdoutContent { background-color: %1; color: %2; border-radius: %3px; }"
    ).arg(tk::codeBg()).arg(tk::codeFg()).arg(tk::radiusMd()));
}

} // namespace kai::ui
