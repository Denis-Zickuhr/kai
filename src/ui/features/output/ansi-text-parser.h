#pragma once

#include <QString>
#include <QTextCharFormat>
#include <QVector>

namespace kai::ui {

// Segmento de texto com o QTextCharFormat resolvido dos códigos SGR; `isLink` marca URL detectada automaticamente.
struct AnsiSegment {
    QString text;
    QTextCharFormat format;
    bool isLink = false;
};

// Parser puro (sem widgets) de ANSI SGR: cores básicas (30-37, 90-97), fundo (40-47, 100-107), bold, reset e SGR
// combinados ("\x1b[1;31m"). Mantém estado (m_currentFormat) entre chamadas para parsing incremental de um stream,
// onde a cor de uma sequência continua nos chunks seguintes.
class AnsiTextParser {
public:
    AnsiTextParser();

    // Processa um chunk (0, 1 ou várias sequências) e devolve os segmentos já formatados.
    QVector<AnsiSegment> parse(const QString &input);

    // Volta ao formato padrão (equivale a \x1b[0m).
    void resetFormat();

private:
    static QColor ansiColorFor(int code, bool bright);

    QTextCharFormat m_currentFormat;
    QTextCharFormat m_defaultFormat;
    // Cauda pendente: chunk terminado em escape incompleto (ex: "\x1b[3" + "1m" no próximo) fica guardado e é
    // prefixado na próxima chamada.
    QString m_pendingTail;
};

} // namespace kai::ui
