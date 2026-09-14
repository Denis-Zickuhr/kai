#pragma once

#include <QString>
#include <QTextCharFormat>
#include <QVector>

namespace kai::ui {

// Um segmento de texto já com o QTextCharFormat resolvido a partir dos
// códigos de escape ANSI SGR (\x1b[...m) encontrados na entrada. `isLink`
// indica que o texto do segmento é uma URL detectada automaticamente
// (auto-highlight de links no Terminal Drawer).
struct AnsiSegment {
    QString text;
    QTextCharFormat format;
    bool isLink = false;
};

// Parser puro (sem dependência de widgets) de sequências ANSI SGR para uso
// no Terminal Drawer. Suporta cores básicas (30-37, 90-97),
// background (40-47, 100-107), bold, reset e SGR combinados
// (ex: "\x1b[1;31m").
//
// Mantém estado entre chamadas (m_currentFormat) para permitir parsing
// incremental de um stream de saída de processo, onde uma sequência ANSI
// pode começar em um chunk e continuar aplicada aos chunks seguintes.
class AnsiTextParser {
public:
    AnsiTextParser();

    // Processa um chunk de texto (pode conter 0, 1 ou várias sequências
    // ANSI) e retorna os segmentos resultantes, já com formatação aplicada.
    QVector<AnsiSegment> parse(const QString &input);

    // Reseta o formato acumulado para o padrão (equivalente a \x1b[0m).
    void resetFormat();

private:
    static QColor ansiColorFor(int code, bool bright);

    QTextCharFormat m_currentFormat;
    QTextCharFormat m_defaultFormat;
    // Cauda pendente: se um chunk termina com uma sequência de escape
    // INCOMPLETA (ex: "\x1b[3" e o "1m" vem no próximo chunk), guardamos
    // aqui e prependamos na próxima chamada. Sem isto a cor não aplicava e
    // vazava lixo tipo "1m" como texto (achado de auditoria).
    QString m_pendingTail;
};

} // namespace kai::ui
