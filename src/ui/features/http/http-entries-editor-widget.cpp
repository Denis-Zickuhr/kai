#include "ui/features/http/http-entries-editor-widget.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/features/http/http-ui.h"
#include "ui/shared/table-utils.h"
#include "ui/shared/token-style.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace tk = utils::tokens;

HttpEntriesEditorWidget::HttpEntriesEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("httpEntries"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(2));

    m_rowsLayout = new QVBoxLayout();
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(tk::space(2));
    layout->addLayout(m_rowsLayout);

    m_buttonsRow = new QWidget(this);
    makeTransparentHost(m_buttonsRow, QStringLiteral("httpEntriesButtons"));
    auto *buttons = new QHBoxLayout(m_buttonsRow);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addStretch();
    auto *add = makeAccentAddButton(m_buttonsRow, utils::tr(QStringLiteral("keyvalue.add")));
    connect(add, &QPushButton::clicked, this, &HttpEntriesEditorWidget::handleAddRowClicked);
    buttons->addWidget(add);
    layout->addWidget(m_buttonsRow);
    layout->addStretch(1); // numa página alta as linhas ficam no topo, sem esticar

    applyRowStyle();
}

void HttpEntriesEditorWidget::applyRowStyle()
{
    // Campos e linha redeclaram o raio: o QSS global não sabe o tamanho deles e a preferência de cantos some.
    bindTokenStyle(this, []() {
        const int fieldH = tk::controlHeight();
        return QStringLiteral(
            "QWidget#httpEntries { background: transparent; }"
            "QFrame#httpEntryRow { background-color: %1; border: 1px solid %2; border-radius: %3px; }"
            "QFrame#httpEntryRow QLineEdit, QFrame#httpEntryRow QPlainTextEdit {"
            " min-height: 0px; padding: 0px %4px; border-radius: %5px; }"
            "QFrame#httpEntryRow QPlainTextEdit { padding: %6px %4px; }"
            "QFrame#httpEntryRow QLineEdit[off=\"true\"], QFrame#httpEntryRow QPlainTextEdit[off=\"true\"] { color: %7; }"
            "QFrame#httpEntryRow QCheckBox { background: transparent; }")
            .arg(tk::surface(), tk::borderColor())
            .arg(tk::radiusMdForHeight(fieldH + tk::space(3)))
            .arg(tk::space(2))
            .arg(tk::radiusMdForHeight(fieldH))
            .arg(tk::space(1))
            .arg(tk::mutedFg());
    });
}

void HttpEntriesEditorWidget::setShowOwnAddButton(bool show)
{
    m_buttonsRow->setVisible(show);
}

void HttpEntriesEditorWidget::setFileSupport(bool enabled)
{
    m_fileSupport = enabled;
    for (const Row &row : std::as_const(m_rows)) {
        if (row.file) {
            row.file->setVisible(enabled);
        }
    }
}

void HttpEntriesEditorWidget::setMultilineValue(bool multiline)
{
    if (m_multilineValue == multiline) {
        return;
    }
    const QVector<core::HttpEntry> current = entries();
    m_multilineValue = multiline;
    setEntries(current);
}

