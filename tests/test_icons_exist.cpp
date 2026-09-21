// Todo ícone Lucide referenciado por nome literal no código precisa estar
// embutido no resource (assets/icons/icons.qrc). Um nome sem SVG vira um ícone
// nulo e some da tela sem erro nenhum — foi o caso do ícone da dica na tela de
// boas-vindas ("lightbulb").

#include <QTest>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include "ui/shared/lucide-icons.h"

using namespace kai::ui;

class TestIconsExist : public QObject {
    Q_OBJECT

private slots:
    void everyLiteralLucideIconNameIsEmbedded()
    {
        const QRegularExpression call(QStringLiteral(
            "LucideIcons::icon\\(\\s*QStringLiteral\\(\"([a-z0-9-]+)\"\\)"));
        QSet<QString> missing;
        int checked = 0;
        QDirIterator it(QStringLiteral("src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString text = QString::fromUtf8(file.readAll());
            auto matches = call.globalMatch(text);
            while (matches.hasNext()) {
                const QString name = matches.next().captured(1);
                ++checked;
                if (!LucideIcons::has(name)) {
                    missing.insert(QStringLiteral("%1 (%2)").arg(name, path));
                }
            }
        }
        QVERIFY2(checked > 20, "a varredura não encontrou chamadas — o teste está rodando fora da raiz do repo?");
        QVERIFY2(missing.isEmpty(), qPrintable(QStringList(missing.begin(), missing.end()).join(QStringLiteral(", "))));
    }

    // Nenhum ícone com cor RGB cravada na chamada: cores vêm dos tokens do tema
    // (mutedFg/accent...). Um QColor(139, 233, 253) literal (o ciano do Dracula)
    // não acompanha a troca de tema. Cores SEMÂNTICAS (play verde, parar
    // amarelo...) ficam em variáveis nomeadas, fora desta regra.
    void noIconUsesAHardcodedRgbColor()
    {
        const QRegularExpression call(QStringLiteral(
            "LucideIcons::icon\\([^;]*?QColor\\(\\s*\\d+\\s*,"));
        QStringList offenders;
        QDirIterator it(QStringLiteral("src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            if (call.match(QString::fromUtf8(file.readAll())).hasMatch()) {
                offenders << path;
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QStringLiteral(", "))));
    }
};

QTEST_MAIN(TestIconsExist)
#include "test_icons_exist.moc"
