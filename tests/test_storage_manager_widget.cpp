#include <QTest>
#include <QApplication>
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QMessageBox>
#include <QTimer>
#include <algorithm>

#include "ui/shared/storage-manager-widget.h"
#include "core/config-manager.h"
#include "core/models.h"

using namespace kai::ui;
using namespace kai::core;

namespace {

// Layout de colunas da tabela (ver storage-manager-widget.cpp): checkbox
// numa coluna própria (0), depois Nome/Tipo/Pasta-Origem/Ações.
constexpr int kCheckCol = 0;
constexpr int kNameCol = 1;
constexpr int kLocationCol = 3;
constexpr int kActionsCol = 4;

// A confirmação de exclusão abre um QMessageBox::question BLOQUEANTE
// (exec síncrono) — agenda um clique em "Sim" pra rodar assim que o
// diálogo entrar no próprio loop de eventos, senão o teste travaria
// esperando um clique que nunca vem.
void confirmNextMessageBox(QMessageBox::StandardButton button = QMessageBox::Yes)
{
    QTimer::singleShot(50, [button]() {
        if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            mb->button(button)->click();
        }
    });
}

Command makeCommand(const QString &id, const QString &name, const QString &folderId)
{
    Command c;
    c.id = id;
    c.name = name;
    c.folderId = folderId;
    return c;
}

Folder makeFolder(const QString &id, const QString &name, const QString &parentId = QString())
{
    Folder f;
    f.id = id;
    f.name = name;
    f.parentId = parentId.isEmpty() ? std::nullopt : std::make_optional(parentId);
    return f;
}

Collection makeCollection(const QString &id, const QString &name, const QString &folderId, int entryCount)
{
    Collection col;
    col.id = id;
    col.name = name;
    col.folderId = folderId;
    col.schema = Collection::defaultSchema();
    for (int i = 0; i < entryCount; ++i) {
        CollectionEntry e;
        e.id = QStringLiteral("%1_e%2").arg(id).arg(i);
        e.values.insert(QStringLiteral("key"), QStringLiteral("k%1").arg(i));
        col.entries << e;
    }
    return col;
}

QCheckBox *checkboxAt(QTableWidget *table, int row)
{
    // cellWidget() é o CONTAINER que centraliza o checkbox (ver
    // StorageManagerWidget::makeRowCheckbox) — o QCheckBox é filho dele.
    if (auto *container = table->cellWidget(row, kCheckCol)) {
        return container->findChild<QCheckBox *>();
    }
    return nullptr;
}

// Marca via checkbox (coluna própria) todas as linhas cujo nome contém
// `needle`.
void checkRowsContaining(QTableWidget *table, const QString &needle)
{
    for (int i = 0; i < table->rowCount(); ++i) {
        if (table->item(i, kNameCol)->text().contains(needle)) {
            checkboxAt(table, i)->setChecked(true);
        }
    }
}

QTableWidget *tableOf(StorageManagerWidget &widget)
{
    return widget.findChild<QTableWidget *>();
}

QComboBox *kindComboOf(StorageManagerWidget &widget)
{
    return widget.findChild<QComboBox *>();
}

QLineEdit *filterOf(StorageManagerWidget &widget)
{
    return widget.findChild<QLineEdit *>();
}

QPushButton *duplicateButtonOf(StorageManagerWidget &widget)
{
    return widget.findChild<QPushButton *>(QStringLiteral("storageDuplicateButton"));
}

QPushButton *deleteButtonOf(StorageManagerWidget &widget)
{
    return widget.findChild<QPushButton *>(QStringLiteral("storageDeleteButton"));
}

}

