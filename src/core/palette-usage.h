#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace kai::core {

// Quanto cada item da paleta (Ctrl+K) foi usado, para ele subir na lista. Estado LOCAL e pessoal (palette-usage.json):
// fora de exportação e sync, sem telemetria. A chave é "<tipo>:<id>", montada por quem chama.
class PaletteUsage {
public:
    // Quantos itens a memória guarda; passando disso os de menor pontuação saem.
    static constexpr int kMaxEntries = 400;

    void record(const QString &key, qint64 nowMs);
    // Frequência com esquecimento: cada uso vale 1 e perde metade do peso a cada 14 dias. 0 se nunca foi usado.
    double score(const QString &key, qint64 nowMs) const;
    bool isEmpty() const { return m_uses.isEmpty(); }
    int size() const { return int(m_uses.size()); }

    QJsonObject toJson() const;
    static PaletteUsage fromJson(const QJsonObject &root);

private:
    struct Use {
        int count = 0;
        qint64 lastMs = 0;
    };
    static double weight(const Use &use, qint64 nowMs);

    QHash<QString, Use> m_uses;
};

} // namespace kai::core
