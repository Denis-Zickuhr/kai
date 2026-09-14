#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace kai::core {

// Resultado de uma invocação do helper `kai kip` (spec 11 §16).
struct KipCliResult {
    // Bytes para o stdout, prontos (terminam em '\n'). Vazio em qualquer erro:
    // o stdout é o canal do protocolo e nunca recebe lixo.
    QByteArray output;
    // Mensagem para o stderr (uso inválido / ajuda de erro). Vazio no sucesso.
    QString error;
    // 0 ok; 1 `get` sem o caminho; 2 uso inválido.
    int exitCode = 0;
};

// Núcleo PURO e offline de `kai kip`: formata mensagens KIP a partir de argv e
// extrai valores de uma resposta. `args` = tudo depois de "kip"
// (["hello", "--title", "Deploy"]). Não toca em IPC, processo, app ou config.
KipCliResult runKipCli(const QStringList &args);

// Texto de ajuda (traduzido) do helper.
QString kipCliHelpText();

// `kai kip get <json> <path>`: caminho com pontos ("values.opts.dry"; índices
// numéricos entram em arrays). Strings saem cruas, números/bool como literais
// JSON, arrays um item por linha, objetos como JSON compacto; caminho ausente
// = nada e exitCode 1.
KipCliResult kipCliGet(const QString &json, const QString &path);

} // namespace kai::core
