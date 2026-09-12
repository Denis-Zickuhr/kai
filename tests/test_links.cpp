#include <QTest>
#include <QApplication>
#include "ui/code-output-view.h"

using namespace kai::ui;

class TestLinks : public QObject {
    Q_OBJECT
private slots:
    void detectsUrlUnderCursor()
    {
        CodeOutputView view;
        view.setPlainText(QStringLiteral("Servidor em http://localhost:3000/api ok"));
        view.resize(800, 200);
        // Posiciona no meio da URL: procura a coluna do "localhost".
        const QString line = view.toPlainText();
        const int col = line.indexOf(QStringLiteral("localhost"));
        QVERIFY(col > 0);
        QTextCursor c(view.document());
        c.setPosition(col + 2);
        const QRect r = view.cursorRect(c);
        const QString url = view.urlAt(r.center());
        QCOMPARE(url, QStringLiteral("http://localhost:3000/api"));
    }

    void ignoresTrailingPunctuationAndPlainText()
    {
        CodeOutputView view;
        view.setPlainText(QStringLiteral("veja https://exemplo.com/x. fim"));
        view.resize(800, 200);
        const int col = view.toPlainText().indexOf(QStringLiteral("exemplo"));
        QTextCursor c(view.document());
        c.setPosition(col + 2);
        // o "." final NAO deve entrar na URL
        QCOMPARE(view.urlAt(view.cursorRect(c).center()), QStringLiteral("https://exemplo.com/x"));
        // sobre texto comum nao ha link
        QTextCursor c2(view.document());
        c2.setPosition(1);
        QVERIFY(view.urlAt(view.cursorRect(c2).center()).isEmpty());
    }
};

QTEST_MAIN(TestLinks)
#include "test_links.moc"
