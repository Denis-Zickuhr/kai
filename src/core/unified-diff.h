#pragma once

#include "core/text-diff.h"

#include <QString>
#include <QVector>

namespace kai::core {

// Lê a saída de `git diff` / `diff -u` (um ou vários arquivos): renomes, arquivos novos/apagados, binários e os
// trechos "@@". O que não reconhece é ignorado — um patch estranho rende menos arquivos, nunca um erro. Os pares
// removida/adicionada já saem com o destaque por palavra.
QVector<DiffFile> parseUnifiedDiff(const QString &patch);

} // namespace kai::core
