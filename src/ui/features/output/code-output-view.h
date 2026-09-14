#pragma once

#include <QPlainTextEdit>

namespace kai::ui {

// ============================================================================
// Área de saída com GUTTER de números de linha
// ----------------------------------------------------------------------------
// O QPlainTextEdit não tem números de linha nativos e `setViewportMargins` é
// protegido, então a única forma correta de reservar a margem é subclassear.
// Segue o padrão canônico do Qt: um widget filho desenha os números e o editor
// reserva a margem esquerda para ele.
//
// Sem Q_OBJECT de propósito: não declara sinais/slots novos, apenas sobrescreve
// eventos — assim não precisa passar pelo moc.
// ============================================================================
class CodeOutputView : public QPlainTextEdit {
public:
    explicit CodeOutputView(QWidget *parent = nullptr);

    void setLineNumbersVisible(bool visible);
    bool lineNumbersVisible() const { return m_showLineNumbers; }

    // Cores do gutter (vêm dos design tokens; atualizadas na troca de tema).
    void setGutterColors(const QColor &background, const QColor &foreground);

    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent *event);

    // LINKS CLICÁVEIS: detecta a URL sob uma posição do viewport. Devolve
    // string vazia se não houver link ali. (Comportamento restaurado — havia
    // sido perdido na migração para o painel de saída v2.)
    QString urlAt(const QPoint &viewportPos) const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);

    QWidget *m_lineNumberArea = nullptr;
    bool m_showLineNumbers = false;
    QColor m_gutterBg;
    QColor m_gutterFg;
};

} // namespace kai::ui
