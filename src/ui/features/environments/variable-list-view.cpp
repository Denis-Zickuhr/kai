#include "ui/features/environments/variable-list-view.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/shared/lucide-icons.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {

// Texto numa linha só, cortado com reticências conforme a largura disponível.
class ElidedText : public QLabel {
public:
    explicit ElidedText(QWidget *parent) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(tk::space(10));
    }

    void setFullText(const QString &text)
    {
        m_full = text;
        refresh();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        refresh();
    }

private:
    void refresh()
    {
        setText(fontMetrics().elidedText(m_full, Qt::ElideRight, width()));
        setToolTip(text() == m_full ? QString() : m_full);
    }

    QString m_full;
};

class VariableRow : public QWidget {
    Q_OBJECT

public:
    VariableRow(const VariableRowSpec &spec, QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("varRow"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral(
            "QWidget#varRow { background: transparent; border: none; border-radius: %1px; }"
            "QWidget#varRow:hover { background-color: %2; }").arg(tk::radiusMd()).arg(tk::hoverBg()));
        setFixedHeight(tk::controlHeight() + tk::space(3));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(tk::space(3), 0, tk::space(2), 0);
        layout->setSpacing(tk::space(3));

        auto *keyLabel = new ElidedText(this);
        keyLabel->setFont(tk::monoFont());
        keyLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; font-weight: 600; }").arg(tk::fg()));
        keyLabel->setFullText(spec.key);
        layout->addWidget(keyLabel, 2);

        auto *valueLabel = new ElidedText(this);
        valueLabel->setFont(tk::monoFont());
        valueLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }").arg(tk::mutedFg()));
        const bool masked = spec.secret && !spec.revealed;
        valueLabel->setFullText(masked ? QString(8, QChar(0x2022)) : QString(spec.value).replace(QLatin1Char('\n'), QLatin1Char(' ')));
        layout->addWidget(valueLabel, 3);

        if (!spec.pill.isEmpty()) {
            auto *pill = new QLabel(spec.pill, this);
            pill->setToolTip(spec.pill);
            pill->setMaximumWidth(tk::space(40));
            pill->setStyleSheet(QStringLiteral(
                "QLabel { background-color: rgba(%1, %2, %3, 38); color: %4; border: none; border-radius: %5px;"
                " padding: 1px %6px; font-weight: 600; font-size: %7pt; }")
                .arg(spec.pillColor.red()).arg(spec.pillColor.green()).arg(spec.pillColor.blue())
                .arg(spec.pillColor.name()).arg(tk::radiusSm()).arg(tk::space(2)).arg(tk::fontSizeSmallPt()));
            layout->addWidget(pill);
        }

        if (spec.secret || spec.persisted) {
            auto *lock = new QLabel(this);
            lock->setFixedSize(tk::space(4), tk::space(4));
            lock->setStyleSheet(QStringLiteral("QLabel { background: transparent; border: none; }"));
            lock->setPixmap(LucideIcons::icon(QStringLiteral("lock"),
                                              QColor(spec.secret ? tk::warningFg() : tk::mutedFg()), 14).pixmap(14, 14));
            lock->setAlignment(Qt::AlignCenter);
            layout->addWidget(lock);
        }
        if (spec.secret) {
            auto *eye = makeIconButton(this, spec.revealed ? QStringLiteral("eye-off") : QStringLiteral("eye"),
                utils::tr(spec.revealed ? QStringLiteral("keyvalue.secret.hide") : QStringLiteral("keyvalue.secret.reveal")),
                QColor(tk::mutedFg()));
            connect(eye, &QToolButton::clicked, this, &VariableRow::revealToggled);
            layout->addWidget(eye);
        }

        auto *edit = makeIconButton(this, QStringLiteral("pencil"), utils::tr(QStringLiteral("keyvalue.edit_row")),
                                    QColor(tk::mutedFg()));
        connect(edit, &QToolButton::clicked, this, &VariableRow::editRequested);
        layout->addWidget(edit);
        auto *del = makeIconButton(this, QStringLiteral("trash-2"), utils::tr(QStringLiteral("env.action.delete")),
                                   QColor(tk::errorFg()));
        connect(del, &QToolButton::clicked, this, &VariableRow::deleteRequested);
        layout->addWidget(del);
    }

signals:
    void editRequested();
    void deleteRequested();
    void revealToggled();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit editRequested();
        }
    }
};

QWidget *makeSeparator(QWidget *parent)
{
    auto *line = new QWidget(parent);
    line->setObjectName(QStringLiteral("varSeparator"));
    line->setAttribute(Qt::WA_StyledBackground, true);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("QWidget#varSeparator { background-color: %1; border: none; }").arg(tk::borderColor()));
    return line;
}

} // namespace

VariableListView::VariableListView(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("varBox"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QFrame#varBox { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
        .arg(tk::bg(), tk::borderColor()).arg(tk::radiusMd()));

    auto *root = new QVBoxLayout(this);
    const int inset = tk::space(1) + 1;
    root->setContentsMargins(inset, inset, inset, inset);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("varStack"));
    m_stack->setStyleSheet(QStringLiteral("QStackedWidget#varStack { background: transparent; border: none; }"));

    m_scroll = new QScrollArea(m_stack);
    m_scroll->setObjectName(QStringLiteral("varScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea#varScroll { background: transparent; border: none; }"));
    m_scroll->viewport()->setAutoFillBackground(false);
    m_listHost = new QWidget();
    m_listHost->setObjectName(QStringLiteral("varList"));
    m_listHost->setStyleSheet(QStringLiteral("QWidget#varList { background: transparent; }"));
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);
    m_scroll->setWidget(m_listHost);
    m_stack->addWidget(m_scroll);

    auto *empty = new QWidget(m_stack);
    empty->setObjectName(QStringLiteral("varEmpty"));
    empty->setStyleSheet(QStringLiteral("QWidget#varEmpty { background: transparent; }"));
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setSpacing(tk::space(2));
    emptyLayout->addStretch(1);
    auto *icon = new QLabel(empty);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QStringLiteral("QLabel { background: transparent; border: none; }"));
    icon->setPixmap(LucideIcons::icon(QStringLiteral("variable"), QColor(tk::mutedFg()), 28).pixmap(28, 28));
    emptyLayout->addWidget(icon);
    m_emptyText = new QLabel(empty);
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    m_emptyText->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; border: none; }").arg(tk::mutedFg()));
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addStretch(2);
    m_stack->addWidget(empty);

    root->addWidget(m_stack);
}

void VariableListView::setEmptyText(const QString &text)
{
    m_emptyText->setText(text);
}

void VariableListView::setRows(const QVector<VariableRowSpec> &rows)
{
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (i > 0) {
            m_listLayout->addWidget(makeSeparator(m_listHost));
        }
        auto *row = new VariableRow(rows.at(i), m_listHost);
        connect(row, &VariableRow::editRequested, this, [this, i]() { emit editRequested(i); });
        connect(row, &VariableRow::deleteRequested, this, [this, i]() { emit deleteRequested(i); });
        connect(row, &VariableRow::revealToggled, this, [this, i]() { emit revealToggled(i); });
        m_listLayout->addWidget(row);
    }
    m_listLayout->addStretch(1);
    m_stack->setCurrentIndex(rows.isEmpty() ? 1 : 0);
}

} // namespace kai::ui

#include "variable-list-view.moc"
