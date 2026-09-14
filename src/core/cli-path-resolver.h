#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Resolve tokens do argv sobre a árvore de folders/commands JÁ COLAPSADA: pastas sem cli_path são
// transparentes (os filhos com cli_path sobem pro escopo do ancestral opt-in mais próximo, ou a raiz).
// Mesma regra de colisão de core::validateKaiFileText, aqui sobre dados já carregados (CommandsData).
// Puramente lógico (sem I/O nem execução); o que fazer com o resultado é de quem chama.

struct CliPathChildEntry {
    QString cliPath;      // o segmento em si (ex: "env")
    QString label;        // nome de exibição (Folder::name ou Command::name)
    QString description;  // Command::description ou Folder::cliDescription, se houver
    bool isFolder = false;
    QString targetId;     // Folder::id ou Command::id
};

struct CliPathResolution {
    enum class Kind {
        Command,  // um Command foi alcançado — pronto pra rodar
        Folder,   // parou numa pasta (sem mais tokens, ou o próximo não bateu): lista os filhos
        NotFound  // o PRIMEIRO token já não bateu com nada neste escopo
    };
    Kind kind = Kind::NotFound;

    // Kind::Command apenas.
    QString commandId;
    // Argumentos que sobraram depois do último segmento reconhecido (candidatos a posicional/flag).
    QStringList remainingArgs;

    // Kind::Folder apenas (vazio = a raiz "colapsada", que pode não ter NENHUMA pasta real com cli_path).
    QString folderId;

    // Kind::Folder e Kind::NotFound: os filhos alcançáveis daqui (da raiz, se NotFound no 1º token),
    // para listagem automática e sugestões de erro.
    QVector<CliPathChildEntry> children;
};

class CliPathResolver {
public:
    CliPathResolver(const QVector<Folder> &folders, const QVector<Command> &commands);

    // Resolve a partir da RAIZ do namespace de CLI (todo o `folders`/`commands` do construtor: no modo
    // local, o arquivo do diretório atual; no global, a árvore de UM projeto importado).
    CliPathResolution resolve(const QStringList &args) const;

    // Filhos diretos da raiz: o que `kai` mostraria sem nenhum token.
    QVector<CliPathChildEntry> rootChildren() const;

private:
    QVector<Folder> m_folders;
    QVector<Command> m_commands;

    const Folder *folderById(const QString &id) const;
    // Escopo (id de pasta; vazio = raiz) em que uma pasta/comando aparece no namespace colapsado.
    QString scopeForFolder(const QString &folderId) const;
    QString scopeForCommandsFolder(const QString &folderId) const;
    QVector<CliPathChildEntry> childrenOfScope(const QString &scopeFolderId) const;
};

} // namespace kai::core
