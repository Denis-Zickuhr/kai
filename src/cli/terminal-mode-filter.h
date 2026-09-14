#pragma once

#include <QString>

namespace kai::cli {

// Filtra a saída de um comando repassada ao terminal REAL de quem chamou. Remove:
//  - DECSET/DECRST (ESC[?<n>h/l) de modos de ENTRADA (win32-input-mode 9001, focus 1004, mouse,
//    bracketed paste, cursor keys de aplicação) e da visibilidade do cursor (25): o ConPTY os emite
//    ao iniciar e deixavam o terminal de quem chamou em win32-input-mode depois que o kai saía;
//  - limpar tela (ESC[2J/3J), cursor pro canto (ESC[H) e título da aba (OSC 0/1/2), que o ConPTY
//    emite em toda execução.
// Posições ABSOLUTAS viram RELATIVAS: o ConPTY emula uma tela e traduz "sobe 2 linhas" em "vá pra
// linha 2" da tela virtual, que no terminal real seria o topo. O filtro acompanha a linha do cursor
// virtual; linhas ainda não impressas viram quebras de linha.
// Com estado: uma sequência partida entre dois chunks fica retida até o próximo feed()/flush().
class TerminalModeFilter {
public:
    QString feed(const QString &chunk);
    QString flush();

private:
    void trackText(QStringView text);
    QString moveTo(int row, int column);
    QString moveToRow(int row);

    QString m_pending;
    // Linha do cursor na tela virtual (1 = onde a saída começou) e a mais baixa já alcançada.
    int m_row = 1;
    int m_maxRow = 1;
    bool m_atLineStart = true;
};

} // namespace kai::cli
