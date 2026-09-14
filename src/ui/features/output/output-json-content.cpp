#include "output-json-content.h"
#include "ui/shared/json-viewer-widget.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>

namespace kai::ui {
namespace tk = utils::tokens;

OutputJsonContent::OutputJsonContent(QWidget *parent)
    : AbaContent(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_viewer = new JsonViewerWidget(this);
    m_viewer->setEmbeddedMode(true);  // Sem título redundante no painel
    layout->addWidget(m_viewer);

    setLayout(layout);
    applyTheme();
}

void OutputJsonContent::setJson(const QString &jsonText)
{
    if (m_viewer) {
        m_viewer->setJsonText(jsonText);
    }
}

void OutputJsonContent::clear()
{
    if (m_viewer) {
        m_viewer->setJsonText(QString());
    }
}

bool OutputJsonContent::hasContent() const
{
    if (!m_viewer) {
        return false;
    }
    return !m_viewer->formattedJson().isEmpty();
}

void OutputJsonContent::applyTheme()
{
    this->setStyleSheet(QString(
        "OutputJsonContent { background-color: %1; color: %2; border-radius: %3px; }"
    ).arg(tk::codeBg()).arg(tk::codeFg()).arg(tk::radiusMd()));
}

} // namespace kai::ui
