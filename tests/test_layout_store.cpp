#include <QDir>
#include <QFile>
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
        QCOMPARE(loaded.layout.zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Left));
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
        QCOMPARE(loaded.layout.zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Right));
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
        model.placeView(view_id::kOutput, Zone::Right);
        store.scheduleSave(model, {}); // a última chamada vence
        QVERIFY(!QFile::exists(path)); // ainda no debounce: nada na interface foi bloqueado por I/O
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(LayoutStore(path).load(known()).layout.zoneOf(view_id::kOutput) == std::optional<Zone>(Zone::Right), 3000);
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
            model.placeView(view_id::kOutput, Zone::Left);
            store.scheduleSave(model, {});
        } // sai antes do debounce
        QVERIFY(QFile::exists(path));
        LayoutStore store(path);
        QCOMPARE(store.load(known()).layout.zoneOf(view_id::kOutput), std::optional<Zone>(Zone::Left));
    }

    void perspectivesRoundTripAndDropGhostPanels()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layout.json"));
        {
            LayoutStore store(path);
            LayoutModel mine = LayoutModel::defaultLayout();
            mine.placeView(view_id::kHistory, Zone::Right);
            store.scheduleSave(LayoutModel::defaultLayout(), {{QStringLiteral("mine"), mine}});
            store.flush();
        }
        // Edita à mão: a perspectiva ganha um painel que não existe e um nome vazio.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QJsonObject perspectives = root.value(QStringLiteral("perspectives")).toObject();
        QJsonObject mine = perspectives.value(QStringLiteral("mine")).toObject();
        QJsonObject zones = mine.value(QStringLiteral("zones")).toObject();
        QJsonObject right = zones.value(QStringLiteral("right")).toObject();
        QJsonArray views = right.value(QStringLiteral("views")).toArray();
        views.append(QStringLiteral("ghost"));
        right.insert(QStringLiteral("views"), views);
        zones.insert(QStringLiteral("right"), right);
        mine.insert(QStringLiteral("zones"), zones);
        perspectives.insert(QStringLiteral("mine"), mine);
        perspectives.insert(QStringLiteral(" "), mine);
        root.insert(QStringLiteral("perspectives"), perspectives);
        file.resize(0);
        file.write(QJsonDocument(root).toJson());
        file.close();

        LayoutStore store(path);
        const auto loaded = store.load(known());
        QCOMPARE(loaded.perspectives.keys(), QStringList{QStringLiteral("mine")});
        QCOMPARE(loaded.perspectives.value(QStringLiteral("mine")).zone(Zone::Right).views,
                 (QStringList{view_id::kOutput, view_id::kHistory})); // o painel fantasma saiu
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
