#pragma once

#include <QWidget>

#include "core/kip-settings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace kai::ui {

// Aba "KIP": preferências globais do protocolo (spec 11 §23) — tempos de espera,
// respostas lembradas e comportamento do Detalhes. Por comando, o que existe é só
// "Interface KIP" / "Abrir em janela própria" no editor (comandos shell).
class KipTab : public QWidget {
    Q_OBJECT

public:
    explicit KipTab(const core::KipSettings &settings, QWidget *parent = nullptr);

    core::KipSettings settings() const;

    QSpinBox *handshakeTimeoutField() const { return m_handshake; }
    QSpinBox *changeTimeoutField() const { return m_change; }
    QSpinBox *cancelGraceField() const { return m_grace; }
    QCheckBox *rememberAnswersField() const { return m_remember; }
    QCheckBox *expandDetailsField() const { return m_expandDetails; }
    QComboBox *detachedWindowModeField() const { return m_detachedMode; }
    QPushButton *clearRememberedButton() const { return m_clearRemembered; }

    // Resultado da limpeza (feito pelo diálogo, que tem os comandos).
    void showClearedCount(int commands);

signals:
    // O usuário pediu para esquecer as respostas lembradas de TODOS os comandos.
    void clearRememberedRequested();

private:
    QSpinBox *m_handshake = nullptr;
    QSpinBox *m_change = nullptr;
    QSpinBox *m_grace = nullptr;
    QCheckBox *m_remember = nullptr;
    QCheckBox *m_expandDetails = nullptr;
    QComboBox *m_detachedMode = nullptr;
    QPushButton *m_clearRemembered = nullptr;
    QLabel *m_clearedLabel = nullptr;
};

} // namespace kai::ui
