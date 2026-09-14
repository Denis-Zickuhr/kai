#pragma once

#include <QWidget>

#include "core/interpreter-settings.h"

class QLineEdit;

namespace kai::ui {

// Aba "Linguagens": interpretadores globais dos comandos Python e Node. Cada
// valor é uma linha de shell resolvida onde o comando roda (alvo de terminal
// incluso); um comando pode sobrepô-la no próprio editor.
class LanguagesTab : public QWidget {
    Q_OBJECT

public:
    explicit LanguagesTab(const core::InterpreterSettings &settings, QWidget *parent = nullptr);

    core::InterpreterSettings settings() const;

    QLineEdit *pythonField() const { return m_python; }
    QLineEdit *nodeField() const { return m_node; }
    QLineEdit *phpField() const { return m_php; }

private:
    QLineEdit *m_python = nullptr;
    QLineEdit *m_node = nullptr;
    QLineEdit *m_php = nullptr;
};

} // namespace kai::ui
