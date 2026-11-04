#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "ui/layout/layout-store.h"

using namespace kai::ui::layout;

namespace {
LayoutModel::Validator known()
{
    return [](const QString &id) {
        return id == view_id::kCommands || id == view_id::kOutput || id == view_id::kHistory;
    };
}
} // namespace

// layout.json: a leitura que conserta, a migração das configurações antigas e a gravação atômica fora da interface.
class TestLayoutStore : public QObject {
    Q_OBJECT

private slots:
    void aMissingFileGivesTheDefault()
    {
        QTemporaryDir dir;
        LayoutStore store(dir.filePath(QStringLiteral("layout.json")));
        const auto loaded = store.load(known());
        QVERIFY(!loaded.fileExisted);
        QVERIFY(!loaded.recovered);
        QCOMPARE(loaded.layout, LayoutModel::defaultLayout());
    }

    void aMissingFileMigratesTheLegacyOutputPosition()
    {
        QTemporaryDir dir;
        LayoutStore store(dir.filePath(QStringLiteral("layout.json")));
        const auto loaded = store.load(known(), []() { return LayoutModel::fromLegacy(QStringLiteral("left"), {4000, 8000}); });
        QVERIFY(!loaded.fileExisted);
        QCOMPARE(loaded.layout.zoneOf(view_id::kOutput), std::optional<Zone>(Zone{0, 0}));
    }

