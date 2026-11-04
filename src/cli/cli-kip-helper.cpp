#include "cli/cli-kip-helper.h"

#include "core/kip-cli-builder.h"
#include "utils/translation-manager.h"

#include <QProcessEnvironment>

#include <cstdio>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

namespace kai::cli {

int runKipHelper(const QStringList &args)
{
#if defined(Q_OS_WIN)
    // Sem tradução de '\n' para '\r\n' no stdout/stderr.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    // Idioma das mensagens do próprio helper: o que o Kai injeta no ambiente
    // (KIP_LOCALE). Nunca a configuração do usuário — o helper não lê config.
    const QString locale = QProcessEnvironment::systemEnvironment().value(QStringLiteral("KIP_LOCALE")).trimmed();
    if (!locale.isEmpty()) {
        utils::TranslationManager::instance().loadLanguage(locale);
    }

    const core::KipCliResult result = core::runKipCli(args.mid(2));
    if (!result.output.isEmpty()) {
        std::fwrite(result.output.constData(), 1, static_cast<size_t>(result.output.size()), stdout);
        std::fflush(stdout);
    }
    if (!result.error.isEmpty()) {
        const QByteArray message = result.error.toUtf8() + '\n';
        std::fwrite(message.constData(), 1, static_cast<size_t>(message.size()), stderr);
        std::fflush(stderr);
    }
    return result.exitCode;
}

} // namespace kai::cli
