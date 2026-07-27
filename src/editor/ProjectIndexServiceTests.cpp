#include "ProjectIndexService.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>
#include <filesystem>

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
    if (version >= 2)
    {
        result.insert(QStringLiteral("sceneRoots"), QJsonArray{QStringLiteral("Scenes")});
    }
    if (version == 3)
    {
        result.insert(QStringLiteral("componentRoots"), QJsonArray{});
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

std::filesystem::path filesystem_path(const QString& value)
{
#ifdef Q_OS_WIN
    return std::filesystem::path{value.toStdWString()};
#else
    return std::filesystem::path{value.toStdString()};
#endif
}

bool create_directory_symlink(const QString& target, const QString& link)
{
    std::error_code error;
    std::filesystem::create_directory_symlink(
        filesystem_path(target), filesystem_path(link), error);
    return !error;
}

bool create_file_symlink(const QString& target, const QString& link)
{
    std::error_code error;
    std::filesystem::create_symlink(
        filesystem_path(target), filesystem_path(link), error);
    return !error;
}

bool remove_filesystem_link(const QString& link)
{
    std::error_code error;
    const auto removed = std::filesystem::remove(filesystem_path(link), error);
    return removed && !error;
}

bool write_minimal_project_tree(const QDir& root, const QJsonObject& project)
{
    return write_json(root.filePath(QStringLiteral("DragonPixelProject.json")), project)
        && write_json(root.filePath(QStringLiteral("Scenes/Main.dpescene")), scene_document())
        && QDir{}.mkpath(root.filePath(QStringLiteral("Assets")));
}
}

class ProjectIndexServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void default_sample_indexes_input_map_asset()
    {
        const auto result = ProjectIndexService{}.build_candidate(
            QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT));
        QStringList messages;
        for (const auto& diagnostic : result.diagnostics)
        {
            messages.push_back(QStringLiteral("%1 %2 %3")
                .arg(project_index_diagnostic_code_name(diagnostic.code),
                    diagnostic.document_path, diagnostic.message));
        }
        QVERIFY2(result.succeeded(), qPrintable(messages.join(QLatin1Char('\n'))));
        const auto iterator = std::find_if(
            result.candidate->entries.cbegin(), result.candidate->entries.cend(),
            [](const ProjectIndexEntry& entry) {
                return entry.kind == ProjectIndexEntryKind::asset
                    && entry.asset_type == QStringLiteral("input-map");
            });
        QVERIFY(iterator != result.candidate->entries.cend());
        QVERIFY(iterator->structurally_valid);
        QVERIFY(iterator->resolved_source_path.endsWith(QStringLiteral("DefaultGameplay.dpeinputmap")));
    }

    void builds_complete_v2_candidate_without_writes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document();
        const QJsonObject preserved_extension{
            {QStringLiteral("enabled"), true},
            {QStringLiteral("nested"), QJsonObject{{QStringLiteral("value"), 42}}},
        };
        project.insert(QStringLiteral("futureExtension"), preserved_extension);
        QVERIFY(write_json(root.filePath(QStringLiteral("DragonPixelProject.json")), project));
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
        QCOMPARE(result.candidate->source_format_version, 2);
        QCOMPARE(result.candidate->format_version, 4);
        QCOMPARE(result.candidate->migrations.size(), 2);
        QCOMPARE(result.candidate->migrations.constFirst().source_format_version, 2);
        QCOMPARE(result.candidate->migrations.constFirst().target_format_version, 3);
        QCOMPARE(result.candidate->migrations.constLast().source_format_version, 3);
        QCOMPARE(result.candidate->migrations.constLast().target_format_version, 4);
        QCOMPARE(result.candidate->manifest.value(QStringLiteral("formatVersion")).toInt(), 4);
        QCOMPARE(
            result.candidate->manifest.value(QStringLiteral("$schema")).toString(),
            QStringLiteral("https://dragonpixel.dev/schemas/v4/project.schema.json"));
        QVERIFY(result.candidate->manifest.value(QStringLiteral("componentRoots")).toArray().isEmpty());
        QCOMPARE(
            result.candidate->manifest.value(QStringLiteral("futureExtension")).toObject(),
            preserved_extension);
        QCOMPARE(result.candidate->entries.size(), 4);
        QCOMPARE(result.candidate->roots.size(), 2);
        QCOMPARE(result.candidate->find_by_id(QString::fromLatin1(asset_a_id))->asset_type,
            QStringLiteral("sprite"));
        QCOMPARE(result.candidate->find_by_id(QString::fromLatin1(prefab_id))->kind,
            ProjectIndexEntryKind::prefab);
        QCOMPARE(result.candidate->find_by_path(result.candidate->startup_scene_path)->kind,
            ProjectIndexEntryKind::scene);
        QCOMPARE(before, after);

        const auto repeated = service.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(repeated.succeeded());
        QCOMPARE(
            result.candidate->canonical_manifest_json(),
            repeated.candidate->canonical_manifest_json());
        QVERIFY(result.candidate->canonical_manifest_json().endsWith('\n'));
    }

    void migrates_v2_components_directory_deterministically()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document();
        project.insert(
            QStringLiteral("futureExtension"),
            QJsonObject{{QStringLiteral("preserved"), QStringLiteral("yes")}});
        QVERIFY(write_minimal_project_tree(root, project));
        QVERIFY(QDir{}.mkpath(root.filePath(QStringLiteral("Components"))));

        const auto before = tree_fingerprint(directory.path());
        const ProjectIndexService service;
        const auto first = service.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        const auto second = service.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        const auto after = tree_fingerprint(directory.path());

        QVERIFY2(first.succeeded(), first.diagnostics.isEmpty()
            ? "candidate unexpectedly failed"
            : qPrintable(first.diagnostics.constFirst().message));
        QVERIFY(second.succeeded());
        QCOMPARE(first.candidate->source_format_version, 2);
        QCOMPARE(first.candidate->format_version, 4);
        QCOMPARE(first.candidate->migrations.size(), 2);
        const auto component_roots = first.candidate->manifest
                                         .value(QStringLiteral("componentRoots"))
                                         .toArray();
        QCOMPARE(component_roots.size(), 1);
        QCOMPARE(component_roots.at(0).toString(), QStringLiteral("Components"));
        QCOMPARE(first.candidate->roots.size(), 3);
        QCOMPARE(
            static_cast<int>(std::count_if(
                first.candidate->roots.cbegin(), first.candidate->roots.cend(), [](const auto& item) {
                    return item.kind == ProjectIndexRootKind::components
                        && item.declared_path == QStringLiteral("Components");
                })),
            1);
        QCOMPARE(
            first.candidate->manifest.value(QStringLiteral("futureExtension")).toObject(),
            project.value(QStringLiteral("futureExtension")).toObject());
        QCOMPARE(first.candidate->canonical_manifest_json(), second.candidate->canonical_manifest_json());
        QCOMPARE(before, after);
    }

    void indexes_contained_component_sources_and_metadata_without_reading_code_as_json()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document(3);
        project.insert(
            QStringLiteral("componentRoots"), QJsonArray{QStringLiteral("Components")});
        QVERIFY(write_minimal_project_tree(root, project));
        const auto managed = root.filePath(QStringLiteral("Components/CSharp/Managed Mover.cs"));
        const auto native = root.filePath(QStringLiteral("Components/Native/Spinner.cpp"));
        const auto header = root.filePath(QStringLiteral("Components/Native/Spinner.hpp"));
        const auto metadata = root.filePath(QStringLiteral("Components/Game.dpecomponents"));
        QVERIFY(write_bytes(managed, QByteArrayLiteral("this is source, not json\n")));
        QVERIFY(write_bytes(native, QByteArrayLiteral("void update() {}\n")));
        QVERIFY(write_bytes(header, QByteArrayLiteral("#pragma once\n")));
        QVERIFY(write_bytes(metadata, QByteArrayLiteral("{\"format\":\"dpe.component-metadata\"}\n")));

        const auto before = tree_fingerprint(directory.path());
        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        const auto after = tree_fingerprint(directory.path());
        QVERIFY2(result.succeeded(), result.diagnostics.isEmpty()
            ? "candidate unexpectedly failed"
            : qPrintable(result.diagnostics.constFirst().message));
        QCOMPARE(before, after);

        const auto* managed_entry = result.candidate->find_by_path(managed);
        const auto* native_entry = result.candidate->find_by_path(native);
        const auto* header_entry = result.candidate->find_by_path(header);
        const auto* manifest_entry = result.candidate->find_by_path(metadata);
        QVERIFY(managed_entry != nullptr && native_entry != nullptr
            && header_entry != nullptr && manifest_entry != nullptr);
        QCOMPARE(managed_entry->kind, ProjectIndexEntryKind::component_source);
        QCOMPARE(managed_entry->display_name, QStringLiteral("Managed Mover.cs"));
        QCOMPARE(managed_entry->asset_type, QStringLiteral("C# Script"));
        QCOMPARE(native_entry->asset_type, QStringLiteral("C++ Source"));
        QCOMPARE(header_entry->asset_type, QStringLiteral("C++ Header"));
        QCOMPARE(manifest_entry->kind, ProjectIndexEntryKind::component_manifest);
        QVERIFY(managed_entry->id.isEmpty());
        QVERIFY(manifest_entry->id.isEmpty());
    }

    void rejects_ambiguous_v2_component_migration_inputs()
    {
        QTemporaryDir field_directory;
        QVERIFY(field_directory.isValid());
        const QDir field_root{field_directory.path()};
        auto colliding_project = project_document();
        colliding_project.insert(
            QStringLiteral("componentRoots"), QJsonArray{QStringLiteral("LegacyComponents")});
        QVERIFY(write_minimal_project_tree(field_root, colliding_project));

        const auto field_result = ProjectIndexService{}.build_candidate(
            field_root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(field_result.candidate.has_value());
        QVERIFY(field_result.has_errors());
        QVERIFY(has_code(field_result, ProjectIndexDiagnosticCode::invalid_project_format));
        QCOMPARE(field_result.candidate->source_format_version, 2);
        QCOMPARE(field_result.candidate->format_version, 2);
        QVERIFY(field_result.candidate->migrations.isEmpty());
        QCOMPARE(
            field_result.candidate->manifest.value(QStringLiteral("componentRoots")).toArray(),
            colliding_project.value(QStringLiteral("componentRoots")).toArray());

        QTemporaryDir file_directory;
        QVERIFY(file_directory.isValid());
        const QDir file_root{file_directory.path()};
        QVERIFY(write_minimal_project_tree(file_root, project_document()));
        QVERIFY(write_bytes(file_root.filePath(QStringLiteral("Components")), QByteArrayLiteral("not-a-directory")));

        const auto file_result = ProjectIndexService{}.build_candidate(
            file_root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(file_result.candidate.has_value());
        QVERIFY(file_result.has_errors());
        QVERIFY(has_code(file_result, ProjectIndexDiagnosticCode::unsafe_path));
        QVERIFY(file_result.candidate->migrations.isEmpty());
    }

    void rejects_unsafe_component_root_paths()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document(3);
        project.insert(
            QStringLiteral("componentRoots"), QJsonArray{QStringLiteral("../Outside")});
        QVERIFY(write_minimal_project_tree(root, project));

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
    }

    void rejects_v2_components_link_outside_project()
    {
        QTemporaryDir project_directory;
        QTemporaryDir outside_directory;
        QVERIFY(project_directory.isValid());
        QVERIFY(outside_directory.isValid());
        const QDir root{project_directory.path()};
        QVERIFY(write_minimal_project_tree(root, project_document()));
        if (!create_directory_symlink(
                outside_directory.path(), root.filePath(QStringLiteral("Components"))))
        {
            QSKIP("This host does not permit creation of a directory symbolic link.");
        }

        const auto before = tree_fingerprint(project_directory.path());
        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        const auto after = tree_fingerprint(project_directory.path());
        QVERIFY(remove_filesystem_link(root.filePath(QStringLiteral("Components"))));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
        QCOMPARE(result.candidate->format_version, 2);
        QVERIFY(result.candidate->migrations.isEmpty());
        QCOMPARE(before, after);
    }

    void accepts_unicode_and_spaced_contained_paths()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{QDir{directory.path()}.filePath(
            QStringLiteral("Dragon Pixel \u0394 Project"))};
        QVERIFY(QDir{}.mkpath(root.path()));

        auto project = project_document(3);
        project.insert(
            QStringLiteral("startupScene"),
            QStringLiteral("Sc\u00e8nes \u7f8e/Main \u573a\u666f.dpescene"));
        project.insert(
            QStringLiteral("sceneRoots"), QJsonArray{QStringLiteral("Sc\u00e8nes \u7f8e")});
        project.insert(
            QStringLiteral("assetRoots"), QJsonArray{QStringLiteral("Assets \u7a7a \u95f4")});
        project.insert(
            QStringLiteral("componentRoots"), QJsonArray{QStringLiteral("Components C++")});

        QVERIFY(write_json(
            root.filePath(QStringLiteral("DragonPixelProject.json")), project));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Sc\u00e8nes \u7f8e/Main \u573a\u666f.dpescene")),
            scene_document()));
        QVERIFY(write_bytes(
            root.filePath(QStringLiteral("Assets \u7a7a \u95f4/Sprite \u03a9.bin")),
            QByteArrayLiteral("sprite")));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets \u7a7a \u95f4/Texture \u0394.dpeasset")),
            asset_document(
                QString::fromLatin1(asset_a_id), QStringLiteral("Sprite \u03a9.bin"))));
        QVERIFY(QDir{}.mkpath(root.filePath(QStringLiteral("Components C++"))));

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));

        QVERIFY2(result.succeeded(), result.diagnostics.isEmpty()
            ? "candidate unexpectedly failed"
            : qPrintable(result.diagnostics.constFirst().message));
        QVERIFY(result.candidate.has_value());
        QCOMPARE(result.candidate->roots.size(), 3);
        QCOMPARE(result.candidate->entries.size(), 2);
        QVERIFY(result.candidate->startup_scene_path.contains(QStringLiteral("Main \u573a\u666f")));
    }

    void rejects_duplicate_case_alias_declared_roots()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document(3);
        project.insert(
            QStringLiteral("sceneRoots"),
            QJsonArray{QStringLiteral("Scenes"), QStringLiteral("scenes")});
        QVERIFY(write_minimal_project_tree(root, project));

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::duplicate_root));
    }

    void rejects_unicode_normalization_alias_entries()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        QVERIFY(write_minimal_project_tree(root, project_document(3)));
        const auto composed = root.filePath(
            QStringLiteral("Assets/Caf\u00e9 Asset.dpeasset"));
        const auto decomposed = root.filePath(
            QStringLiteral("Assets/Cafe\u0301 Asset.dpeasset"));
        QVERIFY(write_json(
            composed,
            asset_document(QString::fromLatin1(asset_a_id), QStringLiteral("builtin://a"))));
        QVERIFY(write_json(
            decomposed,
            asset_document(QString::fromLatin1(asset_b_id), QStringLiteral("builtin://b"))));
        if (QDir{root.filePath(QStringLiteral("Assets"))}
                .entryList(QStringList{QStringLiteral("*.dpeasset")}, QDir::Files)
                .size()
            != 2)
        {
            QSKIP("This filesystem does not preserve distinct Unicode-normalization aliases.");
        }

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
    }

    void rejects_declared_root_link_outside_project()
    {
        QTemporaryDir project_directory;
        QTemporaryDir outside_directory;
        QVERIFY(project_directory.isValid());
        QVERIFY(outside_directory.isValid());
        const QDir root{project_directory.path()};
        auto project = project_document(3);
        project.insert(
            QStringLiteral("assetRoots"), QJsonArray{QStringLiteral("Linked Assets")});
        QVERIFY(write_json(
            root.filePath(QStringLiteral("DragonPixelProject.json")), project));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Scenes/Main.dpescene")), scene_document()));
        const auto link = root.filePath(QStringLiteral("Linked Assets"));
        if (!create_directory_symlink(outside_directory.path(), link))
        {
            QSKIP("This host does not permit creation of a directory symbolic link.");
        }

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(remove_filesystem_link(link));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
    }

    void rejects_project_root_link_alias()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir parent{directory.path()};
        const QDir real_root{parent.filePath(QStringLiteral("Real Project"))};
        QVERIFY(QDir{}.mkpath(real_root.path()));
        QVERIFY(write_minimal_project_tree(real_root, project_document(3)));
        const auto alias = parent.filePath(QStringLiteral("Linked Project"));
        if (!create_directory_symlink(real_root.path(), alias))
        {
            QSKIP("This host does not permit creation of a directory symbolic link.");
        }

        const ProjectIndexService service;
        const auto selected_manifest = QDir{alias}.filePath(
            QStringLiteral("DragonPixelProject.json"));
        const auto location = service.validate_location(selected_manifest);
        const auto result = service.build_candidate(selected_manifest);
        QVERIFY(remove_filesystem_link(alias));

        QVERIFY(!location.succeeded());
        QVERIFY(!location.location.has_value());
        QVERIFY(!result.candidate.has_value());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
    }

    void allows_safe_ancestor_alias_but_not_a_linked_project_root()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir parent{directory.path()};
        const QDir real_container{parent.filePath(QStringLiteral("Real Container"))};
        const QDir real_root{real_container.filePath(QStringLiteral("Real Project"))};
        QVERIFY(QDir{}.mkpath(real_root.path()));
        QVERIFY(write_minimal_project_tree(real_root, project_document(3)));
        const auto container_alias = parent.filePath(QStringLiteral("Container Alias"));
        if (!create_directory_symlink(real_container.path(), container_alias))
        {
            QSKIP("This host does not permit creation of a directory symbolic link.");
        }

        const auto selected_manifest = QDir{container_alias}.filePath(
            QStringLiteral("Real Project/DragonPixelProject.json"));
        const ProjectIndexService service;
        const auto location = service.validate_location(selected_manifest);
        const auto result = service.build_candidate(selected_manifest);
        QVERIFY(remove_filesystem_link(container_alias));

        QVERIFY2(location.succeeded(), "A safe ancestor alias was rejected.");
        QVERIFY(location.location.has_value());
        QCOMPARE(location.location->project_root, real_root.canonicalPath());
        QVERIFY(result.succeeded());
        QVERIFY(result.candidate.has_value());
        QCOMPARE(result.candidate->project_root, real_root.canonicalPath());
    }

    void rejects_linked_manifest_file()
    {
        QTemporaryDir project_directory;
        QTemporaryDir outside_directory;
        QVERIFY(project_directory.isValid());
        QVERIFY(outside_directory.isValid());
        const QDir root{project_directory.path()};
        const auto target = QDir{outside_directory.path()}.filePath(
            QStringLiteral("Real Manifest.json"));
        QVERIFY(write_json(target, project_document(3)));
        const auto link = root.filePath(QStringLiteral("DragonPixelProject.json"));
        if (!create_file_symlink(target, link))
        {
            QSKIP("This host does not permit creation of a file symbolic link.");
        }

        const ProjectIndexService service;
        const auto location = service.validate_location(link);
        const auto result = service.build_candidate(link);
        QVERIFY(remove_filesystem_link(link));

        QVERIFY(!location.succeeded());
        QVERIFY(!location.location.has_value());
        QVERIFY(!result.candidate.has_value());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
    }

    void rejects_linked_document_entry()
    {
        QTemporaryDir project_directory;
        QTemporaryDir outside_directory;
        QVERIFY(project_directory.isValid());
        QVERIFY(outside_directory.isValid());
        const QDir root{project_directory.path()};
        QVERIFY(write_minimal_project_tree(root, project_document(3)));
        const auto target = QDir{outside_directory.path()}.filePath(
            QStringLiteral("Outside Asset.dpeasset"));
        QVERIFY(write_json(
            target,
            asset_document(QString::fromLatin1(asset_a_id), QStringLiteral("builtin://outside"))));
        const auto link = root.filePath(QStringLiteral("Assets/Linked Asset.dpeasset"));
        if (!create_file_symlink(target, link))
        {
            QSKIP("This host does not permit creation of a file symbolic link.");
        }

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(remove_filesystem_link(link));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
        QVERIFY(result.candidate->find_by_id(QString::fromLatin1(asset_a_id)) == nullptr);
    }

    void rejects_ambiguous_case_alias_v2_components_directories()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        QVERIFY(write_minimal_project_tree(root, project_document()));
        QVERIFY(QDir{}.mkpath(root.filePath(QStringLiteral("Components"))));
        QVERIFY(QDir{}.mkpath(root.filePath(QStringLiteral("components"))));
        const auto matches = root.entryList(
            QStringList{QStringLiteral("Components"), QStringLiteral("components")},
            QDir::Dirs | QDir::NoDotAndDotDot);
        if (matches.size() != 2)
        {
            QSKIP("This filesystem cannot represent case-alias Components directories.");
        }

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
        QCOMPARE(result.candidate->format_version, 2);
        QVERIFY(result.candidate->migrations.isEmpty());
    }

    void rejects_nonportable_declared_paths()
    {
        const QStringList invalid_roots{
            QStringLiteral("Components\\Native"),
            QStringLiteral("C:/Components"),
            QStringLiteral("//server/share"),
            QStringLiteral("CON"),
            QStringLiteral("LPT1.generated"),
            QStringLiteral("Components."),
            QStringLiteral("Components "),
            QStringLiteral("Components//Nested"),
            QStringLiteral("Components/../Nested"),
        };
        for (const auto& invalid : invalid_roots)
        {
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            const QDir root{directory.path()};
            auto project = project_document(3);
            project.insert(QStringLiteral("componentRoots"), QJsonArray{invalid});
            QVERIFY(write_minimal_project_tree(root, project));

            const auto result = ProjectIndexService{}.build_candidate(
                root.filePath(QStringLiteral("DragonPixelProject.json")));
            QVERIFY2(result.has_errors(), qPrintable(invalid));
            QVERIFY2(has_code(result, ProjectIndexDiagnosticCode::unsafe_path), qPrintable(invalid));
        }

        QTemporaryDir startup_directory;
        QVERIFY(startup_directory.isValid());
        const QDir startup_root{startup_directory.path()};
        auto startup_project = project_document(3);
        startup_project.insert(
            QStringLiteral("startupScene"), QStringLiteral("Scenes\\Main.dpescene"));
        QVERIFY(write_minimal_project_tree(startup_root, startup_project));
        const auto startup_result = ProjectIndexService{}.build_candidate(
            startup_root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(has_code(startup_result, ProjectIndexDiagnosticCode::unsafe_path));

        QTemporaryDir source_directory;
        QVERIFY(source_directory.isValid());
        const QDir source_root{source_directory.path()};
        QVERIFY(write_minimal_project_tree(source_root, project_document(3)));
        QVERIFY(write_json(
            source_root.filePath(QStringLiteral("Assets/Invalid.dpeasset")),
            asset_document(
                QString::fromLatin1(asset_a_id), QStringLiteral("Nested\\sprite.bin"))));
        const auto source_result = ProjectIndexService{}.build_candidate(
            source_root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(has_code(source_result, ProjectIndexDiagnosticCode::unsafe_path));
    }

    void startup_scene_must_be_beneath_a_declared_scene_root()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        auto project = project_document(3);
        project.insert(
            QStringLiteral("startupScene"), QStringLiteral("Other/Main.dpescene"));
        QVERIFY(write_minimal_project_tree(root, project));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Other/Main.dpescene")), scene_document()));

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::startup_scene_not_indexed));
        QVERIFY(result.candidate->startup_scene_path.isEmpty());
        QCOMPARE(result.candidate->entries.size(), 1);
    }

    void rejects_cross_kind_and_repeated_invalid_root_declarations()
    {
        QTemporaryDir collision_directory;
        QVERIFY(collision_directory.isValid());
        const QDir collision_root{collision_directory.path()};
        auto collision_project = project_document(3);
        collision_project.insert(
            QStringLiteral("startupScene"), QStringLiteral("Shared/Main.dpescene"));
        collision_project.insert(
            QStringLiteral("sceneRoots"), QJsonArray{QStringLiteral("Shared")});
        collision_project.insert(
            QStringLiteral("assetRoots"), QJsonArray{QStringLiteral("shared")});
        QVERIFY(write_json(
            collision_root.filePath(QStringLiteral("DragonPixelProject.json")),
            collision_project));
        QVERIFY(write_json(
            collision_root.filePath(QStringLiteral("Shared/Main.dpescene")), scene_document()));
        QVERIFY(QDir{}.mkpath(collision_root.filePath(QStringLiteral("Assets"))));

        const auto collision_result = ProjectIndexService{}.build_candidate(
            collision_root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(collision_result.has_errors());
        QVERIFY(has_code(collision_result, ProjectIndexDiagnosticCode::duplicate_root));

        QTemporaryDir repeated_directory;
        QVERIFY(repeated_directory.isValid());
        const QDir repeated_root{repeated_directory.path()};
        auto repeated_project = project_document(3);
        repeated_project.insert(
            QStringLiteral("sceneRoots"),
            QJsonArray{QStringLiteral("Missing"), QStringLiteral("Missing")});
        QVERIFY(write_minimal_project_tree(repeated_root, repeated_project));

        const auto repeated_result = ProjectIndexService{}.build_candidate(
            repeated_root.filePath(QStringLiteral("DragonPixelProject.json")));
        QVERIFY(repeated_result.has_errors());
        QVERIFY(has_code(repeated_result, ProjectIndexDiagnosticCode::duplicate_root));
        QVERIFY(has_code(repeated_result, ProjectIndexDiagnosticCode::missing_root));
    }

    void asset_sources_participate_in_portable_alias_detection()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        QVERIFY(write_minimal_project_tree(root, project_document(3)));
        const auto composed_name = QStringLiteral("Caf\u00e9.bin");
        const auto decomposed_name = QStringLiteral("Cafe\u0301.bin");
        QVERIFY(write_bytes(root.filePath(QStringLiteral("Assets/") + composed_name), QByteArrayLiteral("a")));
        QVERIFY(write_bytes(root.filePath(QStringLiteral("Assets/") + decomposed_name), QByteArrayLiteral("b")));
        const auto source_files = QDir{root.filePath(QStringLiteral("Assets"))}.entryList(
            QStringList{QStringLiteral("*.bin")}, QDir::Files);
        if (source_files.size() != 2)
        {
            QSKIP("This filesystem does not preserve distinct Unicode-normalization aliases.");
        }
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/A.dpeasset")),
            asset_document(QString::fromLatin1(asset_a_id), composed_name)));
        QVERIFY(write_json(
            root.filePath(QStringLiteral("Assets/B.dpeasset")),
            asset_document(QString::fromLatin1(asset_b_id), decomposed_name)));

        const auto result = ProjectIndexService{}.build_candidate(
            root.filePath(QStringLiteral("DragonPixelProject.json")));

        QVERIFY(result.candidate.has_value());
        QVERIFY(result.has_errors());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::unsafe_path));
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
        QCOMPARE(result.candidate->source_format_version, 1);
        QCOMPARE(result.candidate->format_version, 4);
        QCOMPARE(result.candidate->migrations.size(), 3);
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

        const ProjectIndexService service;
        const auto location = service.validate_location(manifest);
        const auto result = service.build_candidate(manifest);
        QVERIFY(location.succeeded());
        QVERIFY(location.location.has_value());
        QCOMPARE(location.location->manifest_path, QFileInfo{manifest}.canonicalFilePath());
        QVERIFY(!result.candidate.has_value());
        QVERIFY(has_code(result, ProjectIndexDiagnosticCode::invalid_json));
    }
};

QTEST_APPLESS_MAIN(ProjectIndexServiceTests)

#include "ProjectIndexServiceTests.moc"
