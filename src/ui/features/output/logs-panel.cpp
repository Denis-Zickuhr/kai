#include "ui/features/output/logs-panel.h"

#include "ui/features/http/http-ui.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCursor>
#include <QThreadPool>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr qint64 kMaxTail = 512 * 1024;

QString levelLabel(utils::LogLevel level)
{
    switch (level) {
    case utils::LogLevel::Debug: return QStringLiteral("DEBUG");
    case utils::LogLevel::Info: return QStringLiteral("INFO");
    case utils::LogLevel::Warning: return QStringLiteral("WARN");
    case utils::LogLevel::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString oneLine(QString text)
{
    text.replace(QLatin1Char('\n'), QStringLiteral(" \u21b5 "));
    text.remove(QLatin1Char('\r'));
    return text;
}

QToolButton *makeToolButton(QWidget *parent, const QString &icon, const QString &tooltip, bool checkable = false)
{
    auto *button = new QToolButton(parent);
    button->setIcon(LucideIcons::icon(icon, QColor(tk::mutedFg()), 16));
    button->setToolTip(tooltip);
    button->setCheckable(checkable);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: %1px; padding: 4px; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton:checked { background: %3; }")
        .arg(tk::radiusSm())
        .arg(tk::hoverBg(), tk::selBg()));
    return button;
}

} // namespace

std::optional<utils::LogEntry> LogsPanel::parseFileLine(const QString &line)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)\s+\[(DEBUG|INFO|WARN|ERROR)\]\s+\[([^\]]*)\]\s?(.*)$)"));
    const QRegularExpressionMatch match = pattern.match(line);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    utils::LogEntry entry;
    entry.timestamp = QDateTime::fromString(match.captured(1), Qt::ISODate);
    const QString level = match.captured(2);
    entry.level = level == QLatin1String("DEBUG") ? utils::LogLevel::Debug
        : level == QLatin1String("WARN")          ? utils::LogLevel::Warning
        : level == QLatin1String("ERROR")         ? utils::LogLevel::Error
                                                  : utils::LogLevel::Info;
    entry.tag = match.captured(3);
    entry.message = match.captured(4);
    return entry;
}

