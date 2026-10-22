#include "utils/qt-builtin-translator.h"

#include "utils/translation-manager.h"

#include <QByteArray>
#include <QHash>

namespace kai::utils {

namespace {
// Texto-fonte do Qt -> chave do language pack.
const QHash<QByteArray, QString> &sourceToKey()
{
    static const QHash<QByteArray, QString> map = {
        // Menu de contexto dos campos de texto.
        {"&Undo", QStringLiteral("qt.menu.undo")},
        {"&Redo", QStringLiteral("qt.menu.redo")},
        {"Cu&t", QStringLiteral("qt.menu.cut")},
        {"&Copy", QStringLiteral("qt.menu.copy")},
        {"&Paste", QStringLiteral("qt.menu.paste")},
        {"Delete", QStringLiteral("qt.menu.delete")},
        {"Select All", QStringLiteral("qt.menu.select_all")},
        {"Copy &Link Location", QStringLiteral("qt.menu.copy_link")},
        {"&Step up", QStringLiteral("qt.menu.step_up")},
        {"Step &down", QStringLiteral("qt.menu.step_down")},
        {"&Select All", QStringLiteral("qt.menu.select_all")},
        // Botões padrão.
        {"OK", QStringLiteral("qt.button.ok")},
        {"Cancel", QStringLiteral("qt.button.cancel")},
        {"Close", QStringLiteral("qt.button.close")},
        {"Save", QStringLiteral("qt.button.save")},
        {"Open", QStringLiteral("qt.button.open")},
        {"&Yes", QStringLiteral("qt.button.yes")},
        {"&No", QStringLiteral("qt.button.no")},
        {"Yes to &All", QStringLiteral("qt.button.yes_all")},
        {"N&o to All", QStringLiteral("qt.button.no_all")},
        {"Abort", QStringLiteral("qt.button.abort")},
        {"Retry", QStringLiteral("qt.button.retry")},
        {"Ignore", QStringLiteral("qt.button.ignore")},
        {"Discard", QStringLiteral("qt.button.discard")},
        {"Don't Save", QStringLiteral("qt.button.dont_save")},
        {"Help", QStringLiteral("qt.button.help")},
        {"Apply", QStringLiteral("qt.button.apply")},
        {"Reset", QStringLiteral("qt.button.reset")},
        {"Restore Defaults", QStringLiteral("qt.button.restore_defaults")},
    };
    return map;
}
} // namespace

QString QtBuiltinTranslator::translate(const char *context, const char *sourceText, const char *disambiguation,
                                       int n) const
{
    Q_UNUSED(context)
    Q_UNUSED(disambiguation)
    Q_UNUSED(n)
    if (!sourceText) {
        return QString();
    }
    const auto it = sourceToKey().constFind(QByteArray(sourceText));
    if (it == sourceToKey().constEnd()) {
        return QString();
    }
    const QString text = TranslationManager::instance().translate(it.value());
    // Chave ausente volta como a própria chave: deixa o texto do Qt.
    return text == it.value() ? QString() : text;
}

} // namespace kai::utils
