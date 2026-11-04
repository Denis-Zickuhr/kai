#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

#include "ui/app-stylesheet.h"
#include "utils/design-tokens.h"

// AGENTS.md §9: todo canto arredondado vem dos tokens de raio (a preferência de cantos do usuário). Um `border-radius: 8px`
// escrito à mão faz o elemento ignorar a preferência. Este teste varre o código e falha com o arquivo e a linha.
//
// Passa: raio 0 (cantos retos, onde um cartão cola no outro) e as exceções abaixo, que são formas geométricas e não cantos
// de moldura (um círculo tem raio = metade do lado por definição).
class TestStyleGuard : public QObject {
    Q_OBJECT

private slots:
    void noFixedCornerRadiusOutsideTheGeometricExceptions()
    {
        // arquivo -> valores permitidos (px).
        const QHash<QString, QSet<int>> allowed = {
            {QStringLiteral("src/ui/features/ai/ai-chat-window.cpp"), {17, 18}},           // botões redondos (34/36 px)
            {QStringLiteral("src/ui/features/settings/tabs/layout-tab.cpp"), {2, 7}},      // trilho e cabeça do slider
            {QStringLiteral("src/ui/shared/action-group-container.cpp"), {2}},             // cabeça da barra de rolagem fina
            {QStringLiteral("src/ui/app-stylesheet.cpp"), {10}},                           // indicador do interruptor (20 px)
            {QStringLiteral("src/ui/shared/help-content.cpp"), {4, 6}},                    // HTML do leitor de ajuda
        };
        const QRegularExpression radius(QStringLiteral(R"(border(?:-(?:top|bottom)-(?:left|right))?-radius:\s*(\d+)px)"));

        QStringList offenders;
        int scanned = 0;
        QDirIterator it(QStringLiteral("src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            ++scanned;
            const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                QRegularExpressionMatchIterator matches = radius.globalMatch(lines.at(i));
                while (matches.hasNext()) {
                    const int value = matches.next().captured(1).toInt();
                    if (value == 0 || allowed.value(QDir::cleanPath(path)).contains(value)) {
                        continue;
                    }
                    offenders << QStringLiteral("%1:%2 (%3px)").arg(path).arg(i + 1).arg(value);
                }
            }
        }
        QVERIFY2(scanned > 100, "o teste precisa rodar na raiz do repositório");
        QVERIFY2(offenders.isEmpty(), qPrintable(QStringLiteral("raio fixo (use utils::tokens::radius*):\n") + offenders.join(QLatin1Char('\n'))));
    }

    // O QSS do tema (núcleo) sai com marcadores no lugar dos raios; a interface põe o raio da preferência de cantos do usuário.
    void theThemeSheetTakesItsRadiiFromTheCornerPreference()
    {
        namespace tk = kai::utils::tokens;
        const tk::Effects saved = tk::effects();
        QSet<QString> seen;
        for (int corner : {0, 1, 2}) {
            tk::Effects fx = saved;
            fx.cornerStyle = corner;
            tk::setEffects(fx);
            const QString qss = kai::ui::themeStyleSheet(QStringLiteral("QPushButton { border-radius: @RADIUS_MD@; } QMenu::item { border-radius: @RADIUS_SM@; }"));
            QVERIFY2(!qss.contains(QLatin1Char('@')), qPrintable(qss.left(200)));
            QVERIFY(qss.contains(QStringLiteral("QPushButton { border-radius: %1px; }").arg(tk::radiusMd())));
            QVERIFY(qss.contains(QStringLiteral("QMenu::item { border-radius: %1px; }").arg(tk::radiusSm())));
            seen.insert(QString::number(tk::radiusMd()));
        }
        tk::setEffects(saved);
        QVERIFY2(seen.size() > 1, "retos, suaves e arredondados deveriam dar raios diferentes");
    }
};

QTEST_APPLESS_MAIN(TestStyleGuard)
#include "test_style_guard.moc"