QString LogsPanel::formatEntry(const utils::LogEntry &entry)
{
    return QStringLiteral("%1 %2 [%3] %4")
        .arg(entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")), levelLabel(entry.level), entry.tag, oneLine(entry.message));
}

LogsPanel::LogsPanel(QWidget *parent)
    : QWidget(parent)
{
    makeTransparentHost(this, QStringLiteral("logsPanel"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(6);

    m_levelFilter = new QComboBox(this);
    m_levelFilter->setObjectName(QStringLiteral("logsLevel"));
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.debug")), int(utils::LogLevel::Debug));
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.info")), int(utils::LogLevel::Info));
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.warning")), int(utils::LogLevel::Warning));
    m_levelFilter->addItem(utils::tr(QStringLiteral("logs.level.error")), int(utils::LogLevel::Error));
    m_levelFilter->setToolTip(utils::tr(QStringLiteral("logs.min_level")));
    connect(m_levelFilter, &QComboBox::currentIndexChanged, this,
            [this](int index) { applyMinLevel(utils::LogLevel(m_levelFilter->itemData(index).toInt())); });
    bar->addWidget(m_levelFilter);

    m_searchField = new QLineEdit(this);
    m_searchField->setObjectName(QStringLiteral("logsSearch"));
    m_searchField->setPlaceholderText(utils::tr(QStringLiteral("logs.search")));
    m_searchField->setClearButtonEnabled(true);
    connect(m_searchField, &QLineEdit::textChanged, this, &LogsPanel::setSearch);
    bar->addWidget(m_searchField, 1);

    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("logsCount"));
    m_countLabel->setStyleSheet(QStringLiteral("QLabel#logsCount { background: transparent; color: %1; }").arg(tk::mutedFg()));
    bar->addWidget(m_countLabel);

    m_pauseButton = makeToolButton(this, QStringLiteral("pause"), utils::tr(QStringLiteral("logs.pause")), true);
    m_pauseButton->setObjectName(QStringLiteral("logsPause"));
    connect(m_pauseButton, &QToolButton::toggled, this, &LogsPanel::setPaused);
    bar->addWidget(m_pauseButton);

    auto *copy = makeToolButton(this, QStringLiteral("copy"), utils::tr(QStringLiteral("logs.copy")));
    copy->setObjectName(QStringLiteral("logsCopy"));
    connect(copy, &QToolButton::clicked, this, [this]() { QApplication::clipboard()->setText(visibleText()); });
    bar->addWidget(copy);

    auto *clear = makeToolButton(this, QStringLiteral("trash-2"), utils::tr(QStringLiteral("logs.clear")));
    clear->setObjectName(QStringLiteral("logsClear"));
    connect(clear, &QToolButton::clicked, this, &LogsPanel::clear);
    bar->addWidget(clear);
    root->addLayout(bar);

    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("logsView"));
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setMaximumBlockCount(kMaxEntries);
    m_view->setFont(tk::monoFont(tk::fontSizeSmallPt()));
    m_view->setStyleSheet(QStringLiteral("QPlainTextEdit { %1 }").arg(tk::codeAreaQss()));
    root->addWidget(m_view, 1);

    refreshCount();

    connect(&utils::LoggerSink::instance(), &utils::LoggerSink::logEmitted, this, &LogsPanel::appendEntry);
    loadHistory();
}

void LogsPanel::loadHistory()
{
    const QString path = utils::Logger::logFilePath();
    if (path.isEmpty()) {
        addHistory({utils::LogEntry{QDateTime::currentDateTime(), utils::LogLevel::Info, QStringLiteral("Logs"),
                                    utils::tr(QStringLiteral("logs.file_unavailable"))}});
        return;
    }
    // A cauda do arquivo, lida e interpretada fora da GUI: um log de várias sessões passa de megabytes.
    const QPointer<LogsPanel> self(this);
    QThreadPool::globalInstance()->start([self, path]() {
        QList<utils::LogEntry> history;
        bool unreadable = false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            unreadable = true;
        } else {
            if (file.size() > kMaxTail) {
                file.seek(file.size() - kMaxTail);
                file.readLine(); // a primeira linha provavelmente veio cortada
            }
            const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (line.trimmed().isEmpty()) {
                    continue;
                }
                if (const auto entry = parseFileLine(line)) {
                    history.append(*entry);
                } else if (!history.isEmpty()) {
                    history.last().message += QLatin1Char('\n') + line; // continuação de uma mensagem de várias linhas
                }
            }
        }
        {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, history, unreadable, path]() {
                if (!self) {
                    return;
                }
                if (unreadable) {
                    self->addHistory({utils::LogEntry{QDateTime::currentDateTime(), utils::LogLevel::Warning, QStringLiteral("Logs"),
                                                      QStringLiteral("%1 %2").arg(utils::tr(QStringLiteral("logs.file_unreadable")), path)}});
                } else {
                    self->addHistory(history);
                }
            }, Qt::QueuedConnection);
        }
    });
}

void LogsPanel::addHistory(const QList<utils::LogEntry> &history)
{
    // O que chegou ao vivo enquanto o arquivo era lido também está no arquivo: fica só a cópia ao vivo.
    const qint64 firstLive = m_entries.isEmpty() ? std::numeric_limits<qint64>::max() : m_entries.first().timestamp.toSecsSinceEpoch();
    QList<utils::LogEntry> older;
    for (const utils::LogEntry &entry : history) {
        if (entry.timestamp.toSecsSinceEpoch() < firstLive) {
            older.append(entry);
        }
    }
    m_entries = older + m_entries;
    if (m_entries.size() > kMaxEntries) {
        m_entries.remove(0, m_entries.size() - kMaxEntries);
    }
    if (!m_paused) {
        rebuildView();
    } else {
        m_held += int(older.size());
        refreshCount();
    }
}

bool LogsPanel::accepts(const utils::LogEntry &entry) const
{
    if (int(entry.level) < int(m_minLevel)) {
        return false;
    }
    return m_search.isEmpty() || entry.message.contains(m_search, Qt::CaseInsensitive)
        || entry.tag.contains(m_search, Qt::CaseInsensitive);
}

