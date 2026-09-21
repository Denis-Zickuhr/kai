#include "engine/command-language.h"

#include "core/ipc-endpoint.h"

#include <QByteArray>
#include <QFile>
#include <QString>

// O kip-modules.qrc vive numa biblioteca estática: o construtor gerado pelo
// rcc seria descartado pelo linker (mesma história do icons.qrc). Escopo
// global puro, senão o símbolo qInitResources_kip_modules não é achado.
static void ensureKipModulesInitialized()
{
    static const bool initialized = []() {
        Q_INIT_RESOURCE(kip_modules);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace kai::engine {

namespace {

// zlib cru + base64: o qCompress prefixa 4 bytes com o tamanho original, que
// o zlib.decompress (Python) / inflateSync (Node) não esperam.
QString packBytes(const QByteArray &bytes)
{
    QByteArray packed = qCompress(bytes, 9);
    packed.remove(0, 4);
    return QString::fromLatin1(packed.toBase64());
}

// Tira do módulo o que só serve para quem LÊ o fonte (comentários de linha inteira,
// linhas em branco e, no Python, a docstring do módulo): a linha de comando
// passa por `cmd.exe /c` (8191 caracteres) e, num alvo WSL, por um SEGUNDO base64.
// Só linhas inteiras de comentário caem — nunca texto no fim de uma linha de código.
QString slim(const QString &source, core::CommandLanguage language)
{
    const bool python = language == core::CommandLanguage::Python;
    QString text = source;
    if (python && text.startsWith(QLatin1String("\"\"\""))) {
        const int end = text.indexOf(QLatin1String("\"\"\""), 3);
        if (end > 0) {
            text.remove(0, end + 3);
        }
    }
    const QString marker = python ? QStringLiteral("#") : QStringLiteral("//");
    QStringList kept;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(marker)) {
            kept << line;
        }
    }
    return kept.join(QLatin1Char('\n'));
}

// Texto do aviso do `kip` quando o comando não é KIP (o que o autor precisa ligar).
const char *kKipDisabledMessage = "The kip module needs the KIP interface turned on for this command "
                                  "(command editor, KIP tab: KIP interface; or kip: true in kai.json).";

QString kipDisabledStub(core::CommandLanguage language)
{
    if (language == core::CommandLanguage::Python) {
        return QStringLiteral("def __getattr__(name):\n"
                              "    if name.startswith('__'):\n"
                              "        raise AttributeError(name)\n"
                              "    raise RuntimeError('%1')\n").arg(QLatin1String(kKipDisabledMessage));
    }
    return QStringLiteral("module.exports=new Proxy({},{get:(_,k)=>{"
                          "if(typeof k==='symbol'||k==='then'||k==='__esModule')return undefined;"
                          "throw new Error('%1')}});").arg(QLatin1String(kKipDisabledMessage));
}

// Os módulos que precedem o código, em ordem: `kai` sempre, `kip` só em comando KIP.
struct InjectedModule {
    QString name;
    QString source;
};
QVector<InjectedModule> injectedModules(core::CommandLanguage language, bool withKip)
{
    QVector<InjectedModule> modules{{QStringLiteral("kai"), slim(kaiModuleSource(language), language)}};
    if (withKip) {
        modules.push_back({QStringLiteral("kip"), slim(kipModuleSource(language), language)});
    } else {
        // Sem KIP ligado `import kip` não pode ser um "ModuleNotFoundError" misterioso:
        // o módulo existe e explica, ao ser usado, o que falta ligar.
        modules.push_back({QStringLiteral("kip"), kipDisabledStub(language)});
    }
    return modules;
}

// UM fluxo comprimido com [módulo..., código] separados por NUL: comprime melhor
// que cada parte sozinha e paga o base64 uma vez só.
QString packPayload(const QVector<InjectedModule> &modules, const QString &code)
{
    QByteArray joined;
    for (const InjectedModule &module : modules) {
        joined += module.source.toUtf8();
        joined += '\0';
    }
    joined += code.toUtf8();
    return packBytes(joined);
}

QString pythonCommandLine(const QString &interpreter, const QString &code, bool withKip)
{
    // Sem aspas duplas no corpo (vai entre aspas duplas) e sem $ ` \ !, que o
    // shell trataria. O código roda num dict de globais próprio, com
    // __name__ == "__main__", para não enxergar os nomes do bootstrap.
    const QVector<InjectedModule> modules = injectedModules(core::CommandLanguage::Python, withKip);
    QStringList names;
    for (const InjectedModule &module : modules) {
        names << QStringLiteral("'%1'").arg(module.name);
    }
    const QString body = QStringLiteral(
        "import base64,sys,types,zlib;"
        "p=zlib.decompress(base64.b64decode('%1')).decode().split(chr(0));"
        "i=lambda n,s:(lambda m:(exec(s,m.__dict__),sys.modules.__setitem__(n,m)))(types.ModuleType(n));"
        "[i(n,s) for n,s in zip((%2,),p[:-1])];"
        "exec(compile(p[-1],'<kai>','exec'),{'__name__':'__main__'})")
        .arg(packPayload(modules, code), names.join(QLatin1Char(',')));
    return QStringLiteral("%1 -u -c \"%2\"").arg(interpreter, body);
}

QString nodeCommandLine(const QString &interpreter, const QString &code, bool withKip)
{
    // O código vira o corpo de uma função async: `await` no topo funciona e
    // `return` encerra. O "\n" antes do fecho evita que um comentário `//` na
    // última linha engula o `})()`. Falha = rejeição não tratada = exit 1.
    // Os módulos viram globais E ficam acessíveis por require('kai'/'kip').
    const QVector<InjectedModule> modules = injectedModules(core::CommandLanguage::Node, withKip);
    QStringList names;
    for (const InjectedModule &module : modules) {
        names << QStringLiteral("'%1'").arg(module.name);
    }
    const QString body = QStringLiteral(
        "const __kp=require('zlib').inflateSync(Buffer.from('%1','base64')).toString().split(String.fromCharCode(0)),"
        "__kr={},__km=require('module'),__kl=__km._load;"
        "__km._load=function(r,...a){return r in __kr?__kr[r]:__kl.call(this,r,...a)};"
        "[%2].forEach((n,i)=>{const m={exports:{}};"
        "new Function('module','exports','require',__kp[i])(m,m.exports,require);"
        "__kr[n]=m.exports;globalThis[n]=m.exports});"
        "require('vm').runInThisContext('(async()=>{'+__kp[__kp.length-1]+'\\n})()',"
        "{filename:'kai-command.js'})")
        .arg(packPayload(modules, code), names.join(QLatin1Char(',')));
    return QStringLiteral("%1 -e \"%2\"").arg(interpreter, body);
}

} // namespace

QString kipModuleSource(core::CommandLanguage language)
{
    ensureKipModulesInitialized();
    const QString path = language == core::CommandLanguage::Python ? QStringLiteral(":/kip/kip.py")
                       : language == core::CommandLanguage::Node   ? QStringLiteral(":/kip/kip.js")
                                                                   : QString();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString kaiModuleSource(core::CommandLanguage language)
{
    ensureKipModulesInitialized();
    const QString path = language == core::CommandLanguage::Python ? QStringLiteral(":/kip/kai.py")
                       : language == core::CommandLanguage::Node   ? QStringLiteral(":/kip/kai.js")
                                                                   : QString();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString buildInterpreterCommandLine(core::CommandLanguage language, const QString &interpreter,
                                    const QString &code, bool withKip)
{
    switch (language) {
        case core::CommandLanguage::Python: return pythonCommandLine(interpreter, code, withKip);
        case core::CommandLanguage::Node:   return nodeCommandLine(interpreter, code, withKip);
        case core::CommandLanguage::Native: break;
    }
    return QString();
}

QMap<QString, QString> languageEnvDefaults(core::CommandLanguage language)
{
    if (language == core::CommandLanguage::Native) {
        return {};
    }
    // O módulo `kai` conecta neste socket (o fallback pela CLI não precisa dele).
    QMap<QString, QString> env{{QStringLiteral("KAI_IPC_SOCKET"), core::ipcEndpointPath()}};
    if (language == core::CommandLanguage::Python) {
        env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
        env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    }
    return env;
}

} // namespace kai::engine
