#pragma once

#include <QString>
#include <QStringList>

namespace kai::cli {

// Verbos do CLI que falam com o app (sobem o Kai na bandeja se preciso).
// `args` = argv completo (args[0] = executável, args[1] = verbo).

// kai import [arquivo|pasta]: sem argumento, usa a pasta atual.
int runImportVerb(const QStringList &args);

// kai raise [--level info|warning|error] [--title "..."] <mensagem...>
int runRaiseVerb(const QStringList &args);

// kai attach <nome|pid>
int runAttachVerb(const QStringList &args);

// kai history [N] [--json]; kai last [--log] [--json]
int runHistoryVerb(const QStringList &args);
int runLastVerb(const QStringList &args);

// Parse puro (testável).
struct RaiseRequest {
    QString level = QStringLiteral("info");
    QString title;
    QString message;
    QString error; // não vazio = uso inválido (mensagem já traduzida)
};
RaiseRequest parseRaiseArgs(const QStringList &argsAfterVerb);

// Parse puro (testável) de `kai import [arquivo|pasta] [--only a,b]`.
struct ImportRequest {
    QString argument;   // o arquivo/pasta (vazio = pasta atual)
    QStringList only;   // categorias do pacote (vazio = tudo)
    bool recursive = false; // -r/--recursive: todos os projetos abaixo da pasta
    QString error;      // não vazio = uso inválido (mensagem já traduzida)
};
ImportRequest parseImportArgs(const QStringList &argsAfterVerb);

// Arquivo de projeto a importar: o próprio arquivo, ou o kai.yml da pasta (vazio = pasta atual).
// Vazio se não achou nada.
QString resolveImportTarget(const QString &argument, const QString &currentDir);

} // namespace kai::cli
