#pragma once

#include <QMap>
#include <QString>

namespace kai::engine {

// Os módulos `kai` e `kip` (Python, Node, PHP) vivem dentro do binário e o bootstrap de um comando de linguagem os
// registra na memória. Um script que roda como ARQUIVO (um .py do usuário chamado por um comando nativo com KIP,
// um subprocesso, um arquivo do ScriptSpill) não passa por esse bootstrap e não os encontraria. Por isso o Kai
// também os grava em disco, numa pasta SUA em <temp>/kai-run, e aponta os interpretadores para ela:
//
//   Python  PYTHONPATH        -> `import kip`, `import kai`
//   Node    NODE_PATH         -> `require('kip')`, `require('kai')`   (só CommonJS; `import` ESM ignora NODE_PATH)
//   PHP     PHP_INI_SCAN_DIR  -> um .ini com auto_prepend_file: a classe `Kip` já existe no script
//   todos   KAI_MODULES       -> a pasta, para quem quiser carregar por conta própria (require getenv(...))
//
// Há duas pastas: `kip/` com o módulo de verdade e `nokip/` com o stub que explica o que ligar (o mesmo que o
// bootstrap injeta num comando sem KIP). O nome da pasta leva o hash dos fontes: versão nova, pasta nova.
//
// A gravação é em SEGUNDO PLANO (ensureAsync na abertura do app; o pipeline espera por ela se um comando chegar
// antes). Os valores que o usuário já tem nas variáveis são preservados: a pasta do Kai entra na frente.
class ModuleFiles {
public:
    // A pasta do conjunto (`kip` ou `nokip`). Só calcula o caminho: não toca no disco. `root` vazio = <temp>/kai-run.
    static QString directory(bool withKip, const QString &root = QString());

    // Grava todos os arquivos agora (idempotente; arquivo já correto não é reescrito) e remove as pastas de versões
    // antigas. Devolve false se algo não pôde ser gravado. Usado pelo worker e pelos testes.
    static bool write(const QString &root = QString());

    // write() em segundo plano e registra que a pasta padrão está pronta.
    static void ensureAsync();
    // write() na pasta padrão, bloqueante (chamado de uma thread de trabalho) e registra que ela está pronta.
    static bool ensure();
    // A pasta padrão ainda não foi verificada, ou faz tempo: um limpador de /tmp pode ter levado os arquivos.
    static bool needsRefresh();

    // As variáveis a exportar para o processo. `current` traz os valores que o processo já teria de PYTHONPATH,
    // NODE_PATH e PHP_INI_SCAN_DIR (o do usuário no Kai ou o do ambiente do Kai): a pasta do Kai vai na frente.
    static QMap<QString, QString> environment(bool withKip, const QMap<QString, QString> &current,
                                              const QString &root = QString());
};

} // namespace kai::engine
