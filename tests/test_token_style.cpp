#include <QtTest>

#include <QFrame>
#include <QPlainTextEdit>

#include "ui/features/output/logs-panel.h"
#include "ui/shared/shortcuts-manager-widget.h"
#include "ui/shared/token-style.h"
#include "utils/design-tokens.h"

using namespace kai;
namespace tk = utils::tokens;

// Estilos locais calculados dos tokens têm de seguir a preferência de cantos mesmo trocada com o app aberto
// (AGENTS.md §9): painéis criados antes da troca ficavam com o raio antigo até reiniciar.
class TestTokenStyle : public QObject {
    Q_OBJECT

private:
    tk::Effects m_saved;

    static void setCorners(int style)
    {
        tk::Effects fx = tk::effects();
        fx.cornerStyle = style;
        tk::setEffects(fx);
    }

private slots:
    void init() { m_saved = tk::effects(); }
    void cleanup() { tk::setEffects(m_saved); }

    void aBoundStyleIsRecomputedOnRefresh()
    {
        setCorners(0);
        QFrame frame;
        ui::bindTokenStyle(&frame, []() {
            return QStringLiteral("QFrame { border-radius: %1px; }").arg(tk::radiusMd());
        });
        QVERIFY(frame.styleSheet().contains(QStringLiteral("border-radius: 0px")));
        setCorners(2);
        ui::refreshTokenStyles();
        QVERIFY2(frame.styleSheet().contains(QStringLiteral("border-radius: %1px").arg(tk::radiusMd())),
                 qPrintable(frame.styleSheet()));
        QVERIFY(tk::radiusMd() > 0);
    }

    void aDestroyedWidgetIsForgotten()
    {
        {
            QFrame frame;
            ui::bindTokenStyle(&frame, []() { return QString(); });
        }
        ui::refreshTokenStyles(); // não pode tocar no widget destruído
    }

    void theLogsAreaFollowsTheCornerPreferenceChangedAtRuntime()
    {
        setCorners(2);
        ui::LogsPanel panel;
        auto *view = panel.findChild<QPlainTextEdit *>(QStringLiteral("logsView"));
        QVERIFY(view);
        QVERIFY(view->styleSheet().contains(QStringLiteral("border-radius: %1px").arg(tk::radiusMd())));
        setCorners(0);
        ui::refreshTokenStyles();
        QVERIFY2(view->styleSheet().contains(QStringLiteral("border-radius: 0px")), qPrintable(view->styleSheet()));
    }

    void theShortcutsTableBlocksFollowTheCornerPreference()
    {
        setCorners(2);
        ui::ShortcutsManagerWidget widget;
        widget.setShortcuts({});
        widget.resize(600, 400);
        widget.show();
        const auto cells = widget.findChildren<QWidget *>(QStringLiteral("shortcutCell"));
        const auto chips = widget.findChildren<QWidget *>(QStringLiteral("shortcutChip"));
        QVERIFY(!cells.isEmpty());
        QVERIFY(!chips.isEmpty());
        QVERIFY2(!cells.first()->styleSheet().contains(QStringLiteral("border-radius: 0px")),
                 qPrintable(cells.first()->styleSheet()));
        setCorners(0);
        ui::refreshTokenStyles();
        for (QWidget *w : cells + chips) {
            QVERIFY2(w->styleSheet().contains(QStringLiteral("border-radius: 0px")), qPrintable(w->styleSheet()));
        }
    }
};

QTEST_MAIN(TestTokenStyle)
#include "test_token_style.moc"
