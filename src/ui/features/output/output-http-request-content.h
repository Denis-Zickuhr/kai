#pragma once

#include "aba-content.h"
#include "utils/translation-manager.h"

namespace kai::ui {
class OutputMetricsHeader;
}

class QLabel;
class QPlainTextEdit;

namespace kai::ui {

class OutputHttpRequestContent : public AbaContent {
    Q_OBJECT

public:
    explicit OutputHttpRequestContent(QWidget *parent = nullptr);

    QString label() const override { return utils::tr(QStringLiteral("output.tab.request")); }
    QString iconName() const override { return QStringLiteral("globe"); }

    void setHttpResult(int statusCode, const QString &reasonPhrase, qint64 elapsedMs, qint64 bodySize,
                       const QString &method, const QString &url, const QString &body, bool success);
    void clear() override;
    bool hasContent() const override;

    void applyTheme() override;

private:
    OutputMetricsHeader *m_metricsHeader = nullptr;
    QLabel *m_methodLabel = nullptr;
    QLabel *m_urlLabel = nullptr;
    QPlainTextEdit *m_bodyDisplay = nullptr;
    QString m_currentBody;
};

} // namespace kai::ui
