#include "ui/features/output/output-panel.h"
#include "ui/shared/token-style.h"
#include "ui/shared/json-viewer-widget.h"
#include "ui/shared/lucide-icons.h"

#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QCheckBox>

#include <QMessageBox>
#include <QSaveFile>

#include <QLabel>
#include <QLineEdit>

#include <QMenu>

#include <QScrollBar>

#include <QStackedWidget>

#include <QToolButton>
#include <QVBoxLayout>

#include <QWidgetAction>
#include "ui/shared/file-dialogs.h"
#include "ui/features/output/output-panel-views.h"

namespace kai::ui {
namespace tk = kai::utils::tokens;
using namespace outputview;

void OutputPanel::seedOutput(const QString &text)
{
    m_outputView->setPlainText(text);
    m_formattedView->clearLog();
    m_formattedView->appendText(text);
    m_parser.resetFormat();
    detectJsonInText(text);
    updateTabVisibility();
    if (m_options.autoScroll) {
        m_outputView->verticalScrollBar()->setValue(m_outputView->verticalScrollBar()->maximum());
    }
}

QString OutputPanel::plainOutput() const
{
    return m_outputView->toPlainText();
}

void OutputPanel::exportOutputToFile()
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString baseName = m_titleLabel && !m_titleLabel->text().isEmpty()
        ? m_titleLabel->text() : QStringLiteral("saida");
    const QString suggested = QStringLiteral("%1_%2.log").arg(baseName, timestamp);
    const QString path = pickSaveFile(this,
        utils::tr(QStringLiteral("output.export.dialog_title")), suggested,
        utils::tr(QStringLiteral("output.export.filter")));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("output.export.dialog_title")),
            utils::tr(QStringLiteral("output.export.failed")));
        return;
    }
    file.write(plainOutput().toUtf8());
    if (!file.commit()) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("output.export.dialog_title")),
            utils::tr(QStringLiteral("output.export.failed")));
    }
}

// Estilo do overlay de busca (moldura + campo); reaplicado ao trocar
// "Cantos"/tema, pois o QSS é local.
void OutputPanel::applyOutputSearchOverlayStyle()
{
    if (m_outputSearchOverlay) {
        QColor bg(tk::surface2());
        bg.setAlphaF(0.92);
        bindTokenStyle(m_outputSearchOverlay, [=, this]() { return QStringLiteral(
            "QWidget#outputSearchOverlay { background-color: rgba(%1,%2,%3,%4);"
            " border: 1px solid %5; border-radius: %6px; }")
            .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
            .arg(tk::borderColor()).arg(tk::radiusMd()); });
    }
    if (m_outputSearchField) {
        // O raio não pode passar da metade da altura real do campo (com "Arredondado" o token radiusMd é 16 e o Qt
        // desenha o canto QUADRADO num campo de ~27px).
        const int radius = tk::radiusMdForHeight(m_outputSearchField->maximumHeight());
        bindTokenStyle(m_outputSearchField, [=, this]() { return QStringLiteral(
            "QLineEdit { min-height: 0px; padding: 1px %1px; border-radius: %2px; }")
            .arg(tk::space(2)).arg(radius); });
    }
}

