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
    const bool slashComments = language == core::CommandLanguage::Node || language == core::CommandLanguage::Php;
    QString text = source;
    if (python && text.startsWith(QLatin1String("\"\"\""))) {
        const int end = text.indexOf(QLatin1String("\"\"\""), 3);
        if (end > 0) {
            text.remove(0, end + 3);
        }
    }
    const QString marker = slashComments ? QStringLiteral("//") : QStringLiteral("#");
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
                                  "(command editor, KIP tab: KIP interface; or kip: true in kai.yml).";

QString kipDisabledStub(core::CommandLanguage language)
{
    if (language == core::CommandLanguage::Python) {
        return QStringLiteral("def __getattr__(name):\n"
                              "    if name.startswith('__'):\n"
                              "        raise AttributeError(name)\n"
                              "    raise RuntimeError('%1')\n").arg(QLatin1String(kKipDisabledMessage));
    }
    if (language == core::CommandLanguage::Php) {
        return QStringLiteral("<?php\n"
                              "if (!class_exists('Kip', false)) {\n"
                              "class Kip {\n"
                              "public static function __callStatic($name, $arguments) {\n"
                              "throw new RuntimeException('%1');\n"
                              "}\n}\n}\n").arg(QLatin1String(kKipDisabledMessage));
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

QString readResource(const QString &path)
{
    ensureKipModulesInitialized();
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// CRC-32 (o do gzip). O gzip -dc do shell confere o final do arquivo.
quint32 crc32Of(const QByteArray &bytes)
{
    static const QVector<quint32> table = []() {
        QVector<quint32> t(256);
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[static_cast<int>(n)] = c;
        }
        return t;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : bytes) {
        crc = table[(crc ^ static_cast<quint8>(byte)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// deflate cru: o que sobra do zlib do qCompress sem o tamanho (4 bytes), o cabeçalho (2) e o adler32 (4).
QByteArray rawDeflate(const QByteArray &bytes)
{
    QByteArray z = qCompress(bytes, 9);
    return z.mid(6, z.size() - 10);
}

QByteArray gzipBytes(const QByteArray &bytes)
{
    QByteArray out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
    out += rawDeflate(bytes);
    auto le32 = [](quint32 v) {
        QByteArray b(4, 0);
        for (int i = 0; i < 4; ++i) b[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
        return b;
    };
    out += le32(crc32Of(bytes));
    out += le32(static_cast<quint32>(bytes.size()));
    return out;
}

// O helper `kip` do shell POSIX: kip.sh com o programa awk (kip.awk) no lugar de @@AWK@@.
QString shellPrelude()
{
    const auto lang = core::CommandLanguage::Native;
    QString shell = slim(readResource(QStringLiteral(":/kip/kip.sh")), lang);
    return shell.replace(QStringLiteral("@@AWK@@"), slim(readResource(QStringLiteral(":/kip/kip.awk")), lang));
}

// O programa Python que registra os módulos e roda o código. Sem aspas duplas (vai entre aspas duplas na linha)
// e sem $ ` \ !, que o shell trataria. O código roda num dict de globais próprio, com __name__ == "__main__",
// para não enxergar os nomes do bootstrap. Serve igual como argumento de -c ou como arquivo .py.
QString pythonBootstrap(const QString &code, bool withKip)
{
    const QVector<InjectedModule> modules = injectedModules(core::CommandLanguage::Python, withKip);
    QStringList names;
    for (const InjectedModule &module : modules) {
        names << QStringLiteral("'%1'").arg(module.name);
    }
    return QStringLiteral(
        "import base64,sys,types,zlib;"
        "p=zlib.decompress(base64.b64decode('%1')).decode().split(chr(0));"
        "i=lambda n,s:(lambda m:(exec(s,m.__dict__),sys.modules.__setitem__(n,m)))(types.ModuleType(n));"
        "[i(n,s) for n,s in zip((%2,),p[:-1])];"
        "exec(compile(p[-1],'<kai>','exec'),{'__name__':'__main__'})")
        .arg(packPayload(modules, code), names.join(QLatin1Char(',')));
}

QString pythonCommandLine(const QString &interpreter, const QString &code, bool withKip)
{
    return QStringLiteral("%1 -u -c \"%2\"").arg(interpreter, pythonBootstrap(code, withKip));
}

QString nodeBootstrap(const QString &code, bool withKip)
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
    return QStringLiteral(
        "const __kp=require('zlib').inflateSync(Buffer.from('%1','base64')).toString().split(String.fromCharCode(0)),"
        "__kr={},__km=require('module'),__kl=__km._load;"
        "__km._load=function(r,...a){return r in __kr?__kr[r]:__kl.call(this,r,...a)};"
        "[%2].forEach((n,i)=>{const m={exports:{}};"
        "new Function('module','exports','require',__kp[i])(m,m.exports,require);"
        "__kr[n]=m.exports;globalThis[n]=m.exports});"
        "require('vm').runInThisContext('(async()=>{'+__kp[__kp.length-1]+'\\n})()',"
        "{filename:'kai-command.js'})")
        .arg(packPayload(modules, code), names.join(QLatin1Char(',')));
}

QString nodeCommandLine(const QString &interpreter, const QString &code, bool withKip)
{
    return QStringLiteral("%1 -e \"%2\"").arg(interpreter, nodeBootstrap(code, withKip));
}

// Um bloco PHP que o `eval` aceita: sem a abertura `<?php`.
QString phpEvalText(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1String("<?php"))) {
        text = text.mid(5).trimmed();
    }
    return text;
}

// PHP: o código e o helper `Kip` vão comprimidos + base64 (só base64 e pontuação sem significado para o shell). São
// DOIS eval, cada um sua unidade de compilação: um `namespace` ou `declare(strict_types=1)` no começo do código do
// usuário continua válido. Um `<?php` inicial é aceito e ignorado, como num arquivo.
QString phpBootstrap(const QString &code, bool withKip)
{
    const QString helper = withKip ? slim(kipModuleSource(core::CommandLanguage::Php), core::CommandLanguage::Php)
                                   : kipDisabledStub(core::CommandLanguage::Php);
    return QStringLiteral("eval(gzuncompress(base64_decode('%1')));eval(gzuncompress(base64_decode('%2')));")
        .arg(packBytes(phpEvalText(helper).toUtf8()), packBytes(phpEvalText(code).toUtf8()));
}

QString phpCommandLine(const QString &interpreter, const QString &code, bool withKip)
{
    return QStringLiteral("%1 -r \"%2\"").arg(interpreter, phpBootstrap(code, withKip));
}

} // namespace

QString kipModuleSource(core::CommandLanguage language)
{
    switch (language) {
    case core::CommandLanguage::Python: return readResource(QStringLiteral(":/kip/kip.py"));
    case core::CommandLanguage::Node:   return readResource(QStringLiteral(":/kip/kip.js"));
    case core::CommandLanguage::Native: return shellPrelude();
    case core::CommandLanguage::Php:    return readResource(QStringLiteral(":/kip/kip.php"));
    }
    return QString();
}

QString kipDisabledModuleSource(core::CommandLanguage language)
{
    return language == core::CommandLanguage::Native ? QString() : kipDisabledStub(language);
}

QString kaiModuleSource(core::CommandLanguage language)
{
    // O shell tem a CLI `kai` de sempre e o PHP não recebe o módulo `kai`: ele só existe em Python/Node.
    switch (language) {
    case core::CommandLanguage::Python: return readResource(QStringLiteral(":/kip/kai.py"));
    case core::CommandLanguage::Node:   return readResource(QStringLiteral(":/kip/kai.js"));
    default: break;
    }
    return QString();
}

// O helper `kip` em texto puro (para ir dentro de um arquivo, ver ScriptSpill).
QString kipShellPrelude()
{
    return shellPrelude();
}

// O helper `kip` é gzip + base64 na própria linha, decodificado pelo PRÓPRIO shell que a roda (printf/base64/gzip
// existem em qualquer ambiente POSIX) e carregado com eval: define a função `kip` no shell do comando, sem quotes.
QString kipShellLoader()
{
    return QStringLiteral("eval \"$(printf %s '%1' | { base64 -d 2>/dev/null || base64 -D; } | gzip -dc)\"")
        .arg(QString::fromLatin1(gzipBytes(shellPrelude().toUtf8()).toBase64()));
}

QString buildInterpreterCommandLine(core::CommandLanguage language, const QString &interpreter,
                                    const QString &code, bool withKip)
{
    switch (language) {
        case core::CommandLanguage::Python: return pythonCommandLine(interpreter, code, withKip);
        case core::CommandLanguage::Node:   return nodeCommandLine(interpreter, code, withKip);
        case core::CommandLanguage::Php:    return phpCommandLine(interpreter, code, withKip);
        case core::CommandLanguage::Native: break;
    }
    return QString();
}

QString buildInterpreterScript(core::CommandLanguage language, const QString &code, bool withKip)
{
    switch (language) {
        case core::CommandLanguage::Python: return pythonBootstrap(code, withKip);
        case core::CommandLanguage::Node:   return nodeBootstrap(code, withKip);
        case core::CommandLanguage::Php:    return QStringLiteral("<?php\n") + phpBootstrap(code, withKip) + QLatin1Char('\n');
        case core::CommandLanguage::Native: break;
    }
    return QString();
}

QString interpreterScriptExtension(core::CommandLanguage language)
{
    switch (language) {
        case core::CommandLanguage::Python: return QStringLiteral("py");
        case core::CommandLanguage::Node:   return QStringLiteral("cjs"); // CommonJS mesmo sob um package.json "type": "module"
        case core::CommandLanguage::Php:    return QStringLiteral("php");
        case core::CommandLanguage::Native: break;
    }
    return QString();
}

QString buildInterpreterFileCommandLine(core::CommandLanguage language, const QString &interpreter,
                                        const QString &quotedPath)
{
    switch (language) {
        case core::CommandLanguage::Python: return QStringLiteral("%1 -u %2").arg(interpreter, quotedPath);
        case core::CommandLanguage::Node:
        case core::CommandLanguage::Php:    return QStringLiteral("%1 %2").arg(interpreter, quotedPath);
        case core::CommandLanguage::Native: break;
    }
    return QString();
}

QMap<QString, QString> languageEnvDefaults(core::CommandLanguage language)
{
    // Native e PHP não usam o módulo `kai`: nada a injetar no ambiente.
    if (language != core::CommandLanguage::Python && language != core::CommandLanguage::Node) {
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
