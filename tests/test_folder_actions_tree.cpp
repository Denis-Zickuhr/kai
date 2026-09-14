#include <QTest>

#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>

#include "core/folder-actions.h"
#include "ui/features/command-editor/command-tree-widget.h"
#include "ui/features/command-editor/folder-actions-view.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

using namespace kai::core;
using namespace kai::ui;

namespace {

constexpr int kCommandIdRole = Qt::UserRole + 1;

Command command(const QString &id, const QString &name, const QString &icon = QStringLiteral("terminal"))
{
    Command c;
    c.id = id;
    c.name = name;
    c.icon = icon;
    c.type = CommandType::Command;
    c.command = QStringLiteral("echo ") + name;
    c.folderId = QStringLiteral("tools");
    return c;
}

QTreeWidgetItem *findItem(QTreeWidget *tree, const QString &id)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, kCommandIdRole).toString() == id) {
            return *it;
        }
    }
    return nullptr;
}

} // namespace

// Ícones de ação na linha das subpastas: quais aparecem, onde, o clique que
// executa sem selecionar, o foco (e como ele se desfaz) e as cores.
class TestFolderActionsTree : public QObject {
    Q_OBJECT

private:
    struct Fixture {
        CommandTreeWidget widget;
        QTreeWidget *tree = nullptr;
        FolderActionsView *view = nullptr;
        QVector<Folder> folders;
        QVector<Command> commands;
    };

    // Raiz > "Repo A" (ações próprias: Pull, Fetch) / "Repo B" (nenhuma) / "Proj" (pasta-projeto).
    // Globais: Status (todas as pastas) e Git (só pastas-projeto).
    static void fill(Fixture &f)
    {
        Folder root;
        root.id = QStringLiteral("root");
        root.name = QStringLiteral("Root");
        Folder tools;
        tools.id = QStringLiteral("tools");
        tools.name = QStringLiteral("Tools");
        Folder repoA;
        repoA.id = QStringLiteral("repo_a");
        repoA.name = QStringLiteral("Repo A");
        repoA.parentId = QStringLiteral("root");
        repoA.actions = {QStringLiteral("c_pull"), QStringLiteral("c_fetch")};
        Folder repoB;
        repoB.id = QStringLiteral("repo_b");
        repoB.name = QStringLiteral("Repo B");
        repoB.parentId = QStringLiteral("root");
        Folder proj;
        proj.id = QStringLiteral("proj");
        proj.name = QStringLiteral("Proj");
        proj.parentId = QStringLiteral("root");
        proj.isProject = true;
        tools.parentId = QStringLiteral("root");
        f.folders = {root, tools, repoA, repoB, proj};
        f.commands = {command(QStringLiteral("c_pull"), QStringLiteral("Pull")),
                      command(QStringLiteral("c_fetch"), QStringLiteral("Fetch")),
                      command(QStringLiteral("c_status"), QStringLiteral("Status")),
                      command(QStringLiteral("c_git"), QStringLiteral("Git"))};
        f.widget.setData(f.folders, f.commands, {});
        f.widget.setGlobalActions({{QStringLiteral("c_status"), false}, {QStringLiteral("c_git"), true}});
        f.widget.resize(700, 500);
        f.widget.show();
        QTest::qWait(50);
        f.tree = f.widget.findChild<QTabWidget *>()->currentWidget()->findChild<QTreeWidget *>();
        if (!f.tree) {
            f.tree = qobject_cast<QTreeWidget *>(f.widget.findChild<QTabWidget *>()->currentWidget());
        }
        f.view = f.tree ? qobject_cast<FolderActionsView *>(f.tree->itemDelegateForColumn(FolderActionsView::kActionsColumn)) : nullptr;
    }

    // A célula da COLUNA DE AÇÕES (a última, depois do status/tempo) da linha da pasta.
    static QRect cellOf(Fixture &f, const QString &folderId)
    {
        QTreeWidgetItem *item = findItem(f.tree, folderId);
        QRect rect = f.tree->visualItemRect(item);
        rect.setLeft(f.tree->columnViewportPosition(FolderActionsView::kActionsColumn));
        rect.setWidth(f.tree->columnWidth(FolderActionsView::kActionsColumn));
        return rect;
    }