void OutputPanel::setupOutputSearchOverlay()
{
    m_outputSearchOverlay = new QWidget(m_outputContainer);
    m_outputSearchOverlay->setObjectName(QStringLiteral("outputSearchOverlay"));
    m_outputSearchOverlay->setAttribute(Qt::WA_StyledBackground, true);
    auto *bar = new QHBoxLayout(m_outputSearchOverlay);
    bar->setContentsMargins(tk::space(1), tk::space(1) / 2, tk::space(1), tk::space(1) / 2);
    bar->setSpacing(tk::space(1));

    const QColor iconColor(tk::mutedFg());

    m_outputSearchToggle = new QToolButton(m_outputSearchOverlay);
    m_outputSearchToggle->setIcon(LucideIcons::icon(QStringLiteral("search"), iconColor, 16));
    m_outputSearchToggle->setToolTip(utils::tr(QStringLiteral("output.search.toggle")));
    m_outputSearchToggle->setAutoRaise(true);
    m_outputSearchToggle->setCheckable(true);
    connect(m_outputSearchToggle, &QToolButton::clicked, this, [this]() {
        setOutputSearchExpanded(!m_outputSearchExpanded);
    });
    bar->addWidget(m_outputSearchToggle);

    m_outputSearchField = new QLineEdit(m_outputSearchOverlay);
    m_outputSearchField->setPlaceholderText(utils::tr(QStringLiteral("output.search.placeholder")));
    m_outputSearchField->setClearButtonEnabled(true);
    m_outputSearchField->setFixedWidth(200);
    m_outputSearchField->setFixedHeight(tk::iconButtonSize());
    applyOutputSearchOverlayStyle();
    m_outputSearchField->hide();
    connect(m_outputSearchField, &QLineEdit::textChanged, this, [this](const QString &needle) {
        if (m_formattedOutputEnabled) {
            m_formattedView->setFilterText(needle);
            updateOutputMatchCounterLabel();
        } else {
            applyOutputSearchFilter(needle);
        }
    });
    connect(m_outputSearchField, &QLineEdit::returnPressed, this, [this]() {
        const bool previous = QApplication::keyboardModifiers() & Qt::ShiftModifier;
        if (m_formattedOutputEnabled) {
            if (previous) {
                m_formattedView->goToPreviousMatch();
            } else {
                m_formattedView->goToNextMatch();
            }
            updateOutputMatchCounterLabel();
        } else if (previous) {
            goToPreviousOutputMatch();
        } else {
            goToNextOutputMatch();
        }
    });
    bar->addWidget(m_outputSearchField);

    m_outputFieldFilterButton = new QToolButton(m_outputSearchOverlay);
    m_outputFieldFilterButton->setIcon(LucideIcons::icon(QStringLiteral("funnel"), iconColor, 16));
    m_outputFieldFilterButton->setToolTip(utils::tr(QStringLiteral("output.search.field_filter")));
    m_outputFieldFilterButton->setAutoRaise(true);
    m_outputFieldFilterButton->hide();
    connect(m_outputFieldFilterButton, &QToolButton::clicked, this, &OutputPanel::showFieldFilterMenu);
    bar->addWidget(m_outputFieldFilterButton);

    m_outputMatchCounterLabel = new QLabel(m_outputSearchOverlay);
    m_outputMatchCounterLabel->setProperty("kaiRole", QStringLiteral("caption"));
    m_outputMatchCounterLabel->setMinimumWidth(QFontMetrics(m_outputMatchCounterLabel->font())
        .horizontalAdvance(QStringLiteral("99/99")));
    m_outputMatchCounterLabel->setAlignment(Qt::AlignCenter);
    m_outputMatchCounterLabel->hide();
    bar->addWidget(m_outputMatchCounterLabel);

    m_outputPrevMatchButton = new QToolButton(m_outputSearchOverlay);
    m_outputPrevMatchButton->setIcon(LucideIcons::icon(QStringLiteral("chevron-up"), iconColor, 16));
    m_outputPrevMatchButton->setToolTip(utils::tr(QStringLiteral("json_viewer.search.previous")));
    m_outputPrevMatchButton->setAutoRaise(true);
    m_outputPrevMatchButton->hide();
    connect(m_outputPrevMatchButton, &QToolButton::clicked, this, [this]() {
        if (m_formattedOutputEnabled) {
            m_formattedView->goToPreviousMatch();
            updateOutputMatchCounterLabel();
        } else {
            goToPreviousOutputMatch();
        }
    });
    bar->addWidget(m_outputPrevMatchButton);

    m_outputNextMatchButton = new QToolButton(m_outputSearchOverlay);
    m_outputNextMatchButton->setIcon(LucideIcons::icon(QStringLiteral("chevron-down"), iconColor, 16));
    m_outputNextMatchButton->setToolTip(utils::tr(QStringLiteral("json_viewer.search.next")));
    m_outputNextMatchButton->setAutoRaise(true);
    m_outputNextMatchButton->hide();
    connect(m_outputNextMatchButton, &QToolButton::clicked, this, [this]() {
        if (m_formattedOutputEnabled) {
            m_formattedView->goToNextMatch();
            updateOutputMatchCounterLabel();
        } else {
            goToNextOutputMatch();
        }
    });
    bar->addWidget(m_outputNextMatchButton);

    m_outputSearchOverlay->adjustSize();
    m_outputContainer->installEventFilter(this);
    setOutputSearchExpanded(false);
    repositionOutputSearchOverlay();
}

