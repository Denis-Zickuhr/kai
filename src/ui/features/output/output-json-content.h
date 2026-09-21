#pragma once

#include "aba-content.h"
#include "utils/translation-manager.h"

namespace kai::ui {
class JsonViewerWidget;
}

namespace kai::ui {

class OutputJsonContent : public AbaContent {
    Q_OBJECT

public:
    explicit OutputJsonContent(QWidget *parent = nullptr);

    QString label() const override { return utils::tr(QStringLiteral("output.tab.json")); }
    QString iconName() const override { return QStringLiteral("braces"); }

    void setJson(const QString &jsonText);
    void clear() override;
    bool hasContent() const override;

    void applyTheme() override;

private:
    JsonViewerWidget *m_viewer = nullptr;
};

} // namespace kai::ui