    // Nomes das ações de uma pasta, de verdade (mira o centro de cada slot).
    static QStringList namesOn(Fixture &f, const QString &folderId)
    {
        QStringList names;
        for (const QRect &rect : FolderActionsView::iconRects(cellOf(f, folderId), 8)) {
            const auto hit = f.view->hitTest(rect.center());
            if (hit.valid && !hit.expander && hit.group.isEmpty()) names << hit.icon.name;
        }
        return names;
    }

private slots:
    // Ao passar o mouse: olho quando o clique só MOSTRA a saída, play quando executa de novo.
    void hoverShowsAnEyeWhenTheClickOnlyShowsTheOutput_data()
    {
        QTest::addColumn<bool>("running");
        QTest::addColumn<bool>("focused");
        QTest::addColumn<bool>("hasOutput");
        QTest::addColumn<int>("glyph");
        using G = FolderActionsView::HoverGlyph;
        QTest::newRow("never ran")                    << false << false << false << int(G::None);
        QTest::newRow("ran, not loaded yet")          << false << false << true  << int(G::Eye);
        QTest::newRow("loaded and stopped: runs")     << false << true  << true  << int(G::Play);
        QTest::newRow("running, not focused")         << true  << false << true  << int(G::Eye);
        QTest::newRow("running and focused")          << true  << true  << true  << int(G::Eye);
        QTest::newRow("running, no saved output yet") << true  << false << false << int(G::Eye);
    }

    void hoverShowsAnEyeWhenTheClickOnlyShowsTheOutput()
    {
        QFETCH(bool, running);
        QFETCH(bool, focused);
        QFETCH(bool, hasOutput);
        QFETCH(int, glyph);
        QCOMPARE(int(FolderActionsView::hoverGlyph(running, focused, hasOutput)), glyph);
    }

    void geometryFillsTheColumnLeftToRightCenteredOnTheRowHeight()
    {
        // A coluna tem exatamente a largura dos ícones que cabem nela.
        QCOMPARE(FolderActionsView::columnWidthFor(0), 0);
        const int width = FolderActionsView::columnWidthFor(3);
        QCOMPARE(width, 3 * FolderActionsView::kBox + 2 * FolderActionsView::kGap + 2 * FolderActionsView::kMargin);
        const QRect cell(300, 10, width, 28);
        const QVector<QRect> rects = FolderActionsView::iconRects(cell, 3);
        QCOMPARE(rects.size(), 3);
        QVERIFY(rects.at(0).left() < rects.at(1).left() && rects.at(1).left() < rects.at(2).left());
        QCOMPARE(rects.first().left(), cell.left() + FolderActionsView::kMargin);
        QCOMPARE(rects.last().right() + FolderActionsView::kMargin, cell.right()); // right() é inclusivo
        for (const QRect &r : rects) {
            QCOMPARE(r.size(), QSize(FolderActionsView::kBox, FolderActionsView::kBox));
        }
        // Centrado na altura da linha, em alturas pares E ímpares (o erro vira "descentralizado").
        for (int height : {24, 27, 28, 31, 40}) {
            const QRect row(0, 100, 200, height);
            const QRect r = FolderActionsView::iconRects(row, 1).first();
            QVERIFY2(qAbs((r.top() - row.top()) - (row.bottom() - r.bottom())) <= 1, qPrintable(QString::number(height)));
        }
        // Coluna mais estreita que os ícones: só cabem os que cabem.
        QVERIFY(FolderActionsView::iconRects(QRect(0, 0, 60, 28), 6).size() < 6);
        QVERIFY(FolderActionsView::iconRects(QRect(0, 0, 400, 28), 0).isEmpty());
        QVERIFY(FolderActionsView::iconRects(QRect(0, 0, 10, 28), 3).isEmpty());
    }

