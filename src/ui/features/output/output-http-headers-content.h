#pragma once

#include "aba-content.h"
#include "utils/translation-manager.h"
#include <QMap>

class QTableWidget;

namespace kai::ui {

class OutputHttpHeadersContent : public AbaContent {
    Q_OBJECT

public:
    explicit OutputHttpHeadersContent(QWidget *parent = nullptr);

    QString label() const override { return utils::tr(QStringLiteral("output.tab.headers")); }
    QString iconName() const override { return QStringLiteral("list"); }

    void setHeaders(const QMap<QString, QString> &headers);
    void clear() override;
    bool hasContent() const override;

    void applyTheme() override;

private:
    QTableWidget *m_table = nullptr;
    QMap<QString, QString> m_headers;
};

} // namespace kai::ui