class TestStorageManagerWidget : public QObject {
    Q_OBJECT

private slots:
    void initialCountsMatchDataPerKind()
    {
        CommandsData data;
        data.folders << makeFolder(QStringLiteral("f1"), QStringLiteral("Pasta 1"));
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build"), QStringLiteral("f1"))
                       << makeCommand(QStringLiteral("c2"), QStringLiteral("Deploy"), QStringLiteral("f1"));
        QVector<Collection> collections = {makeCollection(QStringLiteral("col1"), QStringLiteral("Usuarios"),
                                                            QStringLiteral("f1"), 3)};

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        QCOMPARE(tableOf(widget)->rowCount(), 2); // default kind = Comandos
        // Checkbox é um cell widget de verdade, numa coluna própria.
        QVERIFY(checkboxAt(tableOf(widget), 0));
        QVERIFY(!checkboxAt(tableOf(widget), 0)->isChecked());

        kindComboOf(widget)->setCurrentIndex(1); // Pastas
        QCOMPARE(tableOf(widget)->rowCount(), 1);

        kindComboOf(widget)->setCurrentIndex(2); // Coleções
        QCOMPARE(tableOf(widget)->rowCount(), 1);
        // Coluna "Pasta/Origem" traz a contagem de registros junto.
        QVERIFY(tableOf(widget)->item(0, kLocationCol)->text().contains(QStringLiteral("3")));
    }

    void filterRestrictsVisibleItems()
    {
        CommandsData data;
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build Release"), QString())
                       << makeCommand(QStringLiteral("c2"), QStringLiteral("Deploy Prod"), QString());
        QVector<Collection> collections;

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        filterOf(widget)->setText(QStringLiteral("Deploy"));
        QCOMPARE(tableOf(widget)->rowCount(), 1);
        QVERIFY(tableOf(widget)->item(0, kNameCol)->text().contains(QStringLiteral("Deploy Prod")));
    }

    // Bug relatado (print): clicar bem na checkbox não fazia nada, porque
    // o indicador nativo do item E o clique-na-linha tentavam alternar o
    // MESMO estado juntos e se cancelavam. Agora a checkbox é um widget
    // de verdade numa coluna própria — clicar nela (setChecked) é o ÚNICO
    // caminho que a afeta diretamente, sem conflito.
    void checkboxTogglesIndependentlyAndShowsContextualBar()
    {
        CommandsData data;
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build"), QString());
        QVector<Collection> collections;

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        auto *contextualBar = widget.findChild<QWidget *>(QStringLiteral("storageContextualBar"));
        QVERIFY(contextualBar);
        QVERIFY(contextualBar->isHidden()); // sem seleção, a barra começa escondida

        QCheckBox *checkbox = checkboxAt(tableOf(widget), 0);
        QVERIFY(!checkbox->isChecked());
        checkbox->setChecked(true);
        QVERIFY(checkbox->isChecked());
        QVERIFY(!contextualBar->isHidden());

        checkbox->setChecked(false);
        QVERIFY(contextualBar->isHidden());
    }

    void deleteSelectedRemovesOnlyCheckedCommands()
    {
        CommandsData data;
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build"), QString())
                       << makeCommand(QStringLiteral("c2"), QStringLiteral("Deploy"), QString())
                       << makeCommand(QStringLiteral("c3"), QStringLiteral("Test"), QString());
        QVector<Collection> collections;
        int persistCalls = 0;

        StorageManagerWidget widget(data, collections, [&] { ++persistCalls; }, [] {});
        checkRowsContaining(tableOf(widget), QStringLiteral("Deploy"));

        confirmNextMessageBox();
        deleteButtonOf(widget)->click();

        QCOMPARE(data.commands.size(), 2);
        QVERIFY(std::none_of(data.commands.constBegin(), data.commands.constEnd(),
            [](const Command &c) { return c.id == QStringLiteral("c2"); }));
        QCOMPARE(persistCalls, 1);
    }

    void duplicateCommandCreatesUniqueNonCollidingCopy()
    {
        CommandsData data;
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build"), QStringLiteral("f1"));
        QVector<Collection> collections;

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        checkRowsContaining(tableOf(widget), QStringLiteral("Build"));

        QVERIFY(duplicateButtonOf(widget)->isEnabled());
        duplicateButtonOf(widget)->click();

        QCOMPARE(data.commands.size(), 2);
        const Command &copy = data.commands.last();
        QVERIFY(copy.id != QStringLiteral("c1"));
        QVERIFY(copy.name != QStringLiteral("Build"));
        QVERIFY(copy.name.startsWith(QStringLiteral("Build ")));
    }

