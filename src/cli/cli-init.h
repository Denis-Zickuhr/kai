#pragma once

#include <QString>
#include <QStringList>

namespace kai::cli {

// `kai init [--force]`: cria um kai.yml na pasta atual
// a partir da mesma detecção genérica do import de projeto (package.json,
// docker-compose.yml, Makefile, pyproject...), já com cli_path e descrições.
// Sem nada detectado, um exemplo mínimo pra editar.
int runInitVerb(const QStringList &args);

struct InitManifest {
    QString text;           // conteúdo do arquivo
    int commandCount = 0;
    QStringList ecosystems; // grupos detectados (vazio = exemplo mínimo)
};

// Monta o manifesto de `directory` (puro sobre o disco — não grava nada).
InitManifest buildInitManifest(const QString &directory);

// "Subir tudo (docker compose up)" -> "subir-tudo-docker-compose-up".
QString cliSlug(const QString &text);

} // namespace kai::cli