void OutputPanel::setOutputSearchExpanded(bool expanded)
{
    m_outputSearchExpanded = expanded;
    if (m_outputSearchField) {
        m_outputSearchField->setVisible(expanded);
        if (expanded) {
            m_outputSearchField->setFocus();
        } else {
            m_outputSearchField->clear();
        }
    }
    if (!expanded) {
        if (m_outputMatchCounterLabel) m_outputMatchCounterLabel->hide();
        if (m_outputPrevMatchButton) m_outputPrevMatchButton->hide();
        if (m_outputNextMatchButton) m_outputNextMatchButton->hide();
        if (m_outputFieldFilterButton) m_outputFieldFilterButton->hide();
    } else if (m_outputFieldFilterButton) {
        m_outputFieldFilterButton->setVisible(m_formattedOutputEnabled);
    }
    if (m_outputSearchToggle) {
        m_outputSearchToggle->setChecked(expanded);
        m_outputSearchToggle->setIcon(LucideIcons::icon(
            expanded ? QStringLiteral("x") : QStringLiteral("search"),
            QColor(tk::mutedFg()), 16));
        m_outputSearchToggle->setToolTip(utils::tr(expanded
            ? QStringLiteral("json_viewer.search.close")
            : QStringLiteral("output.search.toggle")));
    }
    repositionOutputSearchOverlay();
}

void OutputPanel::repositionOutputSearchOverlay()
{
    if (!m_outputSearchOverlay || !m_outputContainer) {
        return;
    }
    if (m_outputSearchOverlay->layout()) {
        m_outputSearchOverlay->layout()->activate();
    }
    m_outputSearchOverlay->adjustSize();
    const int margin = tk::space(2);
    int scrollbarWidth = 0;
    if (m_formattedOutputEnabled) {
        if (QScrollBar *sb = m_formattedView->verticalScrollBar(); sb && sb->isVisible()) {
            scrollbarWidth = sb->width();
        }
    } else if (QScrollBar *sb = m_outputView->verticalScrollBar(); sb && sb->isVisible()) {
        scrollbarWidth = sb->width();
    }
    const int rightMargin = margin + scrollbarWidth;
    const int x = m_outputContainer->width() - m_outputSearchOverlay->width() - rightMargin;
    m_outputSearchOverlay->move(qMax(margin, x), margin);
    m_outputSearchOverlay->raise();
}

void OutputPanel::applyOutputSearchFilter(const QString &needle)
{
    const QString trimmed = needle.trimmed();
    m_outputMatches.clear();
    m_outputCurrentMatchIndex = -1;
    QTextDocument *doc = m_outputView->document();
    if (!trimmed.isEmpty()) {
        QTextCursor cursor(doc);
        while (true) {
            cursor = doc->find(trimmed, cursor);
            if (cursor.isNull()) {
                break;
            }
            m_outputMatches.append(cursor);
        }
        if (!m_outputMatches.isEmpty()) {
            m_outputCurrentMatchIndex = 0;
        }
    }
    goToOutputMatch(m_outputCurrentMatchIndex);
}

void OutputPanel::goToOutputMatch(int index)
{
    // Contraste automático (como utils::tokens::buttonFg()) em vez de
    // Qt::black/white fixos, que perdiam legibilidade em alguns temas.
    auto contrastFor = [](const QColor &background) {
        return background.lightnessF() > 0.6 ? QColor(0x10, 0x10, 0x14) : QColor(Qt::white);
    };
    const QColor highlightBg = QColor(tk::warningFg()).lighter(160);
    const QColor currentBg = QColor(tk::accent());

    QList<QTextEdit::ExtraSelection> selections;
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(highlightBg);
    highlightFormat.setForeground(contrastFor(highlightBg));
    QTextCharFormat currentFormat;
    currentFormat.setBackground(currentBg);
    currentFormat.setForeground(contrastFor(currentBg));

    for (int i = 0; i < m_outputMatches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_outputMatches.at(i);
        sel.format = (i == index) ? currentFormat : highlightFormat;
        selections.append(sel);
    }
    m_outputView->setExtraSelections(selections);
    if (index >= 0 && index < m_outputMatches.size()) {
        m_outputView->setTextCursor(m_outputMatches.at(index));
        m_outputView->centerCursor();
    }
    updateOutputMatchCounterLabel();
}

void OutputPanel::updateOutputMatchCounterLabel()
{
    if (!m_outputMatchCounterLabel || !m_outputPrevMatchButton || !m_outputNextMatchButton) {
        return;
    }
    const bool hasTerm = m_outputSearchField && !m_outputSearchField->text().trimmed().isEmpty();
    m_outputMatchCounterLabel->setVisible(m_outputSearchExpanded && hasTerm);
    m_outputPrevMatchButton->setVisible(m_outputSearchExpanded && hasTerm);
    m_outputNextMatchButton->setVisible(m_outputSearchExpanded && hasTerm);
    if (!hasTerm) {
        return;
    }
    if (m_formattedOutputEnabled) {
        const int total = m_formattedView->logModel()->matchCount();
        const int ordinal = m_formattedView->logModel()->currentMatchOrdinal();
        m_outputMatchCounterLabel->setText(total == 0
            ? utils::tr(QStringLiteral("json_viewer.search.no_matches"))
            : QStringLiteral("%1/%2").arg(ordinal).arg(total));
    } else {
        m_outputMatchCounterLabel->setText(m_outputMatches.isEmpty()
            ? utils::tr(QStringLiteral("json_viewer.search.no_matches"))
            : QStringLiteral("%1/%2").arg(m_outputCurrentMatchIndex + 1).arg(m_outputMatches.size()));
    }
    repositionOutputSearchOverlay();
}

