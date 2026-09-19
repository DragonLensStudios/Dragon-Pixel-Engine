#include "EditorModels.h"

#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QUuid>
#include <QtTest>

#include <functional>

namespace
{
constexpr auto scene_id = "11111111-1111-4111-8111-111111111111";
constexpr auto prefab_id = "22222222-2222-4222-8222-222222222222";
constexpr auto sprite_id = "33333333-3333-4333-8333-333333333333";
constexpr auto mesh_id = "44444444-4444-4444-8444-444444444444";
constexpr auto missing_id = "55555555-5555-4555-8555-555555555555";

struct ModelFixture final
{
    ProjectIndexCandidate candidate;
    QString sprite_path;
};

ModelFixture make_fixture()
{
    ModelFixture fixture;
    auto& candidate = fixture.candidate;
    candidate.project_root = QDir::temp().filePath(
        QStringLiteral("dpe-project-model-no-io-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    candidate.manifest_path = QDir{candidate.project_root}.filePath(
        QStringLiteral("DragonPixelProject.json"));
    candidate.project_id = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    candidate.name = QStringLiteral("Model Test Project");
    candidate.format_version = 2;
    candidate.roots = {
        ProjectIndexRoot{
            ProjectIndexRootKind::scenes,
            QStringLiteral("Scenes"),
            QDir{candidate.project_root}.filePath(QStringLiteral("Scenes"))},
        ProjectIndexRoot{
            ProjectIndexRootKind::assets,
            QStringLiteral("Assets"),
            QDir{candidate.project_root}.filePath(QStringLiteral("Assets"))},
        ProjectIndexRoot{
            ProjectIndexRootKind::components,
            QStringLiteral("Components"),
            QDir{candidate.project_root}.filePath(QStringLiteral("Components"))},
    };

    ProjectIndexEntry sprite;
    sprite.kind = ProjectIndexEntryKind::asset;
    sprite.id = QString::fromLatin1(sprite_id);
    sprite.logical_path = QStringLiteral("Assets/Dragon.sprite.dpeasset");
    sprite.absolute_path = QDir{candidate.project_root}.filePath(sprite.logical_path);
    fixture.sprite_path = sprite.absolute_path;
    sprite.format_version = 2;
    sprite.asset_type = QStringLiteral("sprite");
    sprite.source = QStringLiteral("builtin://dragon");
    sprite.dependencies = {QString::fromLatin1(missing_id)};
    sprite.document = {{QStringLiteral("name"), QStringLiteral("Dragon")}};

    ProjectIndexEntry scene;
    scene.kind = ProjectIndexEntryKind::scene;
    scene.id = QString::fromLatin1(scene_id);
    scene.display_name = QStringLiteral("Main Scene");
    scene.logical_path = QStringLiteral("Scenes/Main.dpescene");
    scene.absolute_path = QDir{candidate.project_root}.filePath(scene.logical_path);
    scene.format_version = 3;

    ProjectIndexEntry prefab;
    prefab.kind = ProjectIndexEntryKind::prefab;
    prefab.id = QString::fromLatin1(prefab_id);
    prefab.logical_path = QStringLiteral("Assets/Prefabs/Actor.dpeprefab");
    prefab.absolute_path = QDir{candidate.project_root}.filePath(prefab.logical_path);
    prefab.format_version = 1;
    prefab.dependencies = {QString::fromLatin1(mesh_id)};
    prefab.document = {{QStringLiteral("name"), QStringLiteral("Actor")}};

    ProjectIndexEntry mesh;
    mesh.kind = ProjectIndexEntryKind::asset;
    mesh.id = QString::fromLatin1(mesh_id);
    mesh.logical_path = QStringLiteral("Assets/Cube.mesh.dpeasset");
    mesh.absolute_path = QDir{candidate.project_root}.filePath(mesh.logical_path);
    mesh.format_version = 2;
    mesh.asset_type = QStringLiteral("mesh");
    mesh.source = QStringLiteral("generated://cube");
    mesh.document = {{QStringLiteral("name"), QStringLiteral("Cube")}};

    ProjectIndexEntry script;
    script.kind = ProjectIndexEntryKind::component_source;
    script.display_name = QStringLiteral("PlayerController.cs");
    script.logical_path = QStringLiteral("Components/CSharp/PlayerController.cs");
    script.absolute_path = QDir{candidate.project_root}.filePath(script.logical_path);
    script.asset_type = QStringLiteral("C# Script");

    ProjectIndexEntry component_manifest;
    component_manifest.kind = ProjectIndexEntryKind::component_manifest;
    component_manifest.display_name = QStringLiteral("Gameplay.dpecomponents");
    component_manifest.logical_path = QStringLiteral("Components/Gameplay.dpecomponents");
    component_manifest.absolute_path = QDir{candidate.project_root}.filePath(
        component_manifest.logical_path);

    // Deliberately non-sorted input proves the model does not inherit caller
    // insertion order.
    candidate.entries = {scene, script, sprite, component_manifest, mesh, prefab};
    return fixture;
}

QModelIndex find_logical_path(
    const QAbstractItemModel& model,
    const QString& logical_path,
    const QModelIndex& parent = {})
{
    for (auto row = 0; row < model.rowCount(parent); ++row)
    {
        const auto index = model.index(row, 0, parent);
        if (index.data(EditorRoles::project_logical_path).toString() == logical_path)
        {
            return index;
        }
        if (const auto child = find_logical_path(model, logical_path, index); child.isValid())
        {
            return child;
        }
    }
    return {};
}

QStringList child_names(const QAbstractItemModel& model, const QModelIndex& parent)
{
    QStringList result;
    for (auto row = 0; row < model.rowCount(parent); ++row)
    {
        result.push_back(model.index(row, 0, parent).data().toString());
    }
    return result;
}

int entry_count(const QAbstractItemModel& model, const QModelIndex& parent = {})
{
    auto result = 0;
    for (auto row = 0; row < model.rowCount(parent); ++row)
    {
        const auto index = model.index(row, 0, parent);
        const auto kind = ProjectModel::item_kind(index);
        if (kind == ProjectItemKind::scene || kind == ProjectItemKind::prefab
            || kind == ProjectItemKind::asset
            || kind == ProjectItemKind::component_source
            || kind == ProjectItemKind::component_manifest)
        {
            ++result;
        }
        result += entry_count(model, index);
    }
    return result;
}
}

class ProjectModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void candidate_rebuild_is_deterministic_and_file_system_independent()
    {
        auto fixture = make_fixture();
        QVERIFY(!QFileInfo::exists(fixture.candidate.project_root));

