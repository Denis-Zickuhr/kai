#pragma once

#include "ui/features/docs/doc-text-tools.h"

#include <QList>
#include <QPlainTextEdit>
#include <QPoint>
#include <QTextCursor>
#include <QTextEdit>
#include <QVector>

#include <functional>
#include <memory>

class QTimer;

namespace kai::ui {

class DocHighlighter;
class DocLineNumberArea;

// O editor de texto do leitor de documentos: realce de sintaxe (JSON, YAML, XML, Markdown), números de linha, recuo
// automático, Tab que indenta e a validação ao vivo (JSON, YAML e XML) com a linha do erro marcada. As ferramentas de
// formatação entram por `transform`, que age na seleção (ou no texto todo) num único passo de "desfazer".
class DocCodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit DocCodeEditor(QWidget *parent = nullptr);
    ~DocCodeEditor() override;

    void setLanguage(texttools::Language language);
    texttools::Language language() const { return m_language; }
    // O resultado da última validação (sempre "ok" para texto simples e Markdown).
    texttools::Issue issue() const { return m_issue; }
    // Valida já, sem esperar o intervalo (os testes e o salvar usam).
    void validateNow();

    // Aplica `fn` à seleção (ou ao texto todo, sem seleção). Recusado (!ok) deixa o texto como estava.
    texttools::Result transform(const std::function<texttools::Result(const QString &)> &fn);
    void goToLine(int line, int column = 1);

    // Realces da busca do leitor, somados aos da linha atual e do erro.
    void setSearchSelections(const QList<QTextEdit::ExtraSelection> &selections);

    // Tema e zoom (100 = tamanho normal).
    void refreshStyle();
    void setZoomPercent(int percent);

    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent *event);

    // --- Atalhos no estilo do VS Code ---------------------------------------------------------------------------------
    // Multi-cursor (Ctrl+D, Ctrl+Shift+L, Ctrl+Alt+↑/↓, Alt+clique, Shift+Alt+arrastar), mover/copiar/apagar linhas,
    // comentar, pares automáticos e mais: ver docs/organizing.md. Os cursores extras ficam aqui; o principal é o do Qt.
    int cursorCount() const { return int(m_extra.size()) + 1; }
    // Todos os cursores, o principal primeiro.
    QVector<QTextCursor> allCursors() const;
    void clearExtraCursors();
    // O que o editor trata como atalho dele (para o app não roubar a tecla).
    static bool isEditorShortcut(const QKeyEvent *event, bool hasExtraCursors);

signals:
    void issueChanged(const kai::ui::texttools::Issue &issue);
    // Shift+Alt+F: formatar o documento (quem sabe como é o leitor).
    void formatRequested();
    // Ctrl+H: abrir a busca com substituição.
    void replaceRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateGutter();
    void updateGutterRect(const QRect &rect, int dy);
    void rebuildExtraSelections();
    void indentSelection(bool outdent);
    void newLineAt(QTextCursor &cursor);

    // atalhos (doc-code-editor-keys.cpp)
    bool handleShortcut(QKeyEvent *event);
    bool handleMultiKey(QKeyEvent *event);
    bool handleAutoClose(QKeyEvent *event);
    void setAllCursors(const QVector<QTextCursor> &cursors);
    void normalizeCursors();
    QVector<QPair<int, int>> lineRanges() const;
    void moveLines(bool up);
    void copyLines(bool up);
    void deleteLines();
    void insertLineBelow(bool below);
    void toggleComment();
    void selectLines();
    void addNextOccurrence();
    void selectAllOccurrences();
    void addCursorVertically(int direction);
    void jumpToMatchingBracket();
    void smartHome(bool select);
    void copyOrCutLine(bool cut);
    void goToLinePrompt();
    int matchingBracket(int position) const;
    void beginBoxSelection(const QPoint &point);
    void updateBoxSelection(const QPoint &point);

    DocLineNumberArea *m_gutter = nullptr;
    DocHighlighter *m_highlighter = nullptr;
    QTimer *m_validation = nullptr;
    texttools::Language m_language = texttools::Language::Text;
    texttools::Issue m_issue;
    QList<QTextEdit::ExtraSelection> m_searchSelections;
    int m_zoom = 100;
    QVector<QTextCursor> m_extra; // cursores além do principal (multi-cursor)
    QString m_occurrenceWord; // a palavra que o 1º Ctrl+D selecionou: as próximas ocorrências são só palavras inteiras
    bool m_boxActive = false;
    QTextCursor m_boxAnchor;
};

} // namespace kai::ui
