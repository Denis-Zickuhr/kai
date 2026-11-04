#include <QTest>
#include <QDialog>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>
#include <QComboBox>
#include <QMenu>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QCompleter>
#include <QAbstractItemView>

#include "ui/shared/dialog-utils.h"
#include "ui/app-stylesheet.h"
#include "ui/shared/combo-popup-filter.h"
#include "ui/shared/inline-code-field.h"
#include "ui/shared/collapsible-section-card.h"
#include <QPlainTextEdit>
#include <QFrame>
#include <QImage>
#include <QListWidget>
#include "ui/features/command-editor/command-editor-dialog.h"
#include "utils/theme-manager.h"
#include "utils/translation-manager.h"
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include "ui/shared/table-utils.h"
#include <QPushButton>
#include "utils/design-tokens.h"

using namespace kai::ui;

// centerOnParent: TODOS os diálogos centralizados em quem abriu; sem espaço, ajusta.
class TestDialogUtils : public QObject {
    Q_OBJECT

private slots:
    // Caso normal: fica centralizado exatamente sobre a geometria do pai.
    void centersOnParentWhenThereIsRoom()
    {
        QWidget parent;
        parent.setGeometry(100, 100, 400, 300);
        parent.show();

        QDialog dialog(&parent);
        dialog.resize(200, 100);
        centerOnParent(&dialog);

        const QRect parentGeom = parent.frameGeometry();
        QCOMPARE(dialog.pos(), QPoint(parentGeom.center().x() - 100, parentGeom.center().y() - 50));
    }

    // Diálogo maior que a janela pai, mas ainda cabendo na tela — continua
    // centralizado (só clampado pela TELA, não pela janela pai).
    void staysWithinScreenWhenParentIsNearScreenEdge()
    {
        QWidget parent;
        // Pai encostado no canto: sem ajuste o diálogo iria pra fora da tela (x negativo).
        parent.setGeometry(0, 0, 100, 100);
        parent.show();

        QDialog dialog(&parent);
        dialog.resize(300, 300);
        centerOnParent(&dialog);

        QScreen *screen = parent.screen();
        QVERIFY(screen != nullptr);
        const QRect avail = screen->availableGeometry();
        const QRect finalGeom(dialog.pos(), dialog.size());
        QVERIFY(finalGeom.left() >= avail.left());
        QVERIFY(finalGeom.top() >= avail.top());
        QVERIFY(finalGeom.right() <= avail.right() + 1);
        QVERIFY(finalGeom.bottom() <= avail.bottom() + 1);
    }

    // Sem pai: centraliza na própria tela do widget.
    void centersOnOwnScreenWhenThereIsNoParent()
    {
        QDialog dialog;
        dialog.resize(200, 150);
        centerOnParent(&dialog);

        QScreen *screen = dialog.screen();
        QVERIFY(screen != nullptr);
        const QRect avail = screen->availableGeometry();
        const QRect finalGeom(dialog.pos(), dialog.size());
        QVERIFY(finalGeom.left() >= avail.left());
        QVERIFY(finalGeom.top() >= avail.top());
    }

    // nullptr não deve crashar.
    void nullDialogIsNoOp()
    {
        centerOnParent(nullptr);
    }

    // O popup do QCompleter de um combo pesquisável (Pasta, Coleção...) tem QSS à parte e precisa do mesmo estado ":hover"
    // da regra "QComboBox QAbstractItemView" de buildModernStylesheet() (select comum).
    void makeSearchableComboPopupHasHoverStateLikeRegularComboPopup()
    {
        QComboBox combo;
        combo.addItem(QStringLiteral("a"));
        combo.addItem(QStringLiteral("b"));
        makeSearchableCombo(&combo);

        QCompleter *completer = combo.completer();
        QVERIFY(completer != nullptr);
        QAbstractItemView *popup = completer->popup();
        QVERIFY(popup != nullptr);
        QVERIFY2(popup->styleSheet().contains(QStringLiteral("item:hover")),
                 "popup do combo pesquisável não tem regra de hover, igual ao select comum");
    }