    // As ações dividem a coluna de status/tempo com o play dos comandos (uma linha é de comando
    // OU de pasta): a coluna não ganha uma terceira e só cresce se as ações precisarem.
    void actionsShareTheStatusColumnWithTheRunningMarker()
    {
        Fixture f;
        fill(f);
        QCOMPARE(f.tree->columnCount(), 2);
        QCOMPARE(FolderActionsView::kActionsColumn, 1);
        const QRect first = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_a")), 1).first();
        const int statusLeft = f.tree->columnViewportPosition(1);
        const int statusRight = statusLeft + f.tree->columnWidth(1);
        QVERIFY2(first.left() >= statusLeft && first.right() < statusRight, "o ícone tem que ficar dentro da coluna de status/tempo");
        QVERIFY(!f.tree->isColumnHidden(1));
        // A coluna cabe a pasta com mais ações (Repo A: 3) e o contador "00h 00m 00s".
        QVERIFY(f.tree->columnWidth(1) >= FolderActionsView::columnWidthFor(3));
        // O primeiro ícone alinha entre as linhas (mesma coluna).
        QCOMPARE(FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_b")), 1).first().left(), first.left());

        // O ícone está centrado na altura REAL da linha.
        const QRect row = f.tree->visualItemRect(findItem(f.tree, QStringLiteral("repo_a")));
        QVERIFY(qAbs((first.top() - row.top()) - (row.bottom() - first.bottom())) <= 1);

        // Sem nenhuma ação a coluna continua (é a do tempo), na largura de antes.
        const int withActions = f.tree->columnWidth(1);
        f.widget.setGlobalActions({});
        f.folders[2].actions.clear();
        f.widget.setData(f.folders, f.commands, {});
        f.tree = qobject_cast<QTreeWidget *>(f.widget.findChild<QTabWidget *>()->currentWidget());
        QVERIFY(!f.tree->isColumnHidden(1));
        QVERIFY(f.tree->columnWidth(1) <= withActions);
    }

    // Já carregado e parado, passar o mouse no ícone mostra o "play" (o próximo clique roda de novo).
    // Regressão: o ícone ficava 2px de um lado e 4px do outro dentro da caixa (QRect::center()
    // arredonda pra esquerda/cima). Um ícone simétrico tem que ter as MESMAS margens nos dois lados.
    // Ações de EXPANSÃO: não ganham ícone; atrás dos ícones comuns fica UM símbolo de expansão
    // (só se a pasta tiver alguma), e clicar nele abre um menu com elas — escolher um item é o
    // mesmo que clicar no ícone.
    void expansionActionsHideBehindOneExpanderWithAMenu()
    {
        Fixture f;
        fill(f);
        f.folders[2].expansionActions = {QStringLiteral("c_fetch")};
        f.widget.setGlobalActions({{QStringLiteral("c_status"), false, false}, {QStringLiteral("c_git"), false, true}});
        f.widget.setData(f.folders, f.commands, {});
        f.widget.show();
        QTest::qWait(50);
        f.tree = qobject_cast<QTreeWidget *>(f.widget.findChild<QTabWidget *>()->currentWidget());
        f.view = qobject_cast<FolderActionsView *>(f.tree->itemDelegateForColumn(FolderActionsView::kActionsColumn));
        QVERIFY(f.view);

        // Repo A: Status e Pull comuns; Git (global) e Fetch (da pasta) escondidos no expansor.
        QCOMPARE(namesOn(f, QStringLiteral("repo_a")), (QStringList{"Status", "Pull"}));
        const QVector<QRect> rects = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_a")), 3);
        QCOMPARE(rects.size(), 3); // 2 ícones + o expansor
        const auto expander = f.view->hitTest(rects.at(2).center());
        QVERIFY(expander.valid && expander.expander);
        QCOMPARE(expander.icon.folderId, QStringLiteral("repo_a"));
        QVERIFY(f.tree->columnWidth(FolderActionsView::kActionsColumn) >= FolderActionsView::columnWidthFor(3));

        // Repo B: a global comum e o expansor (Git é global de expansão, vale em toda pasta).
        const QVector<QRect> repoB = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_b")), 2);
        QVERIFY(f.view->hitTest(repoB.at(1).center()).expander);
        QCOMPARE(namesOn(f, QStringLiteral("repo_b")), QStringList{"Status"});

        // O clique no expansor abre o menu só com as escondidas; escolher "Fetch" ativa a ação.
        QSignalSpy activated(&f.widget, &CommandTreeWidget::folderActionActivated);
        QStringList menuTexts;
        QTimer::singleShot(300, [&menuTexts]() {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            QAction *fetch = nullptr;
            for (QAction *a : menu->actions()) {
                menuTexts << a->text();
                if (a->text() == QStringLiteral("Fetch")) fetch = a;
            }
            if (!fetch) { menu->close(); return; }
            menu->setActiveAction(fetch);
            QTest::keyClick(menu, Qt::Key_Return);
        });
        QTest::mouseClick(f.tree->viewport(), Qt::LeftButton, {}, rects.at(2).center());
        QTest::qWait(600);
        QCOMPARE(menuTexts, (QStringList{"Git", "Fetch"}));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.last().at(0).toString(), QStringLiteral("c_fetch"));
        QCOMPARE(activated.last().at(1).toString(), QStringLiteral("repo_a"));

        // Clicar num ícone comum continua ativando direto.
        QTest::mouseClick(f.tree->viewport(), Qt::LeftButton, {}, rects.at(1).center());
        QCOMPARE(activated.count(), 2);
        QCOMPARE(activated.last().at(0).toString(), QStringLiteral("c_pull"));
    }

