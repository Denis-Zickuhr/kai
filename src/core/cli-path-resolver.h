#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// ============================================================================
// RESOLUÇÃO DE CLI PATH — walks a árvore de folders/commands JÁ COLAPSADA
// (pastas sem cli_path são transparentes: invisíveis como segmento, mas não
// bloqueiam os próprios filhos com cli_path, que "sobem" pro escopo do
// ancestral opt-in mais próximo, ou a raiz) casando um a um contra os
// tokens do argv. Mesma regra de colisão já verificada estaticamente por
// core::validateKaiFileText — aqui é o motor que de fato RESOLVE um
// caminho em tempo de execução, sobre dados JÁ CARREGADOS (CommandsData),
// não sobre o JSON bruto de um arquivo.
//
// Puramente lógico — sem I/O, sem execução. O que fazer com o resultado
// (rodar um comando, listar os filhos de uma pasta) é responsabilidade de
// quem chama.
// ============================================================================

struct CliPathChildEntry {
    QString cliPath;      // o segmento em si (ex: "env")
    QString label;        // nome de exibição (Folder::name ou Command::name)
    QString description;  // Command::description, se houver (folders não têm)
    bool isFolder = false;
    QString targetId;     // Folder::id ou Command::id
};

struct CliPathResolution {
    enum class Kind {
        Command,  // um Command foi alcançado — pronto pra rodar
        Folder,   // caminho válido até aqui, mas parou numa pasta (sem mais
                  // tokens, ou o próximo token não bateu com nada) — lista
                  // os filhos pra uma tela de ajuda/descoberta automática
        NotFound  // o PRIMEIRO token já não bateu com nada neste escopo
    };
    Kind kind = Kind::NotFound;

    // Kind::Command apenas.
    QString commandId;
    // Argumentos que sobraram depois do último segmento reconhecido —
    // são os candidatos a parâmetro posicional/flag do comando.
    QStringList remainingArgs;

    // Kind::Folder apenas (vazio = a pasta "colapsada" raiz, sem id real
    // correspondente — pode não haver NENHUMA pasta real com cli_path).
    QString folderId;

    // Kind::Folder E Kind::NotFound: os filhos alcançáveis daqui (ou da
    // raiz, se NotFound aconteceu no primeiro token) — útil tanto pra
    // listar automaticamente (`kai zephyr` sozinho) quanto pra sugerir
    // opções válidas num erro ("não achou 'env', você quis dizer...").
    QVector<CliPathChildEntry> children;
};

class CliPathResolver {
public:
    CliPathResolver(const QVector<Folder> &folders, const QVector<Command> &commands);

    // Resolve os tokens a partir da RAIZ do namespace de CLI (todo o
    // conteúdo de `folders`/`commands` passado ao construtor — em modo
    // local, isso é só o arquivo lido do diretório atual; em modo global,
    // é a árvore de UM projeto importado específico, nunca o app inteiro).
    CliPathResolution resolve(const QStringList &args) const;

    // Filhos diretos da raiz — o que `kai` (ou `kai <projeto>` já dentro do
    // projeto, em modo global) mostraria sem mais nenhum token.
    QVector<CliPathChildEntry> rootChildren() const;

private:
    QVector<Folder> m_folders;
    QVector<Command> m_commands;

    const Folder *folderById(const QString &id) const;
    // Escopo (id de pasta, ou string vazia = raiz) em que uma pasta/comando
    // aparece no namespace de CLI já colapsado — ver comentário no .cpp.
    QString scopeForFolder(const QString &folderId) const;
    QString scopeForCommandsFolder(const QString &folderId) const;
    QVector<CliPathChildEntry> childrenOfScope(const QString &scopeFolderId) const;
};

} // namespace kai::core