    // Campos (QLineEdit/QComboBox/...) não têm gradiente do tema; ele fica só em fundos e botões de ação.
    void inputFieldsNeverGetThemeGradient()
    {
        kai::utils::tokens::publishTheme({
            {QStringLiteral("gradient_primary_start"), QStringLiteral("#101010")},
            {QStringLiteral("gradient_primary_end"), QStringLiteral("#202020")},
            {QStringLiteral("gradient_secondary_start"), QStringLiteral("#303030")},
            {QStringLiteral("gradient_secondary_end"), QStringLiteral("#404040")},
            {QStringLiteral("gradient_tertiary_start"), QStringLiteral("#505050")},
            {QStringLiteral("gradient_tertiary_end"), QStringLiteral("#606060")},
        });
        kai::utils::tokens::setGradientsEnabled(true);
        QVERIFY(kai::utils::tokens::hasGradient(QStringLiteral("secondary")));

        const QString qss = buildModernStylesheet();
        QVERIFY(qss.contains(QStringLiteral("qlineargradient")));
        const QStringList rules = qss.split(QLatin1Char('}'));
        for (const QString &rule : rules) {
            const QString selector = rule.section(QLatin1Char('{'), 0, 0);
            if (!rule.contains(QStringLiteral("qlineargradient"))) {
                continue;
            }
            for (const QString &field : {QStringLiteral("QLineEdit"), QStringLiteral("QComboBox"),
                                         QStringLiteral("QPlainTextEdit"), QStringLiteral("QTextEdit"),
                                         QStringLiteral("QSpinBox"), QStringLiteral("QPushButton")}) {
                QVERIFY2(!selector.contains(field), qPrintable(rule));
            }
        }
        kai::utils::tokens::publishTheme({});
    }

    // Campo de comando/body deve ter o fundo dos demais campos (bg), não o
    // fundo mais escuro de editor de código.
    void inlineCodeFieldUsesRegularFieldBackground()
    {
        InlineCodeField field;
        const QString qss = field.editor()->styleSheet();
        QVERIFY2(qss.contains(kai::utils::tokens::bg()), qPrintable(qss));
        QVERIFY2(!qss.contains(kai::utils::tokens::codeBg()), qPrintable(qss));
    }

    void cardActionButtonIsSolidEvenWithThemeGradient()
    {
        kai::utils::tokens::publishTheme({
            {QStringLiteral("gradient_tertiary_start"), QStringLiteral("#505050")},
            {QStringLiteral("gradient_tertiary_end"), QStringLiteral("#606060")},
        });
        kai::utils::tokens::setGradientsEnabled(true);
        CollapsibleSectionCard card(QStringLiteral("t"));
        card.setActionButtonText(QStringLiteral("add"));
        const auto buttons = card.findChildren<QPushButton *>();
        QVERIFY(!buttons.isEmpty());
        for (QPushButton *b : buttons) {
            QVERIFY2(!b->styleSheet().contains(QStringLiteral("gradient")), qPrintable(b->styleSheet()));
        }
        kai::utils::tokens::publishTheme({});
    }