    // GRUPOS: as ações de um grupo viram UM ícone (antes do expansor); o clique abre o menu delas.
    void groupedActionsCollapseIntoOneIconWithAMenu()
    {
        Fixture f;
        fill(f);
        f.folders[2].actionGroups = {{QStringLiteral("c_pull"), QStringLiteral("Git")}, {QStringLiteral("c_fetch"), QStringLiteral("git")}};
        f.folders[2].groupIcons = {{QStringLiteral("Git"), QStringLiteral("git-branch")}};
        f.widget.setGlobalActions({{QStringLiteral("c_status"), false, false}, {QStringLiteral("c_git"), false, true}});
        f.widget.setData(f.folders, f.commands, {});
        f.widget.show();
        QTest::qWait(50);
        f.tree = qobject_cast<QTreeWidget *>(f.widget.findChild<QTabWidget *>()->currentWidget());
        f.view = qobject_cast<FolderActionsView *>(f.tree->itemDelegateForColumn(FolderActionsView::kActionsColumn));
        QVERIFY(f.view);

        // Repo A: Status (comum), UM ícone "Git" (Pull + Fetch) e o expansor (Git global de expansão).
        const QVector<QRect> rects = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_a")), 3);
        QCOMPARE(rects.size(), 3);
        QCOMPARE(f.view->hitTest(rects.at(0).center()).icon.name, QStringLiteral("Status"));
        const auto group = f.view->hitTest(rects.at(1).center());
        QVERIFY(group.valid && !group.expander);
        QCOMPARE(group.group, QStringLiteral("Git"));
        QVERIFY(f.view->hitTest(rects.at(2).center()).expander);
        QCOMPARE(namesOn(f, QStringLiteral("repo_a")), QStringList{"Status"}); // sem ícone próprio pras do grupo

        QSignalSpy activated(&f.widget, &CommandTreeWidget::folderActionActivated);
        QStringList menuTexts;
        QTimer::singleShot(300, [&menuTexts]() {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            QAction *pull = nullptr;
            for (QAction *a : menu->actions()) {
                menuTexts << a->text();
                if (a->text() == QStringLiteral("Pull")) pull = a;
            }
            if (!pull) { menu->close(); return; }
            menu->setActiveAction(pull);
            QTest::keyClick(menu, Qt::Key_Return);
        });
        QTest::mouseClick(f.tree->viewport(), Qt::LeftButton, {}, rects.at(1).center());
        QTest::qWait(600);
        QCOMPARE(menuTexts, (QStringList{"Pull", "Fetch"}));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.last().at(0).toString(), QStringLiteral("c_pull"));
        QCOMPARE(activated.last().at(1).toString(), QStringLiteral("repo_a"));

