#pragma once

#include <QLineEdit>
#include <QKeySequence>

namespace kai::ui {

// Campo de captura de atalho: em vez de digitar a string, o usuário foca e pressiona a combinação, que vira
// QKeySequence e é exibida formatada (ex.: "Ctrl+Shift+N"). Escape cancela e restaura o valor anterior;
// Backspace/Delete limpam.
class ShortcutCaptureField : public QLineEdit {
    Q_OBJECT

public:
    explicit ShortcutCaptureField(const QString &initialSequence = QString(), QWidget *parent = nullptr);

    // Sequência atual como string canônica de QKeySequence (ex.: "Ctrl+Q"); vazia sem atalho.
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