        ProjectModel model;
        model.rebuild(fixture.candidate);

        QVERIFY(!QFileInfo::exists(fixture.candidate.project_root));
        QCOMPARE(model.columnCount(), static_cast<int>(ProjectColumn::count));
        QCOMPARE(model.rowCount(), 1);
        const auto project = model.index(0, 0);
        QCOMPARE(child_names(model, project), QStringList({
            QStringLiteral("DragonPixelProject.json"),
            QStringLiteral("Assets"),
            QStringLiteral("Components"),
            QStringLiteral("Scenes"),
        }));
        const auto assets = find_logical_path(model, QStringLiteral("Assets"));
        QVERIFY(assets.isValid());
        QCOMPARE(child_names(model, assets), QStringList({
            QStringLiteral("Prefabs"),
            QStringLiteral("Cube"),
            QStringLiteral("Dragon"),
        }));
        QCOMPARE(entry_count(model), 6);
        QVERIFY(find_logical_path(model, QStringLiteral("Scenes/Main.dpescene")).isValid());
        QVERIFY(find_logical_path(
            model,
            QStringLiteral("Assets/Prefabs/Actor.dpeprefab")).isValid());
        const auto script = find_logical_path(
            model,
            QStringLiteral("Components/CSharp/PlayerController.cs"));
        QVERIFY(script.isValid());
        QCOMPARE(ProjectModel::item_kind(script), ProjectItemKind::component_source);
        QCOMPARE(
            script.siblingAtColumn(static_cast<int>(ProjectColumn::kind_type)).data().toString(),
            QStringLiteral("Component Source / C# Script"));
    }

