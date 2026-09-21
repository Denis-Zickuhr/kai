#include "output-http-headers-content.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>

namespace kai::ui {
namespace tk = utils::tokens;

OutputHttpHeadersContent::OutputHttpHeadersContent(QWidget *parent)
    : AbaContent(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2));

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({
        utils::tr(QStringLiteral("output.headers.key")),
        utils::tr(QStringLiteral("output.headers.value"))
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    layout->addWidget(m_table);

    setLayout(layout);
    applyTheme();
}

void OutputHttpHeadersContent::setHeaders(const QMap<QString, QString> &headers)
{
    m_headers = headers;
    m_table->setRowCount(0);

    int row = 0;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        m_table->insertRow(row);

        auto *keyItem = new QTableWidgetItem(it.key());
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, keyItem);

        auto *valueItem = new QTableWidgetItem(it.value());
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 1, valueItem);

        ++row;
    }
}

void OutputHttpHeadersContent::clear()
{
    m_headers.clear();
    m_table->setRowCount(0);
}

bool OutputHttpHeadersContent::hasContent() const
{
    return !m_headers.isEmpty();
}

void OutputHttpHeadersContent::applyTheme()
{
    this->setStyleSheet(QString(
        "OutputHttpHeadersContent { background-color: %1; color: %2; border-radius: %3px; }"
    ).arg(tk::surface()).arg(tk::fg()).arg(tk::radiusMd()));
}

} // namespace kai::ui
