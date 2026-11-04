#include "ui/shared/shortcut-capture-field.h"
#include "utils/translation-manager.h"

#include <QKeyEvent>

namespace kai::ui {

ShortcutCaptureField::ShortcutCaptureField(const QString &initialSequence, QWidget *parent)
    : QLineEdit(parent)
{
    setReadOnly(true);
    setPlaceholderText(utils::tr(QStringLiteral("shortcut_capture.placeholder.idle")));
    setKeySequenceString(initialSequence);
}

QString ShortcutCaptureField::keySequenceString() const
{
    return m_sequence.isEmpty() ? QString() : m_sequence.toString(QKeySequence::PortableText);
}

void ShortcutCaptureField::setKeySequenceString(const QString &sequence)
{
    m_sequence = QKeySequence(sequence, QKeySequence::PortableText);
    updateDisplayText();
}

void ShortcutCaptureField::updateDisplayText()
{
    setText(m_sequence.isEmpty()
        ? QString()
        : m_sequence.toString(QKeySequence::NativeText));
}

void ShortcutCaptureField::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    setPlaceholderText(utils::tr(QStringLiteral("shortcut_capture.placeholder.recording")));
}

void ShortcutCaptureField::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();

    // Escape cancela a captura sem alterar o valor e devolve o foco ao próximo widget.
    if (key == Qt::Key_Escape) {
        event->ignore();
        return;
    }

    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        m_sequence = QKeySequence();
        updateDisplayText();
        event->accept();
        return;
    }

    // Ignora modificadores sozinhos: espera a tecla real que os acompanha.
    if (key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt || key == Qt::Key_Meta || key == 0) {
        event->accept();
        return;
    }

    const int modifiers = static_cast<int>(event->modifiers()
        & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier));
    m_sequence = QKeySequence(modifiers | key);
    updateDisplayText();
    event->accept();
}

} // namespace kai::ui
