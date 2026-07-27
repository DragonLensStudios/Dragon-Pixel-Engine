#include "AssetService.h"

#include "ProjectIndexService.h"
#include "ProjectLifecycleService.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
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
        template_path(), QStringLiteral("Asset Test"), destination});
    return result.succeeded ? result.project_manifest_path : QString{};
}

bool write_image(const QString& path, const char* format)
{
    QImage image{4, 3, QImage::Format_RGBA8888};
    image.fill(qRgba(240, 80, 20, 255));
    return image.save(path, format);
}

QByteArray read_bytes(const QString& path)
{
    QFile input{path};
    return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray{};
}

QJsonObject read_object(const QString& path)
{
    return QJsonDocument::fromJson(read_bytes(path)).object();
}

bool write_object(const QString& path, const QJsonObject& object)
{
    QFile output{path};
    return output.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && output.write(QJsonDocument{object}.toJson(QJsonDocument::Indented)) > 0;
}
} // namespace

class AssetServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void imports_png_copy_and_generic_with_asset_v3()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        QVERIFY(!manifest.isEmpty());
        const auto png = QDir{temp.path()}.filePath(QStringLiteral("hero.png"));
        QVERIFY(write_image(png, "PNG"));
        const auto generic = QDir{temp.path()}.filePath(QStringLiteral("notes.unknown"));
        QFile generic_file{generic};
        QVERIFY(generic_file.open(QIODevice::WriteOnly));
        QCOMPARE(generic_file.write("opaque generic bytes"), 20);
        generic_file.close();

        AssetService service;
        const auto result = service.import_files({
            manifest, {png, generic}, QStringLiteral("Assets"),
            AssetImportOwnership::copy_into_project});
        QVERIFY2(result.succeeded, result.diagnostics.isEmpty()
            ? "unknown import failure" : qPrintable(result.diagnostics.constFirst().message));
        QCOMPARE(result.asset_ids.size(), 2);
        QCOMPARE(result.metadata_paths.size(), 2);

        const auto indexed = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(indexed.succeeded());
        const auto* sprite = indexed.candidate->find_by_id(result.asset_ids.at(0));
        const auto* opaque = indexed.candidate->find_by_id(result.asset_ids.at(1));
        QVERIFY(sprite != nullptr && opaque != nullptr);
        QCOMPARE(sprite->format_version, 3);
        QCOMPARE(sprite->asset_type, QStringLiteral("sprite"));
        QCOMPARE(sprite->source_ownership, QStringLiteral("copied"));
        QCOMPARE(opaque->asset_type, QStringLiteral("generic"));
        QVERIFY(!opaque->document.value(QStringLiteral("importerDiagnostics")).toArray().isEmpty());

        const auto binding = service.runtime_binding(manifest, sprite->id);
        QVERIFY(binding.succeeded);
        QCOMPARE(binding.media_type, QStringLiteral("image/png"));
        QCOMPARE(binding.immutable_bytes, read_bytes(png));
    }

    void links_jpeg_read_only_and_never_mutates_external_bytes()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        QVERIFY(!manifest.isEmpty());
        const auto jpeg = QDir{temp.path()}.filePath(QStringLiteral("outside.jpg"));
        QVERIFY(write_image(jpeg, "JPEG"));
        const auto before = read_bytes(jpeg);

        AssetService service;
        const auto imported = service.import_files({
            manifest, {jpeg}, QStringLiteral("Assets"),
            AssetImportOwnership::link_external_read_only});
        QVERIFY2(imported.succeeded, imported.diagnostics.isEmpty()
            ? "unknown link failure" : qPrintable(imported.diagnostics.constFirst().message));
        const auto indexed = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(indexed.succeeded());
        const auto* entry = indexed.candidate->find_by_id(imported.asset_ids.constFirst());
        QVERIFY(entry != nullptr);
        QCOMPARE(entry->source_ownership, QStringLiteral("linked"));
        QCOMPARE(entry->resolved_source_path, QFileInfo{jpeg}.canonicalFilePath());
        QVERIFY(service.runtime_binding(manifest, entry->id).succeeded);

        const auto trashed = service.trash_asset(manifest, entry->id);
        QVERIFY(trashed.succeeded);
        QCOMPARE(read_bytes(jpeg), before);
        const auto restored = service.restore_trash(manifest, trashed.operation_id);
        QVERIFY(restored.succeeded);
        QCOMPARE(read_bytes(jpeg), before);
        QVERIFY(ProjectIndexService{}.build_candidate(manifest).succeeded());
    }

    void rename_move_and_duplicate_preserve_or_allocate_identity()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        const auto png = QDir{temp.path()}.filePath(QStringLiteral("tree.png"));
        QVERIFY(write_image(png, "PNG"));
        AssetService service;
        const auto imported = service.import_files({manifest, {png}});
        QVERIFY(imported.succeeded);
        const auto stable_id = imported.asset_ids.constFirst();

        const auto renamed = service.rename_asset(manifest, stable_id, QStringLiteral("Oak Tree"));
        QVERIFY2(renamed.succeeded, renamed.diagnostics.isEmpty()
            ? "unknown rename failure" : qPrintable(renamed.diagnostics.constFirst().message));
        QCOMPARE(renamed.asset_ids, QStringList{stable_id});
        QVERIFY(QFileInfo{QDir{QFileInfo{manifest}.absolutePath()}
            .filePath(QStringLiteral("Assets/Oak Tree.png"))}.isFile());

        const auto folder = service.create_folder(manifest, QStringLiteral("Assets/Nature"));
        QVERIFY(folder.succeeded);
        const auto moved = service.move_asset(manifest, stable_id, QStringLiteral("Assets/Nature"));
        QVERIFY2(moved.succeeded, moved.diagnostics.isEmpty()
            ? "unknown move failure" : qPrintable(moved.diagnostics.constFirst().message));
        QCOMPARE(moved.asset_ids, QStringList{stable_id});

        const auto duplicated = service.duplicate_asset(manifest, stable_id);
        QVERIFY2(duplicated.succeeded, duplicated.diagnostics.isEmpty()
            ? "unknown duplicate failure" : qPrintable(duplicated.diagnostics.constFirst().message));
        QCOMPARE(duplicated.asset_ids.size(), 1);
        QVERIFY(duplicated.asset_ids.constFirst() != stable_id);
        const auto indexed = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(indexed.succeeded());
        QVERIFY(indexed.candidate->find_by_id(stable_id) != nullptr);
        QVERIFY(indexed.candidate->find_by_id(duplicated.asset_ids.constFirst()) != nullptr);
    }

    void moves_asset_folders_atomically_without_changing_asset_identity()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        const auto png = QDir{temp.path()}.filePath(QStringLiteral("folder-sprite.png"));
        QVERIFY(write_image(png, "PNG"));
        AssetService service;
        QVERIFY(service.create_folder(manifest, QStringLiteral("Assets/Source")).succeeded);
        QVERIFY(service.create_folder(manifest, QStringLiteral("Assets/Source/Empty")).succeeded);
        QVERIFY(service.create_folder(manifest, QStringLiteral("Assets/Destination")).succeeded);
        const auto imported = service.import_files({
            manifest, {png}, QStringLiteral("Assets/Source"),
            AssetImportOwnership::copy_into_project});
        QVERIFY(imported.succeeded);
        const auto stable_id = imported.asset_ids.constFirst();
        const auto before = service.runtime_binding(manifest, stable_id);
        QVERIFY(before.succeeded);

        const auto moved = service.move_folder(
            manifest, QStringLiteral("Assets/Source"), QStringLiteral("Assets/Destination"));
        QVERIFY2(moved.succeeded, moved.diagnostics.isEmpty()
            ? "unknown folder move failure" : qPrintable(moved.diagnostics.constFirst().message));
        const auto project_root = QFileInfo{manifest}.absolutePath();
        QVERIFY(!QFileInfo::exists(QDir{project_root}.filePath(QStringLiteral("Assets/Source"))));
        QVERIFY(QFileInfo{QDir{project_root}.filePath(
            QStringLiteral("Assets/Destination/Source/Empty"))}.isDir());
        const auto indexed = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(indexed.succeeded());
        const auto* entry = indexed.candidate->find_by_id(stable_id);
        QVERIFY(entry != nullptr);
        QVERIFY(entry->resolved_source_path.contains(QStringLiteral("Assets/Destination/Source")));
        const auto after = service.runtime_binding(manifest, stable_id);
        QVERIFY(after.succeeded);
        QCOMPARE(after.immutable_bytes, before.immutable_bytes);

        const auto root_move = service.move_folder(
            manifest, QStringLiteral("Assets"), QStringLiteral("Assets/Destination"));
        QVERIFY(!root_move.succeeded);
        QCOMPARE(root_move.diagnostics.constFirst().code,
            QStringLiteral("DPE-ASSET-FOLDER-MOVE-ROOT"));
        const auto cycle = service.move_folder(manifest,
            QStringLiteral("Assets/Destination"),
            QStringLiteral("Assets/Destination/Source/Empty"));
        QVERIFY(!cycle.succeeded);
        QCOMPARE(cycle.diagnostics.constFirst().code,
            QStringLiteral("DPE-ASSET-FOLDER-MOVE-CYCLE"));
    }

    void trash_restore_round_trips_exact_owned_bytes()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        const auto png = QDir{temp.path()}.filePath(QStringLiteral("icon.png"));
        QVERIFY(write_image(png, "PNG"));
        AssetService service;
        const auto imported = service.import_files({manifest, {png}});
        QVERIFY(imported.succeeded);
        const auto indexed = ProjectIndexService{}.build_candidate(manifest);
        const auto* entry = indexed.candidate->find_by_id(imported.asset_ids.constFirst());
        QVERIFY(entry != nullptr);
        const auto source_path = entry->resolved_source_path;
        const auto metadata_path = entry->absolute_path;
        const auto source_before = read_bytes(source_path);
        const auto metadata_before = read_bytes(metadata_path);

        const auto trashed = service.trash_asset(manifest, entry->id);
        QVERIFY(trashed.succeeded);
        QVERIFY(!QFileInfo::exists(source_path));
        QVERIFY(!QFileInfo::exists(metadata_path));
        QVERIFY(QFileInfo{QDir{QFileInfo{manifest}.absolutePath()}.filePath(
            QStringLiteral(".dragonpixel/Trash/%1/recovery.json").arg(trashed.operation_id))}.isFile());

        const auto restored = service.restore_trash(manifest, trashed.operation_id);
        QVERIFY(restored.succeeded);
        QCOMPARE(read_bytes(source_path), source_before);
        QCOMPARE(read_bytes(metadata_path), metadata_before);
        QVERIFY(ProjectIndexService{}.build_candidate(manifest).succeeded());
    }

    void dependency_impact_and_runtime_hash_changes_are_explicit()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        const auto one = QDir{temp.path()}.filePath(QStringLiteral("one.png"));
        const auto two = QDir{temp.path()}.filePath(QStringLiteral("two.png"));
        QVERIFY(write_image(one, "PNG"));
        QVERIFY(write_image(two, "PNG"));
        AssetService service;
        const auto imported = service.import_files({manifest, {one, two}});
        QVERIFY(imported.succeeded);
        auto dependent = read_object(imported.metadata_paths.at(1));
        dependent.insert(QStringLiteral("dependencies"),
            QJsonArray{imported.asset_ids.at(0)});
        QVERIFY(write_object(imported.metadata_paths.at(1), dependent));
        const auto impact = service.dependency_impact(manifest, imported.asset_ids.at(0));
        QCOMPARE(impact.dependent_ids, QStringList{imported.asset_ids.at(1)});

        const auto indexed = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(indexed.succeeded());
        const auto* entry = indexed.candidate->find_by_id(imported.asset_ids.at(0));
        QVERIFY(entry != nullptr);
        QFile changed{entry->resolved_source_path};
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write("changed"), 7);
        changed.close();
        const auto binding = service.runtime_binding(manifest, entry->id);
        QVERIFY(!binding.succeeded);
        QVERIFY(!binding.diagnostics.isEmpty());
        QCOMPARE(binding.diagnostics.constFirst().code, QStringLiteral("DPE-ASSET-RUNTIME-HASH"));
    }

    void rejects_case_folded_collisions_without_overwriting()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto manifest = create_project(temp);
        const auto upper = QDir{temp.path()}.filePath(QStringLiteral("Hero.png"));
        const auto lower_dir = QDir{temp.path()}.filePath(QStringLiteral("external"));
        QVERIFY(QDir{}.mkdir(lower_dir));
        const auto lower = QDir{lower_dir}.filePath(QStringLiteral("hero.png"));
        QVERIFY(write_image(upper, "PNG"));
        QVERIFY(write_image(lower, "PNG"));
        AssetService service;
        QVERIFY(service.import_files({manifest, {upper}}).succeeded);
        const auto second = service.import_files({manifest, {lower}});
        QVERIFY(!second.succeeded);
        QVERIFY(!second.diagnostics.isEmpty());
        QCOMPARE(second.diagnostics.constFirst().code,
            QStringLiteral("DPE-ASSET-IMPORT-COLLISION"));
    }
};

QTEST_MAIN(AssetServiceTests)

#include "AssetServiceTests.moc"
