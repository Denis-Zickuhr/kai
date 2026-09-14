#pragma once

#include <QString>
#include <QWidget>

class QPlainTextEdit;
class QToolButton;

namespace kai::ui {

// Campo de código compacto que cresce com o conteúdo (até um teto) e um botão de expandir que abre o mesmo
// conteúdo num editor grande e redimensionável.
class InlineCodeField : public QWidget {
    Q_OBJECT

public:
    explicit InlineCodeField(QWidget *parent = nullptr);

    QString toPlainText() const;
    void setPlainText(const QString &text);

    void setPlaceholderText(const QString &text);
    // Título usado na janela de expansão (ex: "Comando", "Body (JSON)").
    void setEditorTitle(const QString &title);
    // Linhas visíveis no estado compacto (padrão 2) e no máximo ao crescer (padrão 6).
    void setLineRange(int minLines, int maxLines);
    // Liga o realce de sintaxe JSON no campo e no editor expandido.
    void setJsonSyntax(bool enabled);
    // Usa o fundo de campo normal (bg do tema) em vez do de editor de código (codeBg), para combinar com os outros
    // campos do formulário (Textarea de parâmetro).
    void setPlainField(bool plain);

    QPlainTextEdit *editor() const { return m_edit; }

signals:
    void textChanged();

public slots:
    // Abre o editor grande. Também acessível pelo botão e por Ctrl+Enter.
    void expand();

private:
    void updateHeightForContent();

    QPlainTextEdit *m_edit = nullptr;
    QToolButton *m_expandButton = nullptr;
    QString m_editorTitle;
    int m_editPadding = 0;   // padding vertical do QSS, usado no cálculo da altura
    int m_editPaddingX = 0;  // padding horizontal do QSS
    int m_minLines = 2;
    int m_maxLines = 6;
    bool m_jsonSyntax = false;
};

} // namespace kai::ui
