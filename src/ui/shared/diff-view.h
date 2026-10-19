#pragma once

#include "core/text-diff.h"
#include "ui/features/docs/doc-text-tools.h"

#include <QAbstractScrollArea>
#include <QFont>
#include <QPainterPath>
#include <QHash>
#include <QSet>
#include <QTextLayout>
#include <QVector>

namespace kai::ui {

// Formatos de realce de sintaxe por linha (para pintar com QTextLayout): roda o DocHighlighter do painel Documento
// sobre as linhas, numa ordem só. Texto puro (ou linguagem desconhecida) devolve listas vazias.
QVector<QVector<QTextLayout::FormatRange>> syntaxFormatsForLines(const QStringList &lines,
                                                                 texttools::Language language);
// A linguagem de um diff/código: a pedida (json, yaml, xml, markdown) ou, sem pedido, a da extensão do nome.
texttools::Language languageFor(const QString &requested, const QString &fileName);

// Tabulação vira 4 espaços na tela; os trechos (spans) são remapeados para o texto exibido.
QString diffDisplayText(const QString &text, QVector<core::DiffSpan> *spans = nullptr);

// ============================================================================================================
// DiffView — o diff do Kai (KIP `diff`, e quem mais precisar comparar texto). Pintado e virtualizado: só as linhas
// visíveis são desenhadas, então um patch de milhares de linhas não cria milhares de widgets.
//  - unificado ou lado a lado; arquivos recolhíveis (cabeçalho com status, caminho, +N −N e a barrinha);
//  - destaque por palavra nos pares −/+ e realce de sintaxe nas linguagens que o Documento conhece;
//  - dobras "⋯ N linhas iguais" num diff de arquivo inteiro (clique abre);
//  - navegação entre mudanças;
//  - modo de seleção (KIP campo `diff`): caixas por arquivo (tri-estado) ou por trecho.
// Cores: success/error/info do tema com transparência; `diff_add_bg`/`diff_del_bg` no tema substituem a base.
// ============================================================================================================
class DiffView : public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class Mode { Unified, Split };
    enum class Selection { None, Files, Hunks };

    explicit DiffView(QWidget *parent = nullptr);

    // `language`: pedido explícito (vazio = pela extensão de cada arquivo).
    void setFiles(const QVector<core::DiffFile> &files, const QString &language = QString());
    const QVector<core::DiffFile> &files() const { return m_files; }
    // Arquivos além deste número começam recolhidos (vale no próximo setFiles).
    void setCollapseAfter(int count) { m_collapseAfter = count; }

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    void setFileExpanded(int file, bool expanded);
    bool isFileExpanded(int file) const;

    // ---- seleção (campo `diff`) ----
    void setSelectionMode(Selection selection);
    Selection selectionMode() const { return m_selection; }
    // Ids na ordem: "caminho" (Files) ou "caminho@n" (Hunks).
    QStringList allIds() const;
    QStringList selectedIds() const;
    void setSelectedIds(const QStringList &ids);
    void setInteractive(bool interactive);
    int selectedAdditions() const;
    int selectedDeletions() const;

    // ---- navegação entre mudanças (blocos de linhas −/+) ----
    int changeCount() const { return m_changes.size(); }
    int currentChange() const { return m_currentChange; }
    void goToChange(int index);
    void nextChange();
    void previousChange();

    // Altura máxima antes de rolar por dentro (a tela KIP é que rola o resto).
    void setMaxVisibleHeight(int px);
    int contentHeight() const { return m_contentHeight; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Testes: quantas linhas visuais (cabeçalhos + linhas) existem agora.
    int visualRowCount() const { return m_rows.size(); }

signals:
    void selectionChanged();
    void currentChangeChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void changeEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    struct Row {
        enum class Kind { FileHeader, HunkHeader, Line, Split, Fold, Note };
        Kind kind = Kind::Line;
        int file = -1;
        int hunk = -1;
        int line = -1;  // índice em hunk.lines (Line/Fold); Split: lado esquerdo
        int right = -1; // Split: lado direito
        int fold = -1;  // >= 0: linha `fold` de dentro da dobra `line` já aberta
        int rightFold = -1;
        int y = 0;
        int height = 0;
        int change = -1; // índice da mudança a que a linha pertence
        QString note;
    };
    struct Change {
        int file = 0;
        int hunk = 0;
        int firstLine = 0;
    };

    void rebuildRows();
    void rebuildMetrics();
    void computeFormats();
    void updateScrollBars();
    int rowAt(int y) const;
    QRect rowRect(const Row &row) const;
    void ensureRowVisible(int index);
    bool hunkExpanded(int file, int hunk) const;
    QString fileId(int file) const;
    QString hunkId(int file, int hunk) const;
    int checkState(int file) const; // 0 nada, 1 parcial, 2 tudo
    void toggleFile(int file);
    void toggleHunk(int file, int hunk);
    void activateRow(int index, const QPoint &pos, bool viaKeyboard);
    const core::DiffLine *lineFor(const Row &row, bool right) const;
    const QVector<QTextLayout::FormatRange> *formatsFor(const Row &row, bool right) const;
    QString rowCopyText(const Row &row) const;

    void paintFileHeader(QPainter &p, const Row &row, const QRect &r, bool hovered);
    void paintHunkHeader(QPainter &p, const Row &row, const QRect &r, bool hovered);
    void paintUnifiedLine(QPainter &p, const Row &row, const QRect &r);
    void paintSplitLine(QPainter &p, const Row &row, const QRect &r);
    void paintFold(QPainter &p, const Row &row, const QRect &r, bool hovered);
    void paintNote(QPainter &p, const Row &row, const QRect &r);
    void paintCode(QPainter &p, const core::DiffLine &line, const QVector<QTextLayout::FormatRange> *formats,
                   const QRect &textRect, const QColor &wordColor);
    void paintCheck(QPainter &p, const QRect &box, int state) const;
    QRect checkRect(const QRect &rowRect, int indent) const;
    QPainterPath cardPath(int file) const;

    static quint64 key(int file, int hunk, int line) { return (quint64(file) << 42) | (quint64(hunk) << 21) | quint64(line); }

    QVector<core::DiffFile> m_files;
    QString m_language;
    QVector<bool> m_fileExpanded;
    QSet<quint64> m_hunkExpanded; // modo Hunks: trechos abertos
    QSet<quint64> m_foldOpen;     // dobras abertas
    QHash<quint64, QVector<QTextLayout::FormatRange>> m_formats;      // linha comum
    QHash<quint64, QVector<QVector<QTextLayout::FormatRange>>> m_foldFormats; // linhas de dentro de uma dobra
    QSet<QString> m_selected;
    QVector<Row> m_rows;
    QVector<Change> m_changes;
    int m_currentChange = -1;
    Mode m_mode = Mode::Unified;
    Selection m_selection = Selection::None;
    bool m_interactive = true;
    int m_collapseAfter = 3;
    int m_hover = -1;
    int m_cursor = -1;
    int m_maxVisibleHeight = 520;
    int m_contentHeight = 0;

    // métricas
    QFont m_mono;
    int m_lineHeight = 20;
    int m_headerHeight = 32;
    int m_numberWidth = 40;
    int m_signWidth = 18;
    int m_textWidth = 0; // maior linha dos arquivos abertos
    QVector<int> m_fileTextWidth;               // maior linha de cada arquivo
    QVector<QPair<int, int>> m_fileSpans;       // [y inicial, y final) do cartão de cada arquivo
    bool m_keyboardCursor = false;              // o contorno do cursor só aparece navegando pelo teclado
    int m_charWidth = 8;
};

} // namespace kai::ui
