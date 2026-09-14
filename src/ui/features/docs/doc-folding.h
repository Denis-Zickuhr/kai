#pragma once

#include "ui/features/docs/doc-text-tools.h"

#include <QString>
#include <QVector>

namespace kai::ui::folding {

// Uma região dobrável: a linha de cabeçalho (`start`) fica à vista; de `start + 1` até `end` as linhas somem ao dobrar.
// Linhas começam em 0. `suffix` é o que sobra do fim da região (a linha de fechamento: "}", "],", "</item>") e aparece
// depois do "⋯" no cabeçalho dobrado; vazio quando o fechamento continua visível ou não existe (YAML).
struct Region {
    int start = 0;
    int end = 0;
    QString suffix;

    bool operator==(const Region &other) const
    {
        return start == other.start && end == other.end && suffix == other.suffix;
    }
};

// Esta linguagem tem dobras (JSON, YAML e XML)?
bool supports(texttools::Language language);

// As regiões do texto, por linha de início, sem repetir o início (vale a mais externa). Função pura: texto entra,
// regiões saem; texto malformado dá o que foi possível casar, sem recusar. Texto acima do teto (4 MB) não é analisado.
QVector<Region> regions(const QString &text, texttools::Language language);

} // namespace kai::ui::folding