    void duplicateCollectionCopiesEntries()
    {
        CommandsData data;
        QVector<Collection> collections = {makeCollection(QStringLiteral("col1"), QStringLiteral("Usuarios"),
                                                            QString(), 2)};

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        kindComboOf(widget)->setCurrentIndex(2); // Coleções
        checkRowsContaining(tableOf(widget), QStringLiteral("Usuarios"));

        duplicateButtonOf(widget)->click();

        QCOMPARE(collections.size(), 2);
        QCOMPARE(collections.last().entries.size(), 2);
        QVERIFY(collections.last().id != QStringLiteral("col1"));
        QVERIFY(collections.last().entries.first().id != collections.first().entries.first().id);
    }

    void deleteFolderReparentsChildrenInsteadOfDeletingThem()
    {
        CommandsData data;
        data.folders << makeFolder(QStringLiteral("root"), QStringLiteral("Raiz"))
                      << makeFolder(QStringLiteral("mid"), QStringLiteral("Meio"), QStringLiteral("root"));
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build"), QStringLiteral("mid"));
        QVector<Collection> collections;

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        kindComboOf(widget)->setCurrentIndex(1); // Pastas
        checkRowsContaining(tableOf(widget), QStringLiteral("Meio"));

        confirmNextMessageBox();
        deleteButtonOf(widget)->click();

        // "mid" sumiu, mas o comando de dentro sobrevive e sobe pro avô.
        QCOMPARE(data.folders.size(), 1);
        QCOMPARE(data.folders.first().id, QStringLiteral("root"));
        QCOMPARE(data.commands.size(), 1);
        QCOMPARE(data.commands.first().folderId, QStringLiteral("root"));
    }

    void deletingParentAndChildFolderTogetherKeepsGrandchildren()
    {
        CommandsData data;
        data.folders << makeFolder(QStringLiteral("root"), QStringLiteral("Raiz"))
                      << makeFolder(QStringLiteral("mid"), QStringLiteral("Meio"), QStringLiteral("root"))
                      << makeFolder(QStringLiteral("leaf"), QStringLiteral("Folha"), QStringLiteral("mid"));
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build"), QStringLiteral("leaf"));
        QVector<Collection> collections;

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        kindComboOf(widget)->setCurrentIndex(1); // Pastas
        // Marca "Raiz" e "Meio" (mas NÃO "Folha") pra excluir os dois juntos.
        QTableWidget *table = tableOf(widget);
        for (int i = 0; i < table->rowCount(); ++i) {
            const QString text = table->item(i, kNameCol)->text();
            if (text.contains(QStringLiteral("Raiz")) || text.contains(QStringLiteral("Meio"))) {
                checkboxAt(table, i)->setChecked(true);
            }
        }

        confirmNextMessageBox();
        deleteButtonOf(widget)->click();

        // Só "leaf" sobrevive, e virou raiz (sem pai válido) — nada foi
        // perdido: nem a pasta-folha nem o comando lá dentro.
        QCOMPARE(data.folders.size(), 1);
        QCOMPARE(data.folders.first().id, QStringLiteral("leaf"));
        QVERIFY(!data.folders.first().parentId.has_value() || data.folders.first().parentId->isEmpty());
        QCOMPARE(data.commands.size(), 1);
        QCOMPARE(data.commands.first().folderId, QStringLiteral("leaf"));
    }

    void rowLevelDeleteButtonRemovesJustThatItem()
    {
        CommandsData data;
        data.commands << makeCommand(QStringLiteral("c1"), QStringLiteral("Build"), QString())
                       << makeCommand(QStringLiteral("c2"), QStringLiteral("Deploy"), QString());
        QVector<Collection> collections;

        StorageManagerWidget widget(data, collections, [] {}, [] {});
        QTableWidget *table = tableOf(widget);
        auto *actionsWidget = table->cellWidget(0, kActionsCol);
        QVERIFY(actionsWidget);
        const QList<QToolButton *> rowButtons = actionsWidget->findChildren<QToolButton *>();
        QCOMPARE(rowButtons.size(), 2);

        confirmNextMessageBox();
        rowButtons.last()->click(); // segundo botão = excluir

        QCOMPARE(data.commands.size(), 1);
    }
};

QTEST_MAIN(TestStorageManagerWidget)
#include "test_storage_manager_widget.moc"