void OutputPanel::goToNextOutputMatch()
{
    if (m_outputMatches.isEmpty()) {
        return;
    }
    m_outputCurrentMatchIndex = (m_outputCurrentMatchIndex + 1) % m_outputMatches.size();
    goToOutputMatch(m_outputCurrentMatchIndex);
}

void OutputPanel::goToPreviousOutputMatch()
{
    if (m_outputMatches.isEmpty()) {
        return;
    }
    m_outputCurrentMatchIndex = (m_outputCurrentMatchIndex - 1 + m_outputMatches.size()) % m_outputMatches.size();
    goToOutputMatch(m_outputCurrentMatchIndex);
}

void OutputPanel::insertFieldFilterToken(const QString &field, const QString &value)
{
    QString text = m_outputSearchField->text();
    if (!text.isEmpty() && !text.endsWith(QLatin1Char(' '))) {
        text += QLatin1Char(' ');
    }
    text += value.isEmpty() ? QStringLiteral("%1:").arg(field) : QStringLiteral("%1:%2").arg(field, value);
    m_outputSearchField->setText(text);
    m_outputSearchField->setFocus();
    m_outputSearchField->end(false);
}

void OutputPanel::showFieldFilterMenu()
{
    if (!m_formattedOutputEnabled) {
        return;
    }
    LogLineModel *model = m_formattedView->logModel();
    const QStringList fields = model->knownFieldNames();
    const QStringList levels = model->knownLevelValues();
    if (fields.isEmpty() && levels.isEmpty()) {
        return;
    }

    QMenu menu(this);
    if (!levels.isEmpty()) {
        QMenu *levelMenu = menu.addMenu(utils::tr(QStringLiteral("output.search.field_filter.level")));
        const QSet<QString> currentFilter = model->levelFilter();
        const QSet<QString> allLevelsSet(levels.begin(), levels.end());
        for (const QString &lvl : levels) {
            auto *checkbox = new QCheckBox(lvl, levelMenu);
            checkbox->setChecked(currentFilter.isEmpty() || currentFilter.contains(lvl));
            connect(checkbox, &QCheckBox::toggled, this, [model, lvl, allLevelsSet](bool checked) {
                QSet<QString> filter = model->levelFilter();
                if (filter.isEmpty()) {
                    filter = allLevelsSet;
                }
                if (checked) {
                    filter.insert(lvl);
                } else {
                    filter.remove(lvl);
                }
                if (filter.size() == allLevelsSet.size()) {
                    filter.clear();
                }
                model->setLevelFilter(filter);
            });
            auto *widgetAction = new QWidgetAction(levelMenu);
            widgetAction->setDefaultWidget(checkbox);
            levelMenu->addAction(widgetAction);
        }
        if (!currentFilter.isEmpty()) {
            levelMenu->addSeparator();
            QAction *clearAction = levelMenu->addAction(utils::tr(QStringLiteral("output.search.field_filter.level_clear")));
            connect(clearAction, &QAction::triggered, this, [model]() {
                model->setLevelFilter(QSet<QString>());
            });
        }
        menu.addSeparator();
    }
    for (const QString &field : fields) {
        if (field == QLatin1String("level")) {
            continue;
        }
        QAction *action = menu.addAction(field);
        connect(action, &QAction::triggered, this, [this, field]() {
            insertFieldFilterToken(field);
        });
    }
    menu.exec(m_outputFieldFilterButton->mapToGlobal(
        QPoint(0, m_outputFieldFilterButton->height())));
}

bool OutputPanel::focusSearch()
{
    if (m_interactiveMode || m_skipped) {
        return false;
    }
    QWidget *current = m_pages ? m_pages->currentWidget() : nullptr;
    if (current == m_jsonView && m_jsonView) {
        m_jsonView->focusSearch();
        return true;
    }
    if (current == m_outputContainer && m_outputContainer) {
        setOutputSearchExpanded(true);
        return true;
    }
    return false;
}

} // namespace kai::ui
