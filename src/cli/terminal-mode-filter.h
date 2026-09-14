#pragma once

#include <QString>

namespace kai::cli {

// Remove, da saída de um comando repassada ao terminal REAL do usuário, as
// sequências DECSET/DECRST (ESC[?<n>h / ESC[?<n>l) que mudam o modo de
// ENTRADA do terminal — win32-input-mode (9001), focus events (1004),
// mouse tracking, bracketed paste, cursor keys de aplicação — e a
// visibilidade do cursor (25). Também remove o que "toma a tela" de quem
// chamou: limpar a tela (ESC[2J/3J), cursor pro canto (ESC[H) e troca de
// título da aba (OSC 0/1/2) — o ConPTY abre toda execução com isso.
//
// Bug real: no Windows o comando roda sob um ConPTY, e o conhost dele emite
// ESC[?9001h ESC[?1004h ao iniciar. No modo CLI (`kai <cli_path>`) essa
// saída ia crua pro terminal de quem chamou (ex: Windows Terminal via WSL
// interop), que ficava em win32-input-mode depois que o kai saía — cada
// tecla virava lixo tipo "6;38;108;1;32;1_" no prompt. Esses modos
// pertencem ao shell de quem chamou, nunca a um comando não-interativo.
//
// POSIÇÕES ABSOLUTAS viram RELATIVAS (bug relatado: o progresso do
// `docker compose` aparecia no TOPO da tela, por cima do que já estava lá).
// O ConPTY é um emulador de tela: um "sobe 2 linhas" do programa (ESC[2A)
// sai dele como "vá pra linha 2" (ESC[2;1H) da tela VIRTUAL — medido com
// um .exe de diagnóstico. No terminal de quem chamou, "linha 2" é o topo da
// tela. O filtro acompanha em que linha da tela virtual o cursor está e
// reescreve cada posição absoluta como movimento a partir do cursor real;
// linhas ainda não impressas viram quebras de linha (o terminal rola).
//
// Com estado: uma sequência pode chegar partida entre dois chunks, então o
// prefixo incompleto fica retido até o próximo feed()/flush().
class TerminalModeFilter {
public:
    QString feed(const QString &chunk);
    // Devolve o que ficou retido (sequência incompleta no fim do stream).
    QString flush();

private:
    void trackText(QStringView text);
    QString moveTo(int row, int column);
    QString moveToRow(int row);

    QString m_pending;
    // Linha do cursor na tela virtual (1 = onde a saída começou) e a mais
    // baixa já alcançada — abaixo dela o terminal real ainda não tem linha.
    int m_row = 1;
    int m_maxRow = 1;
    bool m_atLineStart = true;
};

} // namespace kai::cli