    // Hover dos botões de confirmação: perceptível no neutro, e o foco não cobre o hover do primário.
    void dialogButtonBoxStatesAreDistinctAndHoverBeatsFocus()
    {
        kai::utils::tokens::publishTheme({});
        const QString qss = buildModernStylesheet();
        const auto ruleBody = [&qss](const QString &selector) {
            const int i = qss.indexOf(QLatin1Char('\n') + selector + QStringLiteral(" {"));
            if (i < 0) {
                return QString();
            }
            const int open = qss.indexOf(QLatin1Char('{'), i);
            return qss.mid(open, qss.indexOf(QLatin1Char('}'), open) - open);
        };
        const auto bg = [](const QString &body) {
            const int i = body.indexOf(QStringLiteral("background-color:"));
            return i < 0 ? QString() : body.mid(i, body.indexOf(QLatin1Char(';'), i) - i);
        };
        const QString base = QStringLiteral("QDialogButtonBox QPushButton");
        QVERIFY(!ruleBody(base).isEmpty());
        QVERIFY(!ruleBody(base + QStringLiteral(":hover")).isEmpty());
        QVERIFY(bg(ruleBody(base)) != bg(ruleBody(base + QStringLiteral(":hover"))));
        QVERIFY(bg(ruleBody(base + QStringLiteral(":hover"))) != bg(ruleBody(base + QStringLiteral(":pressed"))));

        const QString pri = QStringLiteral("QDialogButtonBox QPushButton[kaiRole=\"primary\"]");
        const QString priHover = pri + QStringLiteral(":hover, QDialogButtonBox QPushButton:default:hover");
        const QString priBase = pri + QStringLiteral(", QDialogButtonBox QPushButton:default");
        QVERIFY(bg(ruleBody(priHover)) != bg(ruleBody(priBase)));
        // Foco antes do hover, senão o foco (Salvar nasce focado) engole o hover.
        const QString priFocus = pri + QStringLiteral(":focus, QDialogButtonBox QPushButton:default:focus");
        QVERIFY(qss.indexOf(priFocus) < qss.indexOf(priHover));
        QVERIFY(bg(ruleBody(priFocus)).isEmpty());
    }

    // O popup de QComboBox comum não tem retângulo branco/quadrado atrás da lista arredondada.
    void comboPopupWindowIsTranslucentAndFrameless()
    {
        auto *filter = new kai::ui::ComboPopupFilter(qApp);
        qApp->installEventFilter(filter);
        QWidget host;
        QComboBox combo(&host);
        combo.addItem(QStringLiteral("Projeto"));
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QWidget *popup = combo.view()->window();
        QVERIFY(popup != &host);
        QVERIFY(popup->testAttribute(Qt::WA_TranslucentBackground));
        QVERIFY(popup->windowFlags() & Qt::FramelessWindowHint);
        QVERIFY(popup->windowFlags() & Qt::Popup);
        qApp->removeEventFilter(filter);
        delete filter;

        QVERIFY(buildModernStylesheet().contains(QStringLiteral("QComboBoxPrivateContainer")));
    }

    // O corpo escuro das tabelas não pode vazar pelos cantos arredondados do card: canto de baixo arredondado
    // na preferência arredondada e reto na "reta".
    void tableBodyFollowsTheCornerPreference()
    {
        const auto saved = kai::utils::tokens::effects();
        for (const int cornerStyle : {0, 2}) {
            auto fx = saved;
            fx.cornerStyle = cornerStyle;
            kai::utils::tokens::setEffects(fx);

            QWidget host;
            host.setFixedSize(460, 380);
            host.setStyleSheet(kai::ui::buildModernStylesheet());
            auto *layout = new QVBoxLayout(&host);
            auto *card = new QFrame(&host);
            card->setObjectName(QStringLiteral("testCard"));
            card->setStyleSheet(QStringLiteral("QFrame#testCard { background: %1; border: 1px solid %2; border-radius: %3px; }")
                .arg(kai::utils::tokens::surface(), kai::utils::tokens::borderColor())
                .arg(kai::utils::tokens::radiusLg()));
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(10, 10, 10, 10);
            auto *table = new QTableWidget(2, 2, card);
            kai::ui::configureTable(table, {{QStringLiteral("A"), 120, false}, {QStringLiteral("B"), 120, true}});
            cardLayout->addWidget(table);
            layout->addWidget(card);
            host.show();
            QVERIFY(QTest::qWaitForWindowExposed(&host));
            QTest::qWait(60);

            const QImage image = host.grab().toImage();
            const QPoint origin = table->mapTo(&host, QPoint(0, 0));
            const QColor corner = image.pixelColor(origin + QPoint(1, table->height() - 2));
            const QColor body = image.pixelColor(origin + QPoint(table->width() / 2, table->height() - 14));
            if (cornerStyle == 0) {
                QCOMPARE(corner, body); // reta: o corpo vai até o canto
            } else {
                QVERIFY2(corner != body, "o canto da tabela tem que mostrar o card (arredondado), não o corpo escuro");
            }
        }
        kai::utils::tokens::setEffects(saved);
    }