        // Sem ações de expansão e sem grupo, a linha não ganha nenhuma posição extra.
        QCOMPARE(FolderActionsView::slotCount({}), 0);
    }

    // Menu de contexto de pasta E de comando tem "Exportar" e ele avisa qual item foi.
    void theContextMenuOffersExportForFoldersAndCommands()
    {
        Fixture f;
        fill(f);
        f.tree->expandAll();
        QSignalSpy exports(&f.widget, &CommandTreeWidget::exportRequested);
        auto chooseExport = [&](const QString &id, const QString &key) {
            QTreeWidgetItem *item = findItem(f.tree, id);
            QVERIFY(item);
            f.tree->setCurrentItem(item);
            QTimer::singleShot(150, [key]() {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu) return;
                for (QAction *a : menu->actions()) {
                    if (a->text() == kai::utils::tr(key)) {
                        menu->setActiveAction(a);
                        QTest::keyClick(menu, Qt::Key_Return); // como o usuário: o exec() devolve a ação escolhida
                        return;
                    }
                }
                menu->close();
            });
            f.widget.openContextMenuForCurrent();
        };
        chooseExport(QStringLiteral("repo_a"), QStringLiteral("ctx.folder.export"));
        QCOMPARE(exports.count(), 1);
        QCOMPARE(exports.last().at(0).toString(), QStringLiteral("repo_a"));
        QCOMPARE(exports.last().at(1).toBool(), true);

        chooseExport(QStringLiteral("c_pull"), QStringLiteral("ctx.command.export"));
        QCOMPARE(exports.count(), 2);
        QCOMPARE(exports.last().at(0).toString(), QStringLiteral("c_pull"));
        QCOMPARE(exports.last().at(1).toBool(), false);
    }

    // Pasta dessincronizada com o kai.json: selo no ícone + tooltip dizendo o que mudou; volta ao
    // normal quando a situação some.
    void aDesyncedFolderGetsABadgeAndATooltipAndClearsWhenInSync()
    {
        Fixture f;
        fill(f);
        QTreeWidgetItem *item = findItem(f.tree, QStringLiteral("repo_a"));
        QVERIFY(item);
        auto iconImage = [&]() { return item->icon(0).pixmap(16, 16).toImage(); };
        const QImage plain = iconImage();
        QVERIFY(item->toolTip(0).isEmpty());

        f.widget.setSyncDrift({{QStringLiteral("repo_a"), kai::core::SyncDrift::KaiChanged}});
        item = findItem(f.tree, QStringLiteral("repo_a"));
        QVERIFY(iconImage() != plain);
        QCOMPARE(item->toolTip(0), kai::utils::tr(QStringLiteral("tree.sync.kai_changed")));

        f.widget.setSyncDrift({{QStringLiteral("repo_a"), kai::core::SyncDrift::FileChanged}});
        QCOMPARE(findItem(f.tree, QStringLiteral("repo_a"))->toolTip(0), kai::utils::tr(QStringLiteral("tree.sync.file_changed")));
        f.widget.setSyncDrift({{QStringLiteral("repo_a"), kai::core::SyncDrift::BothChanged}});
        QCOMPARE(findItem(f.tree, QStringLiteral("repo_a"))->toolTip(0), kai::utils::tr(QStringLiteral("tree.sync.both_changed")));

        // O destaque sobrevive a reconstruir a árvore (setData recria os itens).
        f.widget.setData(f.folders, f.commands, {});
        f.tree = qobject_cast<QTreeWidget *>(f.widget.findChild<QTabWidget *>()->currentWidget());
        QVERIFY(f.tree);
        QVERIFY2(findItem(f.tree, QStringLiteral("repo_a")) && !findItem(f.tree, QStringLiteral("repo_a"))->toolTip(0).isEmpty(),
                 "reconstruir a árvore não pode perder o destaque");

        // Em sincronia (ou ausente do mapa): normal.
        f.widget.setSyncDrift({{QStringLiteral("repo_a"), kai::core::SyncDrift::InSync}});
        item = findItem(f.tree, QStringLiteral("repo_a"));
        QVERIFY(item->toolTip(0).isEmpty());
        QCOMPARE(iconImage(), plain);
    }

    void theIconIsCenteredInsideItsBox()
    {
        Fixture f;
        fill(f);
        for (Command &c : f.commands) c.icon = QStringLiteral("square");
        f.widget.setData(f.folders, f.commands, {});
        f.tree->setStyleSheet(QStringLiteral("QTreeView::item { height: 30px; }")); // linha de tamanho real
        QTest::qWait(50);
        QTest::mouseMove(f.tree->viewport(), QPoint(5, 5));
        QTest::qWait(30);

        const QRect box = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_a")), 3).at(1);
        const QImage image = f.tree->viewport()->grab(box).toImage().convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(image.size(), box.size());
        const QColor background = image.pixelColor(0, 0);
        auto differs = [&](int x, int y) {
            const QColor c = image.pixelColor(x, y);
            return qAbs(c.red() - background.red()) + qAbs(c.green() - background.green())
                   + qAbs(c.blue() - background.blue()) > 90;
        };
        int minX = image.width(), maxX = -1, minY = image.height(), maxY = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (differs(x, y)) {
                    minX = qMin(minX, x); maxX = qMax(maxX, x);
                    minY = qMin(minY, y); maxY = qMax(maxY, y);
                }
            }
        }
        QVERIFY2(maxX > minX && maxY > minY, "o ícone não foi desenhado");
        const int left = minX, right = image.width() - 1 - maxX;
        const int top = minY, bottom = image.height() - 1 - maxY;
        QVERIFY2(qAbs(left - right) <= 1, qPrintable(QStringLiteral("margens horizontais %1 x %2").arg(left).arg(right)));
        QVERIFY2(qAbs(top - bottom) <= 1, qPrintable(QStringLiteral("margens verticais %1 x %2").arg(top).arg(bottom)));
    }

    void hoveringTheFocusedIdleIconShowsAPlayAffordance()
    {
        Fixture f;
        fill(f);
        const QString id = folderActionCommandId(QStringLiteral("c_pull"), QStringLiteral("repo_a"));
        f.widget.setFocusedAction(id);
        const QRect pull = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_a")), 3).at(1);
        QTest::mouseMove(f.tree->viewport(), QPoint(5, 5));
        QTest::qWait(30);
        const QImage plain = f.tree->viewport()->grab(pull).toImage();
        QTest::mouseMove(f.tree->viewport(), pull.center());
        QTest::qWait(30);
        const QImage hovered = f.tree->viewport()->grab(pull).toImage();
        QVERIFY2(plain != hovered, "o ícone focado e parado deveria mudar (play) ao passar o mouse");

        // Rodando: o ícone não vira play (o clique só mostra a saída).
        f.widget.setRunningCommandIds({id});
        QTest::qWait(30);
        const QImage running = f.tree->viewport()->grab(pull).toImage();
        QTest::mouseMove(f.tree->viewport(), QPoint(5, 5));
        QTest::qWait(30);
        QTest::mouseMove(f.tree->viewport(), pull.center());
        QTest::qWait(30);
        QCOMPARE(f.tree->viewport()->grab(pull).toImage().size(), running.size());
    }

    void subfoldersShowGlobalActionsFirstThenTheirOwnInOrder()
    {
        Fixture f;
        fill(f);
        QVERIFY(f.tree && f.view);
        QCOMPARE(namesOn(f, QStringLiteral("repo_a")), (QStringList{"Status", "Pull", "Fetch"}));
        QCOMPARE(namesOn(f, QStringLiteral("repo_b")), QStringList{"Status"});
        // A global "só em projeto" aparece só na pasta-projeto.
        QCOMPARE(namesOn(f, QStringLiteral("proj")), (QStringList{"Status", "Git"}));
    }

    void clickingAnIconActivatesItWithoutSelectingTheRow()
    {
        Fixture f;
        fill(f);
        QTreeWidgetItem *other = findItem(f.tree, QStringLiteral("repo_b"));
        f.tree->setCurrentItem(other);
        QSignalSpy activated(&f.widget, &CommandTreeWidget::folderActionActivated);
        QSignalSpy selection(&f.widget, &CommandTreeWidget::selectionChanged);

        const QRect pull = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_a")), 3).at(1);
        QTest::mouseClick(f.tree->viewport(), Qt::LeftButton, {}, pull.center());

        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().at(0).toString(), QStringLiteral("c_pull"));
        QCOMPARE(activated.first().at(1).toString(), QStringLiteral("repo_a"));
        QCOMPARE(f.tree->currentItem(), other);   // a linha NÃO foi selecionada
        QCOMPARE(selection.count(), 0);

        // Soltar fora do ícone cancela o clique.
        activated.clear();
        QTest::mousePress(f.tree->viewport(), Qt::LeftButton, {}, pull.center());
        QTest::mouseRelease(f.tree->viewport(), Qt::LeftButton, {}, QPoint(5, pull.center().y()));
        QCOMPARE(activated.count(), 0);
    }

    void rightClickOnAnIconAsksForItsMenuAndOnlyThere()
    {
        Fixture f;
        fill(f);
        QSignalSpy menu(&f.widget, &CommandTreeWidget::folderActionContextRequested);
        const QRect fetch = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_a")), 3).at(2);
        QContextMenuEvent onIcon(QContextMenuEvent::Mouse, fetch.center(), f.tree->viewport()->mapToGlobal(fetch.center()));
        QApplication::sendEvent(f.tree->viewport(), &onIcon);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.first().at(0).toString(), QStringLiteral("c_fetch"));
        QCOMPARE(menu.first().at(1).toString(), QStringLiteral("repo_a"));
    }

    void focusClearsWhenAnotherRowIsSelectedOrTheSameRowIsClickedOutsideTheIcons()
    {
        Fixture f;
        fill(f);
        const QString virtualId = folderActionCommandId(QStringLiteral("c_pull"), QStringLiteral("repo_a"));
        f.widget.setFocusedAction(virtualId);
        QCOMPARE(f.widget.focusedAction(), virtualId);

        // Reconstruir a árvore (salvar um comando, por exemplo) restaura a
        // seleção mas NÃO desfoca o ícone.
        f.tree->setCurrentItem(findItem(f.tree, QStringLiteral("repo_a")));
        f.widget.setFocusedAction(virtualId);
        f.widget.setData(f.folders, f.commands, {});
        QCOMPARE(f.widget.focusedAction(), virtualId);
        f.tree = qobject_cast<QTreeWidget *>(f.widget.findChild<QTabWidget *>()->currentWidget());
        QVERIFY(f.tree);

        // Selecionar outra linha desfoca.
        f.tree->setCurrentItem(findItem(f.tree, QStringLiteral("repo_b")));
        QVERIFY(f.widget.focusedAction().isEmpty());

        // Clicar na MESMA linha da pasta, fora dos ícones, desfoca e devolve a seleção à pasta.
        f.widget.setFocusedAction(virtualId);
        QSignalSpy selection(&f.widget, &CommandTreeWidget::selectionChanged);
        // Clique na linha da pasta, na área do NOME (fora dos ícones, que ficam no fim da linha).
        const QRect row = f.tree->visualItemRect(findItem(f.tree, QStringLiteral("repo_a")));
        QTest::mouseClick(f.tree->viewport(), Qt::LeftButton, {}, QPoint(row.left() + 60, row.center().y()));
        QVERIFY(f.widget.focusedAction().isEmpty());
        QVERIFY(selection.count() >= 1);
        QCOMPARE(selection.first().at(0).toString(), QStringLiteral("repo_a"));
        QVERIFY(selection.first().at(1).toBool());
    }

    // Verde enquanto roda, accent parada: confere nos pixels do viewport.
    void runningIconsAreGreenAndIdleOnesUseTheAccent()
    {
        Fixture f;
        fill(f);
        const QColor accent(kai::utils::tokens::accent());
        const QColor green(kai::utils::tokens::successFg());
        auto hasColor = [&](const QRect &rect, const QColor &want) {
            const QImage image = f.tree->viewport()->grab().toImage();
            for (int y = rect.top(); y <= rect.bottom(); ++y) {
                for (int x = rect.left(); x <= rect.right(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (qAbs(c.red() - want.red()) < 12 && qAbs(c.green() - want.green()) < 12
                        && qAbs(c.blue() - want.blue()) < 12) {
                        return true;
                    }
                }
            }
            return false;
        };
        const QRect first = FolderActionsView::iconRects(cellOf(f, QStringLiteral("repo_b")), 1).first();
        QVERIFY2(hasColor(first, accent), "ação parada deveria usar o accent");
        QVERIFY2(!hasColor(first, green), "ação parada não é verde");

        f.widget.setRunningCommandIds({folderActionCommandId(QStringLiteral("c_status"), QStringLiteral("repo_b"))});
        QTest::qWait(20);
        QVERIFY2(hasColor(first, green), "ação rodando deveria ser verde");
        // Só o par (Status, Repo B) roda: a mesma ação em outra pasta continua parada.
        const QRect otherFolder = FolderActionsView::iconRects(cellOf(f, QStringLiteral("proj")), 2).first();
        QVERIFY(!hasColor(otherFolder, green));
    }
};

QTEST_MAIN(TestFolderActionsTree)
#include "test_folder_actions_tree.moc"