    void exposes_entry_roles_columns_tooltips_and_diagnostic_status()
    {
        auto fixture = make_fixture();
        ProjectIndexBuildResult result;
        result.candidate = fixture.candidate;
        result.diagnostics = {
            ProjectIndexDiagnostic{
                ProjectIndexDiagnosticSeverity::warning,
                ProjectIndexDiagnosticCode::unsorted_roots,
                QStringLiteral("Root declarations are not sorted."),
                fixture.candidate.manifest_path,
                QStringLiteral("/assetRoots"),
                {}},
            ProjectIndexDiagnostic{
                ProjectIndexDiagnosticSeverity::error,
                ProjectIndexDiagnosticCode::missing_dependency,
                QStringLiteral("Sprite dependency is missing."),
                fixture.sprite_path,
                QStringLiteral("/dependencies"),
                QString::fromLatin1(missing_id)},
        };

        ProjectModel model;
        model.rebuild(result);
        const auto sprite = find_logical_path(
            model,
            QStringLiteral("Assets/Dragon.sprite.dpeasset"));
        QVERIFY(sprite.isValid());
        QCOMPARE(ProjectModel::item_kind(sprite), ProjectItemKind::asset);
        QCOMPARE(ProjectModel::asset_id(sprite), QString::fromLatin1(sprite_id));
        QCOMPARE(ProjectModel::asset_type(sprite), QStringLiteral("sprite"));
        QCOMPARE(sprite.data(EditorRoles::project_entry_id).toString(), QString::fromLatin1(sprite_id));
        QCOMPARE(sprite.data(EditorRoles::project_type_filter).toString(), QStringLiteral("asset sprite"));
        QCOMPARE(sprite.data(EditorRoles::project_status_filter).toString(), QStringLiteral("error"));
        QCOMPARE(sprite.data(EditorRoles::project_import_status).toString(), QStringLiteral("Built-in"));
        QCOMPARE(sprite.data(EditorRoles::project_dependency_status).toString(), QStringLiteral("Missing"));
        QVERIFY(sprite.data(EditorRoles::project_structurally_valid).toBool());
        QCOMPARE(
            sprite.siblingAtColumn(static_cast<int>(ProjectColumn::kind_type)).data().toString(),
            QStringLiteral("Asset / sprite"));
        QCOMPARE(
            sprite.siblingAtColumn(static_cast<int>(ProjectColumn::identifier)).data().toString(),
            QString::fromLatin1(sprite_id));
        QCOMPARE(
            sprite.siblingAtColumn(static_cast<int>(ProjectColumn::import_status)).data().toString(),
            QStringLiteral("Built-in"));
        QCOMPARE(
            sprite.siblingAtColumn(static_cast<int>(ProjectColumn::dependency_status)).data().toString(),
            QStringLiteral("Missing"));
        QVERIFY(sprite.data(Qt::ToolTipRole).toString().contains(QStringLiteral("missing-dependency")));

        const auto manifest = find_logical_path(
            model,
            QStringLiteral("DragonPixelProject.json"));
        QCOMPARE(manifest.data(EditorRoles::project_status_filter).toString(), QStringLiteral("warning"));
        const auto assets = find_logical_path(model, QStringLiteral("Assets"));
        QCOMPARE(assets.data(EditorRoles::project_status_filter).toString(), QStringLiteral("error"));
        QCOMPARE(model.index(0, 0).data(EditorRoles::project_status_filter).toString(), QStringLiteral("error"));
    }

    void updates_preview_by_unique_id_or_metadata_path()
    {
        auto fixture = make_fixture();
        ProjectModel model;
        model.rebuild(fixture.candidate);
        auto sprite = find_logical_path(
            model,
            QStringLiteral("Assets/Dragon.sprite.dpeasset"));
        QVERIFY(sprite.isValid());

        QPixmap red{8, 8};
        red.fill(Qt::red);
        QVERIFY(model.set_asset_preview(
            QString::fromLatin1(sprite_id),
            QIcon{red},
            QStringLiteral("sha256:first")));
        sprite = find_logical_path(model, QStringLiteral("Assets/Dragon.sprite.dpeasset"));
        QVERIFY(!sprite.data(Qt::DecorationRole).value<QIcon>().isNull());
        QCOMPARE(
            sprite.data(EditorRoles::preview_content_identity).toString(),
            QStringLiteral("sha256:first"));

        QPixmap blue{8, 8};
        blue.fill(Qt::blue);
        QVERIFY(model.set_asset_preview(
            fixture.sprite_path,
            QIcon{blue},
            QStringLiteral("sha256:second")));
        QCOMPARE(
            find_logical_path(model, QStringLiteral("Assets/Dragon.sprite.dpeasset"))
                .data(EditorRoles::preview_content_identity).toString(),
            QStringLiteral("sha256:second"));
        QVERIFY(!model.set_asset_preview(
            QStringLiteral("unknown"),
            QIcon{blue},
            QStringLiteral("sha256:none")));
        QVERIFY(!model.set_asset_preview(
            fixture.sprite_path,
            QIcon{},
            QStringLiteral("sha256:none")));
    }

