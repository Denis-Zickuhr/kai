#pragma once

#include "ui/features/docs/doc-folding.h"
#include "ui/features/docs/doc-text-tools.h"

#include <QList>
#include <QStringList>
#include <QPlainTextEdit>
#include <QPoint>
#include <QTextCursor>
#include <QTextEdit>
#include <QVector>

#include <functional>
#include <memory>

class QCompleter;
class QStringListModel;
class QTextBlock;
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

    // --- Autocompletar do Markdown ------------------------------------------------------------------------------------
    // Ao digitar `[[` (notas) ou `kai:` / `kai:run/`, `kai:open/`, `kai:env/`, `kai:note/` (comandos, pastas, ambientes,
    // notas), abre uma lista para escolher. Quem sabe os nomes é o `provider` (o app): recebe "notes", "commands", "folders"
    // ou "environments". Só vale com a linguagem Markdown e o editor editável.
    using CompletionProvider = std::function<QStringList(const QString &kind)>;
    void setCompletionProvider(CompletionProvider provider) { m_completionProvider = std::move(provider); }
    // O que a lista oferece agora para o texto antes do cursor (vazio = nada); para os testes.
    QStringList completionCandidates() const;
    bool completionVisible() const;
    // Aceita o item `text` da lista no lugar do que foi digitado.
    void insertCompletion(const QString &text);

    // --- Dobras (JSON, YAML e XML) -------------------------------------------------------------------------------------
    // Cada região (ver folding::regions) pode ser dobrada: as linhas somem, o cabeçalho ganha um "⋯" e o fim da região.
    // Funciona também com o editor somente leitura (o leitor de documentos usa assim). Linhas começam em 0. A marca de
    // "dobrado" mora no bloco do cabeçalho, então viaja com a linha quando o texto acima muda.
    bool foldingAvailable() const { return folding::supports(m_language); }
    int foldRegionCount() const { return int(m_folds.size()); }
    bool isFoldStart(int line) const { return regionIndexAt(line) >= 0; }
    bool isFolded(int line) const;
    int hiddenLineCount() const;
    void toggleFold(int line);
    // Dobra tudo; o objeto raiz que ocupa o documento inteiro fica aberto (senão sobraria uma linha só).
    void foldAll();
    void unfoldAll();
    // Dobra a região aberta mais interna sob o cursor / abre a do cabeçalho do cursor (Ctrl+Shift+[ e ]).
    void foldAtCursor();
    void unfoldAtCursor();
    // Recalcula as regiões já, sem esperar a pausa na digitação (o texto trocado e os testes usam).
    void refreshFolds();
    // O clique na calha: no marcador de dobra de uma linha.
    void handleGutterClick(const QPoint &gutterPos);
    // A linha dobrada cujo "⋯" está sob o ponto da viewport (-1 = nenhuma).
    int foldHintLineAt(const QPoint &viewportPos) const;
    static bool isFoldShortcut(const QKeyEvent *event);

signals:
    void issueChanged(const kai::ui::texttools::Issue &issue);
    // As regiões ou o que está dobrado mudaram (a barra do leitor habilita/desabilita os botões).
    void foldsChanged();
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
    int foldColumnWidth() const;
    int foldMarkerSize() const;
    QWidget *gutterWidget() const;

    // autocompletar (doc-code-editor-completion.cpp)
    struct CompletionContext {
        QString kind;     // "notes", "commands+folders", "commands", "environments" ou "actions"
        int start = -1;   // posição no documento de onde começa o texto digitado
        QString prefix;
        QString suffix;   // o que fecha a escolha ("]]" nas notas entre colchetes duplos)
    };
    CompletionContext completionContextAtCursor() const;
    void updateCompletion();

    // dobras (doc-code-editor-folds.cpp)
    int regionIndexAt(int line) const;
    void setFolded(int regionIndex, bool folded);
    void applyFolds();
    void revealLine(int line);
    void onCursorMoved();
    bool handleFoldKey(QKeyEvent *event);
    void handleKeyPress(QKeyEvent *event);
    void paintFoldHints(QPaintEvent *event);
    QRectF foldHintRect(const QTextBlock &block, const folding::Region &region) const;
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
    CompletionProvider m_completionProvider;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;
    CompletionContext m_completionContext;
    QVector<folding::Region> m_folds;
    QTimer *m_foldTimer = nullptr;
    bool m_applyingFolds = false;
    int m_navDirection = 0; // +1/-1 enquanto uma tecla de navegação é tratada: o cursor pula as linhas dobradas
    QVector<QTextCursor> m_extra; // cursores além do principal (multi-cursor)
    QString m_occurrenceWord; // a palavra que o 1º Ctrl+D selecionou: as próximas ocorrências são só palavras inteiras
    bool m_boxActive = false;
    QTextCursor m_boxAnchor;
};

} // namespace kai::ui
