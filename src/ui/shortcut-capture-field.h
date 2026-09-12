#pragma once

#include <QLineEdit>
#include <QKeySequence>

namespace kai::ui {

// Campo de captura de atalho "inteligente" (feedback do
// usuário: "um campo inteligente que detecta os pressionados e os carrega
// automaticamente"). Somente leitura para digitação livre: em vez de o
// usuário digitar manualmente a string do atalho (sujeito a erro de
// sintaxe), ele foca o campo e pressiona a combinação de teclas desejada,
// que é capturada via keyPressEvent, convertida em QKeySequence e exibida
// já formatada (ex: "Ctrl+Shift+N"). Escape cancela a captura em curso e
// restaura o valor anterior; Backspace/Delete com o campo focado limpam o
// atalho.
class ShortcutCaptureField : public QLineEdit {
    Q_OBJECT

public:
    explicit ShortcutCaptureField(const QString &initialSequence = QString(), QWidget *parent = nullptr);

    // Sequência atual como string canônica de QKeySequence (ex: "Ctrl+Q").
    // Vazia se nenhum atalho estiver definido.
    QString keySequenceString() const;

    void setKeySequenceString(const QString &sequence);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    void updateDisplayText();

    QKeySequence m_sequence;
};

} // namespace kai::ui
