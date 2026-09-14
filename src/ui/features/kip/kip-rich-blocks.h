#pragma once

#include "ui/features/kip/kip-blocks.h"

#include <QAbstractScrollArea>
#include <QFont>
#include <QTextLayout>

class QLabel;
class QToolButton;
class QGridLayout;

namespace kai::ui {

class DiffView;

// Blocos da rodada 2 do KIP (spec 11 §27): diff, kv, stats e code.

// `diff`: título + resumo (arquivos, +N −N), navegação entre mudanças e o seletor unificado/lado a lado; embaixo, a
// DiffView. A escolha unificado/lado a lado é do usuário (lembrada por comando); o `view` do programa é só o padrão.
class KipDiffBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipDiffBlockWidget(const core::KipDiff &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;
    // A escolha lembrada do usuário ("unified" | "split" | ""): vence a sugestão do programa.
    void setPreferredView(const QString &view);
    DiffView *view() const { return m_view; }
    QToolButton *unifiedButton() const { return m_unified; }
    QToolButton *splitButton() const { return m_split; }
    QLabel *summaryLabel() const { return m_summary; }

signals:
    // O usuário trocou o modo (para lembrar).
    void viewChosen(const QString &view);

private:
    void applyMode(bool split);
    void refreshNav();
    void fitHeader();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QString m_id;
    QByteArray m_contentSignature; // o que já está na DiffView: redesenhar a tela não reabre/fecha nada
    QString m_titleText;
    bool m_userChose = false;
    QString m_preferred;
    QString m_programView;
    bool m_sides = false;
    QLabel *m_title = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_truncated = nullptr;
    QWidget *m_nav = nullptr;
    QLabel *m_navText = nullptr;
    QToolButton *m_prev = nullptr;
    QToolButton *m_next = nullptr;
    QToolButton *m_unified = nullptr;
    QToolButton *m_split = nullptr;
    DiffView *m_view = nullptr;
};

// `kv`: pares rótulo/valor alinhados numa coluna, com copiar no hover, badge e link (só http/https).
class KipKvBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipKvBlockWidget(const core::KipKv &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;
    int rowCount() const { return m_rows; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild(const core::KipKv &block);

    QString m_id;
    QLabel *m_title = nullptr;
    QWidget *m_card = nullptr;
    int m_rows = 0;
};

// `stats`: até 4 números por linha (rótulo pequeno, valor grande, detalhe com um ponto no tom).
class KipStatsBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipStatsBlockWidget(const core::KipStats &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;

private:
    void rebuild(const core::KipStats &block);

    QString m_id;
    QGridLayout *m_grid = nullptr;
};

// Linhas de código pintadas: numeração, linhas destacadas, realce de sintaxe. Rola só na horizontal; a altura é a
// das linhas mostradas.
class KipCodeLinesView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit KipCodeLinesView(QWidget *parent = nullptr);
    void setCode(const QStringList &lines, const QVector<QVector<QTextLayout::FormatRange>> &formats,
                 const QVector<int> &highlight, bool lineNumbers);
    void setVisibleLines(int count); // -1 = todas
    int lineCount() const { return m_lines.size(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void relayout();

    QStringList m_lines;
    QVector<QVector<QTextLayout::FormatRange>> m_formats;
    QVector<int> m_highlight;
    bool m_numbers = true;
    int m_visible = -1;
    QFont m_mono;
    int m_lineHeight = 20;
    int m_numberWidth = 32;
    int m_textWidth = 0;
};

// `code`: cabeçalho (nome, linguagem, copiar) + as linhas; acima de `max_lines` recolhe com "Mostrar tudo".
class KipCodeBlockWidget : public KipBlockWidget {
    Q_OBJECT

public:
    explicit KipCodeBlockWidget(const core::KipCode &block, QWidget *parent = nullptr);
    bool accepts(const core::KipBlock &block) const override;
    void update(const core::KipBlock &block) override;
    KipCodeLinesView *lines() const { return m_lines; }
    QToolButton *showAllButton() const { return m_more; }

private:
    QString m_id;
    QString m_text;
    bool m_expanded = false;
    int m_maxLines = 0;
    QLabel *m_name = nullptr;
    QLabel *m_language = nullptr;
    QToolButton *m_copy = nullptr;
    KipCodeLinesView *m_lines = nullptr;
    QToolButton *m_more = nullptr;
    QLabel *m_truncated = nullptr;
};

} // namespace kai::ui