    // Com o viewport transparente, a trilha da barra de rolagem da lista de hooks deixava ver o fundo da janela (faixa PRETA):
    // ela tem que ter a cor do corpo da lista. Testa no diálogo real (com o tema).
    void hooksListScrollBarTrackHasTheBodyColor()
    {
        kai::utils::ThemeManager themes;
        QVERIFY(themes.loadThemeFromFile(QStringLiteral("assets/themes/kai-dark.json")));
        kai::core::Folder folder;
        folder.id = QStringLiteral("f_a");
        folder.name = QStringLiteral("A");
        QVector<kai::core::Command> commands;
        for (int i = 0; i < 40; ++i) {
            kai::core::Command c;
            c.id = QStringLiteral("c%1").arg(i);
            c.name = QStringLiteral("Comando %1").arg(i);
            c.folderId = folder.id;
            commands << c;
        }
        kai::ui::CommandEditorDialog dialog(folder.id, commands, {folder}, nullptr);
        dialog.setStyleSheet(kai::ui::themeStyleSheet(themes.currentTheme().qss));
        dialog.resize(900, 700);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        for (auto *nav : dialog.findChildren<QListWidget *>()) {
            for (int i = 0; i < nav->count(); ++i) {
                if (nav->item(i)->text() == kai::utils::tr(QStringLiteral("command.tab.hooks"))) nav->setCurrentRow(i);
            }
        }
        QTest::qWait(200);

        QListWidget *list = nullptr;
        for (auto *l : dialog.findChildren<QListWidget *>()) {
            if (l->isVisible() && l->property("kaiRole").toString() == QStringLiteral("insetList")) list = l;
        }
        QVERIFY(list);
        QVERIFY(list->verticalScrollBar()->isVisible());
        const QImage image = dialog.grab().toImage();
        const QPoint origin = list->mapTo(&dialog, QPoint(0, 0));
        const QScrollBar *bar = list->verticalScrollBar();
        const QPoint barOrigin = bar->mapTo(&dialog, QPoint(0, 0));
        const QColor track = image.pixelColor(barOrigin + QPoint(bar->width() / 2, bar->height() - 8));
        const QColor body = image.pixelColor(origin + QPoint(list->width() - 60, list->height() / 2));
        QCOMPARE(track.name(), body.name());
    }

