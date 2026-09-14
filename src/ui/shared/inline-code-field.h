#pragma once

#include <QString>
#include <QWidget>

class QPlainTextEdit;
class QToolButton;

namespace kai::ui {

// ============================================================================
// CAMPO DE CÓDIGO INLINE (com expansão)
// ----------------------------------------------------------------------------
// Feedback do usuário: "o campo de preencher e criar scripts atual é muito
// grande". Antes o editor de comando era um QPlainTextEdit de 90px (e o body
// HTTP de 180px) ocupando o form inteiro mesmo para um comando de uma linha.
//
// Este widget resolve com um campo COMPACTO que cresce sozinho conforme o
// conteúdo (até um teto), mais um botão de EXPANDIR que abre o mesmo conteúdo
// num editor grande e redimensionável quando o usuário realmente quer editar
// um script maior.
// ============================================================================
class InlineCodeField : public QWidget {
    Q_OBJECT

public:
    explicit InlineCodeField(QWidget *parent = nullptr);

    QString toPlainText() const;
    void setPlainText(const QString &text);

    void setPlaceholderText(const QString &text);
    // Título usado na janela de expansão (ex: "Comando", "Body (JSON)").
    void setEditorTitle(const QString &title);
    // Quantidade de linhas visíveis no estado compacto (padrão 2) e no
    // máximo, ao crescer com o conteúdo (padrão 6).
    void setLineRange(int minLines, int maxLines);
    // Liga o realce de sintaxe JSON no campo e no editor expandido.
    void setJsonSyntax(bool enabled);
    // Usa o fundo de CAMPO NORMAL (bg do tema) em vez do fundo de EDITOR DE
    // CÓDIGO (codeBg, mais escuro). Para Textarea de parâmetro, que deve
    // combinar com os outros campos do formulário, não parecer um bloco de
    // código destoante (relatado).
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
    int m_editPadding = 0;   // padding do QSS, usado no cálculo da altura
    int m_minLines = 2;
    int m_maxLines = 6;
    bool m_jsonSyntax = false;
};

} // namespace kai::ui
