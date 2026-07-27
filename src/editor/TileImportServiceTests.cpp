#include "TileImportService.h"

#include "ProjectIndexService.h"
#include "ProjectLifecycleService.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

namespace
{
QString template_path()
{
    return QDir{QString::fromUtf8(DPE_PROJECT_TEMPLATES_ROOT)}
        .filePath(QStringLiteral("Minimal2D/DragonPixelTemplate.json"));
}

QString create_project(QTemporaryDir& temp)
{
    const auto destination = QDir{temp.path()}.filePath(QStringLiteral("Project"));
    ProjectLifecycleService lifecycle{QDir{temp.path()}.filePath(QStringLiteral("recent.ini"))};
    const auto result = lifecycle.create_project({
        template_path(), QStringLiteral("Tile Import Test"), destination});
    return result.succeeded ? result.project_manifest_path : QString{};
}

bool write_bytes(const QString& path, const QByteArray& bytes)
{
    QFile output{path};
    return output.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && output.write(bytes) == bytes.size();
}

QString create_tiled_source(QTemporaryDir& temp)
{
    const auto source = QDir{temp.path()}.filePath(QStringLiteral("Tiled Source"));
    if (!QDir{}.mkdir(source))
    {
        return {};
    }
    QImage atlas{4, 4, QImage::Format_RGBA8888};
    atlas.fill(qRgba(40, 180, 90, 255));
    if (!atlas.save(QDir{source}.filePath(QStringLiteral("atlas.png")), "PNG"))
    {
        return {};
    }
    const QJsonObject map{
        {QStringLiteral("type"), QStringLiteral("map")},
        {QStringLiteral("orientation"), QStringLiteral("orthogonal")},
        {QStringLiteral("infinite"), false},
        {QStringLiteral("tilewidth"), 2},
        {QStringLiteral("tileheight"), 2},
        {QStringLiteral("width"), 2},
        {QStringLiteral("height"), 1},
        {QStringLiteral("tilesets"), QJsonArray{QJsonObject{
            {QStringLiteral("firstgid"), 1},
            {QStringLiteral("name"), QStringLiteral("Terrain")},
            {QStringLiteral("tilewidth"), 2},
            {QStringLiteral("tileheight"), 2},
            {QStringLiteral("tilecount"), 4},
            {QStringLiteral("columns"), 2},
            {QStringLiteral("image"), QStringLiteral("atlas.png")},
            {QStringLiteral("imagewidth"), 4},
            {QStringLiteral("imageheight"), 4}}}},
        {QStringLiteral("layers"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), 1},
            {QStringLiteral("name"), QStringLiteral("Ground")},
            {QStringLiteral("type"), QStringLiteral("tilelayer")},
            {QStringLiteral("visible"), true},
            {QStringLiteral("width"), 2},
            {QStringLiteral("height"), 1},
            {QStringLiteral("data"), QJsonArray{1, 4}}}}},
    };
    const auto path = QDir{source}.filePath(QStringLiteral("level.tmj"));
    return write_bytes(path, QJsonDocument{map}.toJson(QJsonDocument::Indented))
        ? path : QString{};
}

TileImportService::IdProvider fixed_ids()
{
    auto values = QStringList{
        QStringLiteral("20000000-0000-4000-8000-000000000001"),
        QStringLiteral("20000000-0000-4000-8000-000000000002"),
        QStringLiteral("20000000-0000-4000-8000-000000000003"),
    };
    return [values = std::move(values)]() mutable {
        return values.takeFirst();
    };
}

QJsonObject read_object(const QString& path)
{
    QFile input{path};
    if (!input.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(input.readAll()).object();
}
} // namespace

class TileImportServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void imports_through_real_worker_and_publishes_assets()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        const auto source = create_tiled_source(temp);
        QVERIFY(!manifest.isEmpty());
        QVERIFY(!source.isEmpty());

        AssetService assets;
        TileImportService importer{assets, fixed_ids()};
        const auto result = importer.import_tiled_json({
            manifest, source, QStringLiteral("Forest Level"), 2.0, 30'000, {}});
        QVERIFY2(result.succeeded, result.diagnostics.isEmpty()
            ? "unknown tile import failure"
            : qPrintable(result.diagnostics.constFirst().message));
        QCOMPARE(result.tile_count, 4);
        QCOMPARE(result.layer_count, 1);
        QCOMPARE(result.cell_count, 2);
        QVERIFY(QFileInfo{result.tilemap_path}.isFile());
        QVERIFY(QFileInfo{result.tileset_path}.isFile());
        QVERIFY(QFileInfo{result.texture_path}.isFile());

        const auto indexed = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(indexed.succeeded());
        const auto* map = indexed.candidate->find_by_id(result.tilemap_asset_id);
        const auto* set = indexed.candidate->find_by_id(result.tileset_asset_id);
        const auto* texture = indexed.candidate->find_by_id(result.texture_asset_id);
        QVERIFY(map != nullptr && set != nullptr && texture != nullptr);
        QCOMPARE(map->dependencies, QStringList{result.tileset_asset_id});
        QCOMPARE(set->dependencies, QStringList{result.texture_asset_id});
        QCOMPARE(texture->source_ownership, QStringLiteral("copied"));
    }

    void contains_and_rejects_a_tampered_result_envelope()
    {
        for (const auto tamper_identity : {false, true})
        {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            const auto manifest = create_project(temp);
            const auto source = create_tiled_source(temp);
            QVERIFY(!manifest.isEmpty());
            QVERIFY(!source.isEmpty());
            AssetService assets;
            const TileImportService::WorkerRunner tampered = [tamper_identity](
                const QString&, const QStringList& arguments, int,
                const std::function<bool()>&) {
                const auto request = read_object(arguments.at(1));
                const auto staging = request.value(QStringLiteral("stagingDirectory")).toString();
                const auto ids = request.value(QStringLiteral("assetIds")).toObject();
                const QJsonObject result{
                    {QStringLiteral("format"), QStringLiteral("dpe.tile-import.result")},
                    {QStringLiteral("formatVersion"), 1},
                    {QStringLiteral("importer"), QStringLiteral("dragonpixel.tiled-json")},
                    {QStringLiteral("succeeded"), true},
                    {QStringLiteral("outputs"), QJsonArray{
                        QJsonObject{{QStringLiteral("role"), QStringLiteral("tilemap")},
                            {QStringLiteral("path"), tamper_identity
                                ? QStringLiteral("tilemap.dpetilemap")
                                : QStringLiteral("../escape.dpetilemap")},
                            {QStringLiteral("assetId"), tamper_identity
                                ? QJsonValue{QStringLiteral("30000000-0000-4000-8000-000000000099")}
                                : ids.value(QStringLiteral("tilemap"))}},
                        QJsonObject{{QStringLiteral("role"), QStringLiteral("tileset")},
                            {QStringLiteral("path"), QStringLiteral("tileset.dpetileset")},
                            {QStringLiteral("assetId"), ids.value(QStringLiteral("tileset"))}},
                        QJsonObject{{QStringLiteral("role"), QStringLiteral("texture")},
                            {QStringLiteral("path"), QStringLiteral("texture.png")},
                            {QStringLiteral("assetId"), ids.value(QStringLiteral("texture"))}}}},
                    {QStringLiteral("diagnostics"), QJsonArray{}},
                    {QStringLiteral("statistics"), QJsonObject{
                        {QStringLiteral("tiles"), 1},
                        {QStringLiteral("layers"), 1},
                        {QStringLiteral("cells"), 1}}},
                };
                write_bytes(QDir{staging}.filePath(QStringLiteral("result.json")),
                    QJsonDocument{result}.toJson(QJsonDocument::Indented));
                return TileImportWorkerOutcome{TileImportWorkerStatus::completed, 0, {}};
            };
            TileImportService importer{assets, fixed_ids(), tampered};
            const auto result = importer.import_tiled_json({
                manifest, source, QStringLiteral("Tampered"), 2.0, 30'000, {}});
            QVERIFY(!result.succeeded);
            QCOMPARE(result.diagnostics.constFirst().code,
                QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"));
            const auto indexed = ProjectIndexService{}.build_candidate(manifest);
            QVERIFY(indexed.succeeded());
            QVERIFY(indexed.candidate->find_by_id(result.tilemap_asset_id) == nullptr);
        }
    }

    void maps_worker_containment_failures_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("code");
        QTest::newRow("start") << static_cast<int>(TileImportWorkerStatus::failed_to_start)
                                << QStringLiteral("DPE-TILE-IMPORT-WORKER-START");
        QTest::newRow("crash") << static_cast<int>(TileImportWorkerStatus::crashed)
                                << QStringLiteral("DPE-TILE-IMPORT-WORKER-CRASH");
        QTest::newRow("timeout") << static_cast<int>(TileImportWorkerStatus::timed_out)
                                  << QStringLiteral("DPE-TILE-IMPORT-TIMEOUT");
        QTest::newRow("cancel") << static_cast<int>(TileImportWorkerStatus::cancelled)
                                 << QStringLiteral("DPE-TILE-IMPORT-CANCELLED");
    }

    void maps_worker_containment_failures()
    {
        QFETCH(int, status);
        QFETCH(QString, code);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        const auto source = create_tiled_source(temp);
        AssetService assets;
        const TileImportService::WorkerRunner runner = [status](
            const QString&, const QStringList&, int, const std::function<bool()>&) {
            return TileImportWorkerOutcome{
                static_cast<TileImportWorkerStatus>(status), -1, QStringLiteral("test failure")};
        };
        TileImportService importer{assets, fixed_ids(), runner};
        const auto result = importer.import_tiled_json({
            manifest, source, QStringLiteral("Failure"), 2.0, 30'000, {}});
        QVERIFY(!result.succeeded);
        QCOMPARE(result.diagnostics.constFirst().code, code);
        QVERIFY(ProjectIndexService{}.build_candidate(manifest).succeeded());
    }
};

QTEST_GUILESS_MAIN(TileImportServiceTests)

#include "TileImportServiceTests.moc"
