// O autocompletar do DocCodeEditor para Markdown: notas (`[[`) e os links do Kai (`kai:run/`, `kai:open/`, `kai:env/`,
// `kai:note/`). Quem conhece os nomes é o app, por um provider; aqui mora só o gatilho, a lista e a inserção.

#include "ui/features/docs/doc-code-editor.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>

#include <algorithm>

namespace kai::ui {

namespace {

const QStringList &kaiActions()
{
    static const QStringList actions = {QStringLiteral("run/"), QStringLiteral("open/"), QStringLiteral("env/"),
                                        QStringLiteral("note/")};
    return actions;
}

} // namespace

DocCodeEditor::CompletionContext DocCodeEditor::completionContextAtCursor() const
{
    CompletionContext context;
    if (isReadOnly() || m_language != texttools::Language::Markdown || !m_completionProvider) {
        return context;
    }
    const QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        return context;
    }
    const QString before = cursor.block().text().left(cursor.positionInBlock());
    const int blockStart = cursor.block().position();

    static const QRegularExpression note(QStringLiteral(R"(\[\[([^\]\|\n]*)$)"));
    static const QRegularExpression target(QStringLiteral(R"(kai:(run|open|env|note)/([^\n)>\]]*)$)"));
    static const QRegularExpression action(QStringLiteral(R"((?:^|[\s(<\[])kai:([a-z]*)$)"));

    QRegularExpressionMatch match = note.match(before);
    if (match.hasMatch()) {
        context.kind = QStringLiteral("notes");
        context.prefix = match.captured(1);
        context.start = blockStart + int(match.capturedStart(1));
        context.suffix = QStringLiteral("]]");
        return context;
    }
    match = target.match(before);
    if (match.hasMatch()) {
        const QString verb = match.captured(1);
        context.kind = verb == QLatin1String("run") ? QStringLiteral("commands")
            : verb == QLatin1String("open") ? QStringLiteral("commands+folders")
            : verb == QLatin1String("env") ? QStringLiteral("environments") : QStringLiteral("notes");
        context.prefix = match.captured(2);
        context.start = blockStart + int(match.capturedStart(2));
        return context;
    }
    match = action.match(before);
    if (match.hasMatch()) {
        context.kind = QStringLiteral("actions");
        context.prefix = match.captured(1);
        context.start = blockStart + int(match.capturedStart(1));
    }
    return context;
}

QStringList DocCodeEditor::completionCandidates() const
{
    const CompletionContext context = completionContextAtCursor();
    if (context.kind.isEmpty()) {
        return {};
    }
    QStringList all;
    if (context.kind == QLatin1String("actions")) {
        all = kaiActions();
    } else {
        for (const QString &kind : context.kind.split(QLatin1Char('+'))) {
            all += m_completionProvider(kind);
        }
    }
    all.removeDuplicates();
    QStringList matching;
    for (const QString &candidate : std::as_const(all)) {
        if (candidate.contains(context.prefix, Qt::CaseInsensitive)) {
            matching << candidate;
        }
    }
    std::sort(matching.begin(), matching.end(), [&context](const QString &a, const QString &b) {
        // Quem começa pelo que foi digitado vem antes; depois, ordem alfabética.
        const bool aStarts = a.startsWith(context.prefix, Qt::CaseInsensitive);
        const bool bStarts = b.startsWith(context.prefix, Qt::CaseInsensitive);
        return aStarts != bStarts ? aStarts : a.localeAwareCompare(b) < 0;
    });
    return matching;
}

bool DocCodeEditor::completionVisible() const
{
    return m_completer && m_completer->popup() && m_completer->popup()->isVisible();
}

void DocCodeEditor::updateCompletion()
{
    const CompletionContext context = completionContextAtCursor();
    const QStringList candidates = context.kind.isEmpty() ? QStringList() : completionCandidates();
    // Já digitou o nome inteiro: não há mais o que escolher.
    const bool exact = candidates.size() == 1 && candidates.first().compare(context.prefix, Qt::CaseInsensitive) == 0;
    if (candidates.isEmpty() || exact) {
        if (m_completer) {
            m_completer->popup()->hide();
        }
        return;
    }
    if (!m_completer) {
        m_completionModel = new QStringListModel(this);
        m_completer = new QCompleter(m_completionModel, this);
        m_completer->setWidget(this);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setFilterMode(Qt::MatchContains);
        m_completer->setMaxVisibleItems(10);
        connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated), this, &DocCodeEditor::insertCompletion);
    }
    m_completionContext = context;
    // A lista já vem filtrada e ordenada: o completer só mostra, sem refiltrar.
    m_completer->setCompletionPrefix(QString());
    m_completionModel->setStringList(candidates);
    m_completer->setCompletionPrefix(QString());
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    QRect area = cursorRect();
    area.setWidth(m_completer->popup()->sizeHintForColumn(0) + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 8);
    m_completer->complete(area);
}

void DocCodeEditor::insertCompletion(const QString &text)
{
    const CompletionContext context = m_completionContext;
    if (context.start < 0 || text.isEmpty()) {
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.setPosition(context.start);
    cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    QString inserted = text;
    if (!context.suffix.isEmpty()) {
        const QTextBlock block = textCursor().block();
        if (!block.text().mid(textCursor().positionInBlock(), context.suffix.size()).startsWith(context.suffix)) {
            inserted += context.suffix; // `[[Título]]`: fecha o link
        }
    }
    cursor.insertText(inserted);
    setTextCursor(cursor);
    if (m_completer) {
        m_completer->popup()->hide();
    }
    if (context.kind == QLatin1String("actions")) {
        updateCompletion(); // "kai:run/" já abre a lista dos comandos
    }
}

} // namespace kai::ui
