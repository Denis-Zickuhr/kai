#pragma once

#include <QString>
#include <QStringList>

namespace kai::cli {

// Verbos do CLI que falam com o app (sobem o Kai na bandeja se preciso).
// `args` = argv completo (args[0] = executável, args[1] = verbo).

// kai import [arquivo|pasta] — importa o kai.json/kai.yml/kai.yaml como
// projeto no app. Sem argumento, usa a pasta atual.
int runImportVerb(const QStringList &args);

// kai raise [--level info|warning|error] [--title "..."] <mensagem...> —
// notificação na bandeja do Kai (ex: fim de um script, falha no CI local).
int runRaiseVerb(const QStringList &args);

// kai attach <nome|pid> — acompanha ao vivo um processo que já roda no app.
int runAttachVerb(const QStringList &args);

// kai history [N] [--json] — últimas execuções registradas pelo app.
// kai last [--log] [--json] — a mais recente, com a saída (--log).
int runHistoryVerb(const QStringList &args);
int runLastVerb(const QStringList &args);

// Argumentos já lidos do `kai raise` (puro, testável).
struct RaiseRequest {
    QString level = QStringLiteral("info");
    QString title;
    QString message;
    QString error; // não vazio = uso inválido (mensagem já traduzida)
};
RaiseRequest parseRaiseArgs(const QStringList &argsAfterVerb);

// Arquivo de projeto a importar a partir do que foi passado (vazio = pasta
// atual): o próprio arquivo, ou o kai.json/kai.yml/kai.yaml da pasta. Vazio
// se não achou nada.
QString resolveImportTarget(const QString &argument, const QString &currentDir);

} // namespace kai::cli