    void project_filter_combines_text_type_status_and_tree_ancestry()
    {
        auto fixture = make_fixture();
        ProjectIndexBuildResult result;
        result.candidate = fixture.candidate;
        result.diagnostics = {
            ProjectIndexDiagnostic{
                ProjectIndexDiagnosticSeverity::error,
                ProjectIndexDiagnosticCode::missing_dependency,
                QStringLiteral("Sprite dependency is missing."),
                fixture.sprite_path,
                QStringLiteral("/dependencies"),
                QString::fromLatin1(missing_id)},
            ProjectIndexDiagnostic{
                ProjectIndexDiagnosticSeverity::warning,
                ProjectIndexDiagnosticCode::unexpected_document_format,
                QStringLiteral("Scene needs review."),
                QDir{fixture.candidate.project_root}.filePath(
                    QStringLiteral("Scenes/Main.dpescene")),
                {},
                QString::fromLatin1(scene_id)},
            ProjectIndexDiagnostic{
                ProjectIndexDiagnosticSeverity::information,
                ProjectIndexDiagnosticCode::unsupported_document_version,
                QStringLiteral("Prefab has informational migration metadata."),
                QDir{fixture.candidate.project_root}.filePath(
                    QStringLiteral("Assets/Prefabs/Actor.dpeprefab")),
                {},
                QString::fromLatin1(prefab_id)},
        };

        ProjectModel model;
        model.rebuild(result);
        ProjectFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        QCOMPARE(proxy.columnCount(), 2);
        QCOMPARE(proxy.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Name"));
        QCOMPARE(proxy.headerData(1, Qt::Horizontal).toString(), QStringLiteral("Kind / Type"));
        ProjectFolderProxyModel folder_proxy;
        folder_proxy.setSourceModel(&model);
        QCOMPARE(folder_proxy.columnCount(), 1);
        QCOMPARE(folder_proxy.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Name"));
        QCOMPARE(entry_count(proxy), 6);

        proxy.set_type_filter(QStringLiteral(" SCENE "));
        QCOMPARE(proxy.type_filter(), QStringLiteral("scene"));
        QCOMPARE(entry_count(proxy), 1);
        QVERIFY(find_logical_path(proxy, QStringLiteral("Scenes/Main.dpescene")).isValid());
        QVERIFY(!find_logical_path(proxy, QStringLiteral("Assets")).isValid());
        QVERIFY(!find_logical_path(proxy, QStringLiteral("DragonPixelProject.json")).isValid());

        proxy.set_type_filter(QStringLiteral("asset"));
        QCOMPARE(entry_count(proxy), 2);
        QVERIFY(find_logical_path(proxy, QStringLiteral("Assets")).isValid());
        QVERIFY(!find_logical_path(proxy, QStringLiteral("Assets/Prefabs")).isValid());
        QVERIFY(!find_logical_path(proxy, QStringLiteral("Scenes")).isValid());

        proxy.set_type_filter(QStringLiteral("component"));
        QCOMPARE(entry_count(proxy), 2);
        QVERIFY(find_logical_path(
            proxy, QStringLiteral("Components/CSharp/PlayerController.cs")).isValid());
        QVERIFY(find_logical_path(
            proxy, QStringLiteral("Components/Gameplay.dpecomponents")).isValid());

        proxy.clear_type_filter();
        proxy.set_search_text(QStringLiteral("main scene"));
        QCOMPARE(entry_count(proxy), 1);
        QVERIFY(find_logical_path(proxy, QStringLiteral("Scenes/Main.dpescene")).isValid());
        proxy.set_search_text(QStringLiteral("dragon scene"));
        QCOMPARE(entry_count(proxy), 0);
        proxy.set_search_text(QStringLiteral("t:scene t:prefab"));
        QCOMPARE(entry_count(proxy), 2);
        QVERIFY(find_logical_path(proxy, QStringLiteral("Scenes/Main.dpescene")).isValid());
        QVERIFY(find_logical_path(
            proxy, QStringLiteral("Assets/Prefabs/Actor.dpeprefab")).isValid());
        proxy.set_search_text(QStringLiteral("t:asset s:error"));
        QCOMPARE(entry_count(proxy), 1);
        QVERIFY(find_logical_path(
            proxy, QStringLiteral("Assets/Dragon.sprite.dpeasset")).isValid());
        proxy.clear_search_text();
        proxy.set_status_filter(QStringLiteral("ERROR"));
        QCOMPARE(proxy.status_filter(), QStringLiteral("error"));
        QCOMPARE(entry_count(proxy), 1);
        const auto error_assets = find_logical_path(proxy, QStringLiteral("Assets"));
        QVERIFY(error_assets.isValid());
        QCOMPARE(child_names(proxy, error_assets), QStringList({QStringLiteral("Dragon")}));
        QVERIFY(!find_logical_path(proxy, QStringLiteral("Assets/Prefabs")).isValid());
        QVERIFY(!find_logical_path(proxy, QStringLiteral("Scenes")).isValid());

        proxy.set_status_filter(QStringLiteral("warning"));
        QCOMPARE(entry_count(proxy), 1);
        QVERIFY(find_logical_path(proxy, QStringLiteral("Scenes/Main.dpescene")).isValid());
        proxy.set_status_filter(QStringLiteral("information"));
        QCOMPARE(entry_count(proxy), 1);
        QVERIFY(find_logical_path(
            proxy,
            QStringLiteral("Assets/Prefabs/Actor.dpeprefab")).isValid());
        proxy.set_status_filter(QStringLiteral("ready"));
        QCOMPARE(entry_count(proxy), 3);
        QVERIFY(find_logical_path(
            proxy,
            QStringLiteral("Assets/Cube.mesh.dpeasset")).isValid());

        proxy.clear_filters();
        proxy.set_search_text(QStringLiteral(" dragon "));
        QCOMPARE(proxy.search_text(), QStringLiteral("dragon"));
        QCOMPARE(entry_count(proxy), 1);
        const auto text_assets = find_logical_path(proxy, QStringLiteral("Assets"));
        QVERIFY(text_assets.isValid());
        QCOMPARE(child_names(proxy, text_assets), QStringList({QStringLiteral("Dragon")}));
        QVERIFY(!find_logical_path(proxy, QStringLiteral("Scenes")).isValid());

        proxy.set_search_text(QStringLiteral("Assets"));
        QCOMPARE(entry_count(proxy), 3);
        QVERIFY(find_logical_path(proxy, QStringLiteral("Assets/Prefabs")).isValid());
        QVERIFY(!find_logical_path(proxy, QStringLiteral("Scenes")).isValid());

        proxy.set_type_filter(QStringLiteral("asset"));
        proxy.set_status_filter(QStringLiteral("ready"));
        proxy.set_search_text(QStringLiteral("Cube"));
        QCOMPARE(entry_count(proxy), 1);
        QVERIFY(find_logical_path(
            proxy,
            QStringLiteral("Assets/Cube.mesh.dpeasset")).isValid());
        proxy.set_search_text(QStringLiteral("does-not-exist"));
        QCOMPARE(proxy.rowCount(), 0);

        proxy.clear_search_text();
        proxy.clear_type_filter();
        proxy.clear_status_filter();
        QCOMPARE(entry_count(proxy), 6);
    }

    void orders_folder_and_asset_names_naturally_with_visible_type_icons()
    {
        ProjectIndexCandidate candidate;
        candidate.project_root = QDir::temp().filePath(
            QStringLiteral("dpe-project-model-natural-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        candidate.manifest_path = QDir{candidate.project_root}.filePath(
            QStringLiteral("DragonPixelProject.json"));
        candidate.project_id = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        candidate.name = QStringLiteral("Natural Sort Project");
        candidate.format_version = 4;
        candidate.roots = {{
            ProjectIndexRootKind::assets,
            QStringLiteral("Assets"),
            QDir{candidate.project_root}.filePath(QStringLiteral("Assets")),
        }};
        candidate.discovered_folders = {
            QStringLiteral("Assets/Folder10"),
            QStringLiteral("Assets/Folder2"),
        };

        const auto make_asset = [&](const QString& id, const QString& name) {
            ProjectIndexEntry entry;
            entry.kind = ProjectIndexEntryKind::asset;
            entry.id = id;
            entry.display_name = name;
            entry.logical_path = QStringLiteral("Assets/%1.dpeasset").arg(name);
            entry.absolute_path = QDir{candidate.project_root}.filePath(entry.logical_path);
            entry.format_version = 3;
            entry.asset_type = QStringLiteral("sprite");
            entry.source = QStringLiteral("generated://%1").arg(name);
            return entry;
        };
        candidate.entries = {
            make_asset(QStringLiteral("11111111-1111-4111-8111-111111111111"), QStringLiteral("Tile10")),
            make_asset(QStringLiteral("22222222-2222-4222-8222-222222222222"), QStringLiteral("Tile2")),
        };

        ProjectModel model;
        model.rebuild(candidate);
        const auto assets = find_logical_path(model, QStringLiteral("Assets"));
        QVERIFY(assets.isValid());
        QCOMPARE(child_names(model, assets), QStringList({
            QStringLiteral("Folder2"),
            QStringLiteral("Folder10"),
            QStringLiteral("Tile2"),
            QStringLiteral("Tile10"),
        }));
        for (int row = 0; row < model.rowCount(assets); ++row)
        {
            QVERIFY(!model.index(row, 0, assets).data(Qt::DecorationRole).value<QIcon>().isNull());
        }
    }
};

QTEST_MAIN(ProjectModelTests)
#include "ProjectModelTests.moc"
