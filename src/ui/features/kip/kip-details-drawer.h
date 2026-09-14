#pragma once

#include "engine/kip-session.h"

#include <QFrame>
#include <QWidget>

class QPlainTextEdit;
class QStackedWidget;

class QToolButton;

namespace kai::ui {

// Gaveta "Detalhes" da view KIP (§13.3), recolhida por padrão: aba Log (stderr +
// stdout que não é protocolo + avisos do Kai) e aba Protocolo (toda mensagem nos
// dois sentidos, com horário). É o ÚNICO lugar da tela que parece um terminal.
// Direção: `←` = chegou do programa; `→` = o Kai enviou ao programa.
class KipDetailsDrawer : public QWidget {
    Q_OBJECT

public:
    explicit KipDetailsDrawer(QWidget *parent = nullptr);

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);
    // Zera as abas e carrega o histórico de `session` (nullptr só limpa).
    void reload(const engine::KipSession *session);
    void appendLog(const QString &text);
    void appendTrace(const engine::KipTraceEntry &entry);

    QString logText() const;
    QString protocolText() const;
    QPlainTextEdit *logView() const { return m_log; }
    QPlainTextEdit *protocolView() const { return m_protocol; }
    // O botão "Detalhes": quem hospeda pode levá-lo para outra linha (o rodapé da view KIP), e então a gaveta só
    // aparece aberta.
    QToolButton *toggleButton() const { return m_toggle; }
    // O seletor Log | Protocolo, que acompanha o botão (visível só aberto).
    QWidget *pageSwitch() const { return m_switch; }

signals:
    void expandedChanged(bool expanded);

private:
    void applyStyle();

    QToolButton *m_toggle = nullptr;
    QWidget *m_body = nullptr;
    QFrame *m_switch = nullptr;              // Log | Protocolo (só aparece aberto)
    QList<QToolButton *> m_pageButtons;
    QStackedWidget *m_pages = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPlainTextEdit *m_protocol = nullptr;
    bool m_expanded = false;
};

} // namespace kai::ui
