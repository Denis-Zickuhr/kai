#pragma once

#include <QString>

namespace kai::core {

// ============================================================================
// LISTA DE CONFIANÇA do modo local de CLI Paths (decidido na conversa de
// design: "Pode ser, ter uma trust list pro cli"). Rodar `kai <cli_path>`
// dentro de um diretório EXECUTA o que estiver no kai.json/kai.yml de lá
// automaticamente — sem exigir nada "importado" no app antes (é o ponto
// central da portabilidade). Isso muda o modelo de confiança de sempre do
// Kai (nada roda sem um clique deliberado na GUI), então, mesmo espírito
// do .envrc do direnv: a primeira vez que um arquivo NOVO (ou mudado) é
// visto num diretório, a execução pede confirmação uma vez; aceitar guarda
// o hash do conteúdo, e só pede de novo se o arquivo mudar depois.
//
// Guarda só o HASH do conteúdo (não o path do arquivo em si) por
// diretório — não é uma trava de segurança forte (qualquer um com acesso
// de escrita ao arquivo JSON de confiança pode editá-lo à mão), é só um
// freio de "olha antes de rodar", igual ao direnv.
// ============================================================================

class CliTrustStore {
public:
    // `storePath` vazio (padrão) usa QStandardPaths::AppConfigLocation.
    explicit CliTrustStore(const QString &storePath = QString());

    QString filePath() const;

    // true se `directoryPath` já foi confiado com ESTE hash de conteúdo
    // exato — um hash diferente (arquivo mudou desde a última confiança)
    // volta false, exigindo confirmar de novo.
    bool isTrusted(const QString &directoryPath, const QString &contentHash) const;

    // Marca `directoryPath` como confiável para este `contentHash`.
    void trust(const QString &directoryPath, const QString &contentHash);

    // Hash estável (SHA-256, hex) do conteúdo — usado tanto ao checar
    // quanto ao gravar confiança, pra garantir que os dois lados sempre
    // comparam o mesmo tipo de valor.
    static QString hashContent(const QString &text);

private:
    QString m_storePath;
};

} // namespace kai::core
