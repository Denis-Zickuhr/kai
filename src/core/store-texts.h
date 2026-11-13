#pragma once

#include <QString>

namespace kai::core {

// Textos (traduzidos) dos códigos estáveis da Kai Store, compartilhados pelo CLI e pela janela.

// Erro de rede/instalação ("hash_mismatch", "timeout"...). `detail` (arquivo, saída do git) entra entre parênteses.
QString storeErrorText(const QString &code, const QString &detail = QString());
// Problema de manifesto/publicação ("bad_sha256", "unlisted_file"...); `path` é o campo ou arquivo.
QString storeIssueText(const QString &code, const QString &path = QString());
// "kai_min:2.0.0" / "capability:x" -> "Requer o Kai 2.0.0" / "Requer o recurso x".
QString storeCompatText(const QString &reason);
QString storeKindLabel(const QString &kind);
QString storeTrustLabel(const QString &trust);

} // namespace kai::core