    // centerOnParent() não pode usar o tamanho do diálogo ANTES do layout (ele encolhia/crescia ao aparecer):
    // centraliza com o tamanho FINAL, na hora de mostrar.
    void dialogIsCenteredOnItsParentWithItsFinalSize()
    {
        QWidget host;
        host.setGeometry(40, 40, 640, 420);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));

        QDialog dialog(&host);
        auto *layout = new QVBoxLayout(&dialog);
        auto *filler = new QWidget(&dialog);
        filler->setFixedSize(300, 120); // o conteúdo manda: o diálogo fica bem menor que o tamanho padrão
        layout->addWidget(filler);
        kai::ui::centerOnParent(&dialog); // como os diálogos fazem: no construtor, antes de mostrar

        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::qWait(60);
        const QPoint parentCenter = host.frameGeometry().center();
        const QPoint dialogCenter = dialog.frameGeometry().center();
        QVERIFY2(qAbs(dialogCenter.x() - parentCenter.x()) <= 2 && qAbs(dialogCenter.y() - parentCenter.y()) <= 2,
                 qPrintable(QStringLiteral("dialog center (%1,%2) vs parent center (%3,%4)")
                                .arg(dialogCenter.x()).arg(dialogCenter.y()).arg(parentCenter.x()).arg(parentCenter.y())));
    }

    // O diálogo abre sobre a janela do Kai que o usuário está USANDO (a ativa), não sobre a janela pai: com a saída
    // destacada ou a janela do KIP em outra tela, o pai é a janela principal, que fica na tela errada.
    void dialogOpensOverTheActiveKaiWindowNotOnlyItsParent()
    {
        QWidget mainWindow;
        mainWindow.setGeometry(0, 0, 300, 200);
        mainWindow.show();
        QWidget otherKaiWindow; // ex.: a saída destacada
        otherKaiWindow.setGeometry(400, 300, 300, 200);
        otherKaiWindow.show();
        QDialog dialog(&mainWindow);
        dialog.resize(200, 100);

        // Com a outra janela ativa, é ela a referência.
        QCOMPARE(centerReferenceFor(&dialog, &otherKaiWindow), &otherKaiWindow);
        moveCenteredOn(&dialog, centerReferenceFor(&dialog, &otherKaiWindow));
        QVERIFY(qAbs(dialog.frameGeometry().center().x() - otherKaiWindow.frameGeometry().center().x()) <= 1);
        QVERIFY(qAbs(dialog.frameGeometry().center().y() - otherKaiWindow.frameGeometry().center().y()) <= 1);

        // Sem janela ativa (Kai na bandeja, atalho global), vale a janela do pai.
        QCOMPARE(centerReferenceFor(&dialog, nullptr), &mainWindow);
        // O próprio diálogo ativo (já mostrado) nunca é a referência dele mesmo.
        QCOMPARE(centerReferenceFor(&dialog, &dialog), &mainWindow);
        // Uma janela ativa escondida ou minimizada não serve.
        otherKaiWindow.hide();
        QCOMPARE(centerReferenceFor(&dialog, &otherKaiWindow), &mainWindow);
        // Sem pai e sem janela ativa: não há referência (o centro cai na tela do cursor).
        QDialog orphan;
        QVERIFY(centerReferenceFor(&orphan, nullptr) == nullptr);
    }

    // O QSS arredonda o menu popup; sem janela translúcida sobraria um retângulo quadrado atrás, ignorando os cantos.
    void menuPopupWindowIsTranslucentAndFrameless()
    {
        auto *filter = new kai::ui::ComboPopupFilter(qApp);
        qApp->installEventFilter(filter);
        QMenu menu;
        menu.addAction(QStringLiteral("Item"));
        menu.ensurePolished();
        QVERIFY(menu.testAttribute(Qt::WA_TranslucentBackground));
        QVERIFY(menu.windowFlags() & Qt::FramelessWindowHint);
        QVERIFY(menu.windowFlags() & Qt::NoDropShadowWindowHint);
        QVERIFY(menu.windowFlags() & Qt::Popup);
        qApp->removeEventFilter(filter);
        delete filter;
    }

    // Selects comuns abrem a lista abaixo do campo (não em "popup" sobre o campo, com moldura branca).
    void comboBoxesOpenAsDropdownBelowTheField()
    {
        QWidget host;
        host.setStyleSheet(buildModernStylesheet());
        auto *plain = new QComboBox(&host);
        plain->addItem(QStringLiteral("a"));
        auto *editable = new QComboBox(&host);
        editable->setEditable(true);
        for (QComboBox *combo : {plain, editable}) {
            combo->ensurePolished();
            QStyleOptionComboBox opt;
            opt.initFrom(combo);
            opt.editable = combo->isEditable();
            QCOMPARE(combo->style()->styleHint(QStyle::SH_ComboBox_Popup, &opt, combo), 0);
        }
    }
};

QTEST_MAIN(TestDialogUtils)
#include "test_dialog_utils.moc"