HttpEntriesEditorWidget::Row HttpEntriesEditorWidget::makeRow(const core::HttpEntry &entry)
{
    const int fieldH = tk::controlHeight();
    Row row;
    row.frame = new QFrame(this);
    row.frame->setObjectName(QStringLiteral("httpEntryRow"));
    auto *layout = new QHBoxLayout(row.frame);
    layout->setContentsMargins(tk::space(2), tk::space(1), tk::space(1), tk::space(1));
    layout->setSpacing(tk::space(2));

    row.enabled = new QCheckBox(row.frame);
    row.enabled->setProperty("kaiRole", QStringLiteral("switch"));
    row.enabled->setToolTip(utils::tr(QStringLiteral("http.entries.enabled")));
    row.enabled->setChecked(entry.enabled);
    layout->addWidget(row.enabled, 0, Qt::AlignVCenter);

    row.name = new QLineEdit(entry.name, row.frame);
    row.name->setPlaceholderText(utils::tr(QStringLiteral("http.entries.name")));
    row.name->setFixedHeight(fieldH);
    layout->addWidget(row.name, 2);

    if (m_multilineValue) {
        row.valueMulti = new QPlainTextEdit(entry.value, row.frame);
        row.valueMulti->setPlaceholderText(utils::tr(QStringLiteral("http.entries.value")));
        row.valueMulti->setFixedHeight(fieldH * 2);
        row.valueMulti->setTabChangesFocus(true);
        layout->addWidget(row.valueMulti, 3);
    } else {
        row.valueLine = new QLineEdit(entry.value, row.frame);
        row.valueLine->setPlaceholderText(utils::tr(QStringLiteral("http.entries.value")));
        row.valueLine->setFixedHeight(fieldH);
        layout->addWidget(row.valueLine, 3);
    }

    row.file = makeIconButton(row.frame, QStringLiteral("file-text"), utils::tr(QStringLiteral("http.entries.file")),
                              QColor(tk::mutedFg()));
    row.file->setCheckable(true);
    row.file->setChecked(entry.isFile);
    row.file->setVisible(m_fileSupport);
    layout->addWidget(row.file, 0, Qt::AlignVCenter);

    auto *remove = makeRemoveButton(row.frame, QString());
    QFrame *frame = row.frame.data();
    connect(remove, &QToolButton::clicked, this, [this, frame]() { removeRow(frame); });
    layout->addWidget(remove, 0, Qt::AlignVCenter);

    const auto touched = [this, frame]() {
        for (const Row &r : std::as_const(m_rows)) {
            if (r.frame == frame) {
                refreshOffState(r);
            }
        }
        emit changed();
    };
    connect(row.enabled, &QCheckBox::toggled, this, touched);
    connect(row.name, &QLineEdit::textChanged, this, touched);
    if (row.valueLine) {
        connect(row.valueLine, &QLineEdit::textChanged, this, touched);
    } else {
        connect(row.valueMulti, &QPlainTextEdit::textChanged, this, touched);
    }
    connect(row.file, &QToolButton::toggled, this, [this]() { emit changed(); });

    refreshOffState(row);
    return row;
}

void HttpEntriesEditorWidget::refreshOffState(const Row &row)
{
    const QString off = row.enabled->isChecked() ? QStringLiteral("false") : QStringLiteral("true");
    for (QWidget *field : {static_cast<QWidget *>(row.name), static_cast<QWidget *>(row.valueLine),
                           static_cast<QWidget *>(row.valueMulti)}) {
        if (field && field->property("off").toString() != off) {
            field->setProperty("off", off);
            field->style()->unpolish(field);
            field->style()->polish(field);
        }
    }
}

core::HttpEntry HttpEntriesEditorWidget::entryOf(const Row &row) const
{
    core::HttpEntry e;
    e.name = row.name->text().trimmed();
    e.value = row.valueLine ? row.valueLine->text() : row.valueMulti->toPlainText();
    e.enabled = row.enabled->isChecked();
    e.isFile = row.file->isChecked(); // o estado vale mesmo com o botão oculto
    return e;
}

QVector<core::HttpEntry> HttpEntriesEditorWidget::entries() const
{
    QVector<core::HttpEntry> out;
    for (const Row &row : m_rows) {
        const core::HttpEntry e = entryOf(row);
        if (!e.name.isEmpty() || !e.value.isEmpty()) {
            out.append(e);
        }
    }
    return out;
}

void HttpEntriesEditorWidget::clearRows()
{
    for (const Row &row : std::as_const(m_rows)) {
        if (row.frame) {
            m_rowsLayout->removeWidget(row.frame);
            row.frame->deleteLater();
            row.frame->hide();
        }
    }
    m_rows.clear();
}

void HttpEntriesEditorWidget::setEntries(const QVector<core::HttpEntry> &entries)
{
    clearRows();
    for (const core::HttpEntry &e : entries) {
        m_rows.append(makeRow(e));
        m_rowsLayout->addWidget(m_rows.last().frame);
    }
    emit changed();
}

void HttpEntriesEditorWidget::removeRow(QFrame *frame)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).frame == frame) {
            m_rowsLayout->removeWidget(frame);
            frame->hide();
            frame->deleteLater();
            m_rows.remove(i);
            emit changed();
            return;
        }
    }
}

void HttpEntriesEditorWidget::handleAddRowClicked()
{
    m_rows.append(makeRow(core::HttpEntry{}));
    m_rowsLayout->addWidget(m_rows.last().frame);
    emit changed(); // antes do foco: o card mostra o corpo assim que passa a haver linha
    m_rows.last().name->setFocus();
}

} // namespace kai::ui