void LogsPanel::appendEntry(const utils::LogEntry &entry)
{
    m_entries.append(entry);
    if (m_entries.size() > kMaxEntries) {
        m_entries.removeFirst();
    }
    if (m_paused) {
        ++m_held;
        refreshCount();
        return;
    }
    if (accepts(entry)) {
        appendToView(entry);
    }
}

void LogsPanel::appendToView(const utils::LogEntry &entry)
{
    QScrollBar *bar = m_view->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;

    QTextCursor cursor(m_view->document());
    cursor.movePosition(QTextCursor::End);
    if (m_visible > 0) {
        cursor.insertBlock();
    }
    QTextCharFormat muted;
    muted.setForeground(QColor(tk::mutedFg()));
    QTextCharFormat level;
    QTextCharFormat message;
    message.setForeground(QColor(tk::codeFg()));
    switch (entry.level) {
    case utils::LogLevel::Debug:
        level.setForeground(QColor(tk::mutedFg()));
        message.setForeground(QColor(tk::mutedFg()));
        break;
    case utils::LogLevel::Info:
        level.setForeground(QColor(tk::infoFg()));
        break;
    case utils::LogLevel::Warning:
        level.setForeground(QColor(tk::warningFg()));
        break;
    case utils::LogLevel::Error:
        level.setForeground(QColor(tk::errorFg()));
        message.setForeground(QColor(tk::errorFg()));
        break;
    }
    level.setFontWeight(QFont::Bold);
    cursor.insertText(entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")) + QLatin1Char(' '), muted);
    cursor.insertText(levelLabel(entry.level).leftJustified(5) + QLatin1Char(' '), level);
    cursor.insertText(entry.tag + QLatin1Char(' '), muted);
    cursor.insertText(oneLine(entry.message), message);

    m_visible = qMin(m_visible + 1, kMaxEntries);
    if (atEnd) {
        bar->setValue(bar->maximum());
    }
    refreshCount();
}

void LogsPanel::rebuildView()
{
    m_held = 0;
    m_visible = 0;
    m_view->setUpdatesEnabled(false);
    m_view->clear();
    for (const utils::LogEntry &entry : std::as_const(m_entries)) {
        if (accepts(entry)) {
            appendToView(entry);
        }
    }
    m_view->setUpdatesEnabled(true);
    m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    refreshCount();
}

void LogsPanel::refreshCount()
{
    QString text = QStringLiteral("%1 / %2").arg(m_visible).arg(m_entries.size());
    if (m_paused && m_held > 0) {
        text += QStringLiteral("  ") + utils::tr(QStringLiteral("logs.held")).arg(m_held);
    }
    m_countLabel->setText(text);
}

void LogsPanel::setMinLevel(utils::LogLevel level)
{
    const int index = m_levelFilter->findData(int(level));
    if (index >= 0) {
        m_levelFilter->setCurrentIndex(index); // o sinal do combo aplica
    }
}

void LogsPanel::applyMinLevel(utils::LogLevel level)
{
    if (level == m_minLevel) {
        return;
    }
    m_minLevel = level;
    if (!m_paused) {
        rebuildView();
    }
}

void LogsPanel::setSearch(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_search) {
        return;
    }
    m_search = trimmed;
    if (m_searchField->text() != text) {
        m_searchField->setText(text);
    }
    if (!m_paused) {
        rebuildView();
    }
}

void LogsPanel::setPaused(bool paused)
{
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    if (m_pauseButton->isChecked() != paused) {
        m_pauseButton->setChecked(paused);
    }
    m_pauseButton->setIcon(LucideIcons::icon(paused ? QStringLiteral("play") : QStringLiteral("pause"),
                                             QColor(paused ? tk::accent() : tk::mutedFg()), 16));
    m_pauseButton->setToolTip(utils::tr(paused ? QStringLiteral("logs.resume") : QStringLiteral("logs.pause")));
    if (!paused) {
        rebuildView(); // o que chegou durante a pausa entra de uma vez
    } else {
        m_held = 0;
        refreshCount();
    }
}

void LogsPanel::clear()
{
    m_entries.clear();
    m_held = 0;
    m_visible = 0;
    m_view->clear();
    refreshCount();
}

QString LogsPanel::visibleText() const
{
    QStringList lines;
    for (const utils::LogEntry &entry : m_entries) {
        if (accepts(entry)) {
            lines.append(formatEntry(entry));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace kai::ui