    void anExistingFileBeatsTheLegacyMigration()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            LayoutStore writer(path);
            writer.scheduleSave(LayoutModel::defaultLayout(), {});
            writer.flush();
        }
        LayoutStore store(path);
        bool legacyAsked = false;
        const auto loaded = store.load(known(), [&legacyAsked]() {
            legacyAsked = true;
            return LayoutModel::fromLegacy(QStringLiteral("left"), {});
        });
        QVERIFY(loaded.fileExisted);
        QVERIFY(!legacyAsked);
        QCOMPARE(loaded.layout.zoneOf(view_id::kOutput), std::optional<Zone>(Zone{0, 1}));
    }

    void garbageIsKeptAsABackupAndTheDefaultTakesOver()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[1, 2,");
        }
        LayoutStore store(path);
        const auto loaded = store.load(known());
        QVERIFY(loaded.recovered);
        QCOMPARE(loaded.layout, LayoutModel::defaultLayout());
        QVERIFY(!QFile::exists(path));
        QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("layout.json.corrupt-*")}).size(), 1);
    }

    void aJsonThatIsNotAnObjectCountsAsCorrupt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[1, 2, 3]");
        }
        LayoutStore store(path);
        QVERIFY(store.load(known()).recovered);
    }

    void savingIsDebouncedAndWritesAnAtomicFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        LayoutStore store(path);
        LayoutModel model = LayoutModel::defaultLayout();
        store.scheduleSave(model, {});
        model.placeView(view_id::kOutput, Zone{0, 1});
        store.scheduleSave(model, {}); // a última chamada vence
        QVERIFY(!QFile::exists(path)); // ainda no debounce: nada na interface foi bloqueado por I/O
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(LayoutStore(path).load(known()).layout.zoneOf(view_id::kOutput) == std::optional<Zone>(Zone{0, 1}), 3000);
        // Nenhum arquivo temporário sobra ao lado.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }

    void theDestructorFlushesWhatWasPending()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            LayoutStore store(path);
            LayoutModel model = LayoutModel::defaultLayout();
            model.placeView(view_id::kOutput, Zone{0, 0});
            store.scheduleSave(model, {});
        } // sai antes do debounce
        QVERIFY(QFile::exists(path));
        LayoutStore store(path);
        QCOMPARE(store.load(known()).layout.zoneOf(view_id::kOutput), std::optional<Zone>(Zone{0, 0}));
    }

    void perspectivesRoundTripAndDropGhostPanels()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            LayoutStore store(path);
            LayoutModel mine = LayoutModel::defaultLayout();
            mine.placeView(view_id::kHistory, Zone{0, 1});
            store.scheduleSave(LayoutModel::defaultLayout(), {{QStringLiteral("mine"), mine}});
            store.flush();
        }
        // Edita à mão: a perspectiva ganha um painel que não existe e um nome vazio.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QJsonObject perspectives = root.value(QStringLiteral("perspectives")).toObject();
        QJsonObject mine = perspectives.value(QStringLiteral("mine")).toObject();
        QJsonObject cells = mine.value(QStringLiteral("cells")).toObject();
        QJsonObject right = cells.value(QStringLiteral("0,1")).toObject();
        QJsonArray views = right.value(QStringLiteral("views")).toArray();
        views.append(QStringLiteral("ghost"));
        right.insert(QStringLiteral("views"), views);
        cells.insert(QStringLiteral("0,1"), right);
        mine.insert(QStringLiteral("cells"), cells);
        perspectives.insert(QStringLiteral("mine"), mine);
        perspectives.insert(QStringLiteral(" "), mine);
        root.insert(QStringLiteral("perspectives"), perspectives);
        file.resize(0);
        file.write(QJsonDocument(root).toJson());
        file.close();

        LayoutStore store(path);
        const auto loaded = store.load(known());
        QCOMPARE(loaded.perspectives.keys(), QStringList{QStringLiteral("mine")});
        QCOMPARE(loaded.perspectives.value(QStringLiteral("mine")).zone(Zone{0, 1}).views,
                 (QStringList{view_id::kOutput, view_id::kHistory})); // o painel fantasma saiu
    }

    void aFileFromBeforeTheGridStartsFromTheDefaultAndKeepsACopy()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        // O formato das cinco zonas fixas (versão 2), com uma perspectiva salva.
        const QByteArray old = QByteArrayLiteral(
            "{\"version\":2,\"zones\":{\"left\":{\"views\":[\"output\"]},\"center\":{\"views\":[\"commands\"]}},"
            "\"perspectives\":{\"mine\":{\"version\":2,\"zones\":{\"right\":{\"views\":[\"history\"]}}}}}");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(old);
        }
        LayoutStore store(path);
        const auto loaded = store.load(known());
        QVERIFY(loaded.fileExisted);
        QVERIFY(!loaded.recovered);
        QCOMPARE(loaded.layout, LayoutModel::defaultLayout());
        QVERIFY(loaded.perspectives.isEmpty());
        QFile backup(path + QStringLiteral(".pre-grid"));
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), old);
    }

    void aPerspectiveFromBeforeTheGridIsDropped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            LayoutStore writer(path);
            writer.scheduleSave(LayoutModel::defaultLayout(),
                                {{QStringLiteral("new"), LayoutModel::preset(LayoutModel::Preset::Triple)}});
            writer.flush();
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QJsonObject perspectives = root.value(QStringLiteral("perspectives")).toObject();
        perspectives.insert(QStringLiteral("old"), QJsonObject{{QStringLiteral("version"), 2}});
        root.insert(QStringLiteral("perspectives"), perspectives);
        file.resize(0);
        file.write(QJsonDocument(root).toJson());
        file.close();

        const auto loaded = LayoutStore(path).load(known());
        QCOMPARE(loaded.perspectives.keys(), QStringList{QStringLiteral("new")});
        QCOMPARE(loaded.perspectives.value(QStringLiteral("new")).cols(), 3);
    }

    void aGridLayoutRoundTripsThroughTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        LayoutModel model = LayoutModel::defaultLayout();
        model.setGrid(3, 2, Expansion::Horizontal);
        model.placeView(view_id::kHistory, Zone{1, 2});
        model.repair(known());
        {
            LayoutStore writer(path);
            writer.scheduleSave(model, {});
            writer.flush();
        }
        const auto loaded = LayoutStore(path).load(known());
        QCOMPARE(loaded.layout, model);
        QCOMPARE(loaded.layout.expansion(), Expansion::Horizontal);
    }

    void anInMemoryStoreNeverTouchesTheDisk()
    {
        LayoutStore store{QString()};
        store.scheduleSave(LayoutModel::defaultLayout(), {});
        store.flush();
        QCOMPARE(store.load(known()).layout, LayoutModel::defaultLayout());
    }
};

QTEST_MAIN(TestLayoutStore)
#include "test_layout_store.moc"
