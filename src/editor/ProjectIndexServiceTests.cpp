#include "ProjectIndexService.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

namespace
{
constexpr auto project_id = "11111111-1111-4111-8111-111111111111";
constexpr auto scene_id = "22222222-2222-4222-8222-222222222222";
constexpr auto asset_a_id = "33333333-3333-4333-8333-333333333333";
constexpr auto asset_b_id = "44444444-4444-4444-8444-444444444444";
constexpr auto prefab_id = "55555555-5555-4555-8555-555555555555";
constexpr auto missing_id = "66666666-6666-4666-8666-666666666666";

bool write_bytes(const QString& path, const QByteArray& bytes)
{
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath()))
    {
        return false;
    }
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

bool write_json(const QString& path, const QJsonObject& object)
{
    return write_bytes(path, QJsonDocument{object}.toJson(QJsonDocument::Indented));
}

QJsonObject project_document(int version = 2)
{
    QJsonObject result{
        {QStringLiteral("$schema"),
         QStringLiteral("https://dragonpixel.dev/schemas/v%1/project.schema.json").arg(version)},
        {QStringLiteral("format"), QStringLiteral("dpe.project")},
        {QStringLiteral("formatVersion"), version},
        {QStringLiteral("engineVersion"), QStringLiteral("test")},
        {QStringLiteral("projectId"), QString::fromLatin1(project_id)},
        {QStringLiteral("name"), QStringLiteral("Index Test")},
        {QStringLiteral("startupScene"), QStringLiteral("Scenes/Main.dpescene")},
        {QStringLiteral("assetRoots"), QJsonArray{QStringLiteral("Assets")}},
    };
    if (version == 2)
    {
        result.insert(QStringLiteral("sceneRoots"), QJsonArray{QStringLiteral("Scenes")});
    }
    return result;
}

QJsonObject scene_document(const QString& dependency = {})
{
    QJsonArray instances;
    if (!dependency.isEmpty())
    {
        instances.push_back(QJsonObject{
            {QStringLiteral("instanceId"), QStringLiteral("77777777-7777-4777-8777-777777777777")},
            {QStringLiteral("sourceAssetId"), dependency},
        });
    }
    return QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v3/scene.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.scene")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("engineVersion"), QStringLiteral("test")},
        {QStringLiteral("sceneId"), QString::fromLatin1(scene_id)},
        {QStringLiteral("name"), QStringLiteral("Main")},
        {QStringLiteral("entities"), QJsonArray{}},
        {QStringLiteral("prefabInstances"), instances},
    };
}

QJsonObject asset_document(
    const QString& id,
    const QString& source,
    const QStringList& dependencies = {})
{
    QJsonArray dependency_array;
    for (const auto& dependency : dependencies)
    {
        dependency_array.push_back(dependency);
    }
    return QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v2/asset-metadata.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.asset")},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("engineVersion"), QStringLiteral("test")},
        {QStringLiteral("assetId"), id},
        {QStringLiteral("assetType"), QStringLiteral("sprite")},
        {QStringLiteral("source"), source},
        {QStringLiteral("dependencies"), dependency_array},
        {QStringLiteral("importSettings"), QJsonObject{}},
    };
}

QJsonObject prefab_document(const QStringList& dependencies = {})
{
    QJsonArray dependency_array;
    for (const auto& dependency : dependencies)
    {
        dependency_array.push_back(dependency);
    }
    return QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v1/prefab.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.prefab")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("engineVersion"), QStringLiteral("test")},
        {QStringLiteral("prefabId"), QString::fromLatin1(prefab_id)},
        {QStringLiteral("revision"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("rootEntityId"), QStringLiteral("88888888-8888-4888-8888-888888888888")},
        {QStringLiteral("entities"), QJsonArray{}},
        {QStringLiteral("prefabInstances"), QJsonArray{}},
        {QStringLiteral("dependencies"), dependency_array},
    };
}

QByteArray tree_fingerprint(const QString& root)
{
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QStringList paths;
    QDirIterator iterator{root, QDir::Files, QDirIterator::Subdirectories};
    while (iterator.hasNext())
    {
        paths.push_back(iterator.next());
    }
    paths.sort();
    for (const auto& path : paths)
    {
        QFile file{path};
        if (file.open(QIODevice::ReadOnly))
        {
            hash.addData(QDir{root}.relativeFilePath(path).toUtf8());
            hash.addData(QByteArrayView{"\0", 1});
            hash.addData(file.readAll());
            hash.addData(QByteArrayView{"\0", 1});
        }
    }
    return hash.result();
}

bool has_code(const ProjectIndexBuildResult& result, ProjectIndexDiagnosticCode code)
{
    return std::any_of(result.diagnostics.cbegin(), result.diagnostics.cend(), [code](const auto& item) {
        return item.code == code;
    });
}
}

class ProjectIndexServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void builds_complete_v2_candidate_without_writes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        QVERIFY(write_json(root.filePath(QStringLiteral("DragonPixelProject.json")), project_document()));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Scenes/Main.dpescene")),
            scene_document(QString::fromLatin1(prefab_id))));
        QVERIFY(write_bytes(root.filePath(QStringLiteral("Assets/a.bin")), QByteArrayLiteral("a")));
        QVERIFY(write_bytes(root.filePath(QStringLiteral("Assets/b.bin")), QByteArrayLiteral("b")));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/A.dpeasset")),
            asset_document(
                QString::fromLatin1(asset_a_id),
                QStringLiteral("a.bin"),
                {QString::fromLatin1(asset_b_id)})));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/B.dpeasset")),
            asset_document(QString::fromLatin1(asset_b_id), QStringLiteral("b.bin"))));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/Linked.dpeprefab")),
            prefab_document({QString::fromLatin1(asset_a_id)})));

        const auto before = tree_fingerprint(directory.path());
        const ProjectIndexService service;
        const auto result = service.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        const auto after = tree_fingerprint(directory.path());

        QVERIFY2(result.succeeded(), result.diagnostics.isEmpty()
            ? "candidate unexpectedly failed"
            : qPrintable(result.diagnostics.constFirst().message));
        QVERIFY(result.candidate.has_value());
        QCOMPARE(result.candidate->format_version, 2);
        QCOMPARE(result.candidate->entries.size(), 4);
        QCOMPARE(result.candidate->roots.size(), 2);
        QCOMPARE(result.candidate->find_by_id(QString::fromLatin1(asset_a_id))->asset_type,
            QStringLiteral("sprite"));
        QCOMPARE(result.candidate->find_by_id(QString::fromLatin1(prefab_id))->kind,
            ProjectIndexEntryKind::prefab);
        QCOMPARE(result.candidate->find_by_path(result.candidate->startup_scene_path)->kind,
            ProjectIndexEntryKind::scene);
        QCOMPARE(before, after);
    }

    void supports_v1_project_and_asset_documents()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document(1);
        QVERIFY(write_json(root.filePath(QStringLiteral("DragonPixelProject.json")), project));
        QVERIFY(write_json(root.filePath(QStringLiteral("Scenes/Main.dpescene")), scene_document()));
        auto asset = asset_document(
            QString::fromLatin1(asset_a_id), QStringLiteral("builtin://unit-sprite"));
        asset.insert(QStringLiteral("$schema"),
            QStringLiteral("https://dragonpixel.dev/schemas/v1/asset-metadata.schema.json"));
        asset.insert(QStringLiteral("formatVersion"), 1);
        asset.remove(QStringLiteral("dependencies"));
        asset.remove(QStringLiteral("importSettings"));
        QVERIFY(write_json(root.filePath(QStringLiteral("Assets/A.dpeasset")), asset));

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY2(result.succeeded(), result.diagnostics.isEmpty()
            ? "candidate unexpectedly failed"
            : qPrintable(result.diagnostics.constFirst().message));
        QVERIFY(result.candidate.has_value());
        QCOMPARE(result.candidate->format_version, 1);
        QCOMPARE(result.candidate->entries.size(), 2);
        QCOMPARE(result.candidate->find_by_id(QString::fromLatin1(asset_a_id))->format_version, 1);
    }

    void reports_all_index_integrity_failures_in_one_candidate()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document();
        project.insert(
            QStringLiteral("assetRoots"),
            QJsonArray{QStringLiteral("Assets"), QStringLiteral("../Outside")});
        QVERIFY(write_json(root.filePath(QStringLiteral("DragonPixelProject.json")), project));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Scenes/Main.dpescene")),
            scene_document(QString::fromLatin1(missing_id))));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/A.dpeasset")),
            asset_document(
                QString::fromLatin1(asset_a_id),
                QStringLiteral("missing-a.bin"),
                {QString::fromLatin1(asset_b_id)})));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/B.dpeasset")),
            asset_document(
                QString::fromLatin1(asset_b_id),
                QStringLiteral("builtin://b"),
                {QString::fromLatin1(asset_a_id)})));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/Duplicate.dpeasset")),
            asset_document(QString::fromLatin1(asset_a_id), QStringLiteral("builtin://duplicate"))));

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(!result.succeeded());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsorted_roots));
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::missing_asset_source));
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::duplicate_identifier));
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::missing_dependency));
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::dependency_cycle));
    }

    void rejects_manifest_before_candidate_when_json_is_invalid()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        const auto manifest = root.filePath(QStringLiteral("DragonPixelProject.json"));
        QVERIFY(write_bytes(manifest, QByteArrayLiteral("not-json")));

        const auto result = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(!result.candidate.has_value());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::invalid_json));
    }
};

QTEST_APPLESS_MAIN(ProjectIndexServiceTests)

#include "ProjectIndexServiceTests.moc"
