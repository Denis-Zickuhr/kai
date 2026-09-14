#pragma once

#include <QPlainTextEdit>

namespace kai::ui {

// Área de saída com gutter de números de linha. QPlainTextEdit não tem números nativos e `setViewportMargins` é
// protegido, então reservar a margem exige subclassear (um widget filho desenha os números). Sem Q_OBJECT de
// propósito: só sobrescreve eventos, sem sinais/slots novos.
class CodeOutputView : public QPlainTextEdit {
public:
    explicit CodeOutputView(QWidget *parent = nullptr);

    void setLineNumbersVisible(bool visible);
    bool lineNumbersVisible() const { return m_showLineNumbers; }

    void setGutterColors(const QColor &background, const QColor &foreground);

    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent *event);

    // URL sob uma posição do viewport, ou string vazia.
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
