#include "PrefabService.h"

#include <dragonpixel/metadata/builtin_ids.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using dragonpixel::core::uuid;
using dragonpixel::prefab::document;
using dragonpixel::prefab::instance_record;
using dragonpixel::scene::command;
using dragonpixel::scene::component_record;
using dragonpixel::scene::entity;
using dragonpixel::scene::scene;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error{message};
    }
}

uuid id(const char* value)
{
    const auto parsed = uuid::parse(value);
    require(parsed.has_value(), "Prefab editor test UUID was invalid.");
    return *parsed;
}

std::filesystem::path filesystem_path(const QString& value)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path{value.toStdWString()};
#else
    return std::filesystem::path{value.toStdString()};
#endif
}

bool create_file_symlink(const QString& target, const QString& link)
{
    std::error_code error;
    std::filesystem::create_symlink(filesystem_path(target), filesystem_path(link), error);
    return !error;
}

component_record transform()
{
    return {
        std::string{dragonpixel::metadata::builtin_component_ids::transform},
        2,
        dragonpixel::metadata::runtime_owner::native,
        {
            {"dpe.transform.position", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}},
            {"dpe.transform.rotation", {{"w", 1.0}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}},
            {"dpe.transform.scale", {{"x", 1.0}, {"y", 1.0}, {"z", 1.0}}},
        },
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Native.TransformComponent",
    };
}

component_record rotator(const uuid& target)
{
    return {
        std::string{dragonpixel::metadata::builtin_component_ids::rotator},
        1,
        dragonpixel::metadata::runtime_owner::managed,
        {
            {"dpe.rotator.degrees_per_second", 30.0},
            {"dpe.rotator.target", target.to_string()},
        },
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Managed.RotatorComponent",
    };
}

QByteArray file_bytes(const QString& path)
{
    QFile file{path};
    require(file.open(QIODevice::ReadOnly), "Could not read temporary prefab source.");
    return file.readAll();
}

document read_source(const QString& path)
{
    const auto bytes = file_bytes(path);
    const auto loaded = dragonpixel::prefab::read_json(
        std::string_view{bytes.constData(), static_cast<std::size_t>(bytes.size())});
    require(loaded.value.has_value(), "Temporary prefab source did not parse.");
    return *loaded.value;
}

void write_document(const QString& path, const document& value)
{
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    QFile file{path};
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Could not create temporary prefab document.");
    const auto bytes = QByteArray::fromStdString(dragonpixel::prefab::write_json(value));
    require(file.write(bytes) == bytes.size(), "Could not write temporary prefab document.");
}

void write_bytes(const QString& path, const QByteArray& bytes)
{
    QFile file{path};
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Could not replace temporary prefab bytes.");
    require(file.write(bytes) == bytes.size(), "Could not replace temporary prefab bytes.");
}

QString write_source(const QString& root)
{
    QDir{}.mkpath(QDir{root}.filePath(QStringLiteral("Prefabs")));
    document value;
    value.prefab_id = id("11111111-1111-4111-8111-111111111111");
    value.root_entity_id = id("22222222-2222-4222-8222-222222222222");
    const auto child_id = id("22222222-2222-4222-8222-222222222223");
    value.entities = {
        entity{value.root_entity_id, "Linked Cube", std::nullopt, {transform()}, true, 0},
        entity{child_id, "Linked Child", value.root_entity_id, {transform(), rotator(value.root_entity_id)}, true, 0},
    };
    value.revision = dragonpixel::prefab::compute_revision(value);
    const auto path = QDir{root}.filePath(QStringLiteral("Prefabs/LinkedCube.dpeprefab"));
    QFile file{path};
    require(file.open(QIODevice::WriteOnly), "Could not create temporary prefab source.");
    const auto bytes = QByteArray::fromStdString(dragonpixel::prefab::write_json(value));
    require(file.write(bytes) == bytes.size(), "Could not write temporary prefab source.");
    return path;
}

void instantiate_override_save_reload_and_fallback()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Could not create prefab editor test directory.");
    const auto source_path = write_source(temporary.path());
    PrefabService service;
    const auto metadata = dragonpixel::metadata::registry::slice_one_defaults();
    service.set_metadata(&metadata);
    service.set_project_root(temporary.path());
    scene authoring{id("33333333-3333-4333-8333-333333333333"), "Prefab Scene"};

    const auto instantiated = service.instantiate(authoring, source_path, std::nullopt);
    require(instantiated.succeeded && instantiated.selection.size() == 1,
        "Linked prefab did not instantiate through one transaction.");
    const auto instance_root = instantiated.selection.front();
    require(authoring.find_entity(instance_root) != nullptr
            && authoring.prefab_instances().size() == 1 && authoring.is_dirty(),
        "Instantiated prefab was not materialized with persistent provenance.");
    const auto stored = service.persistent_copy(authoring);
    require(stored.entities().empty() && stored.prefab_instances().size() == 1,
        "Saved scene copy did not separate linked entities from local entities.");

    dragonpixel::scene::scene_document_extras collision_extras;
    collision_extras.prefab_instances = stored.prefab_instances();
    collision_extras.has_explicit_sibling_order = true;
    scene colliding{
        id("44444444-4444-4444-8444-444444444444"),
        "Collision Scene",
        {entity{instance_root, "Local Must Survive", std::nullopt, {transform()}, true, 0}},
        std::move(collision_extras)};
    PrefabService collision_service;
    collision_service.set_metadata(&metadata);
    collision_service.set_project_root(temporary.path());
    const auto rejected_collision = collision_service.hydrate(colliding);
    require(!rejected_collision.scene.has_value(),
        "Candidate load accepted a linked/local UUID collision that could filter local data on save.");

    const std::vector<command> rename{
        dragonpixel::scene::rename_entity_command{instance_root, "Overridden Cube"},
    };
    const auto augmented = service.augment_commands(authoring, rename);
    require(augmented.succeeded && augmented.commands.size() == 2,
        "Linked rename did not append a normalized provenance command.");
    require(authoring.apply_transaction(augmented.commands, "Rename linked entity").succeeded,
        "Linked rename transaction was rejected.");
    service.synchronize(authoring);
    require(authoring.find_entity(instance_root)->name == "Overridden Cube",
        "Linked rename did not update materialized authoring state.");

    const auto reverted = service.revert_selected(authoring, instance_root);
    require(reverted.succeeded && authoring.find_entity(instance_root)->name == "Linked Cube",
        "Revert Selected did not rematerialize the source value.");
    require(authoring.undo().succeeded
            && authoring.find_entity(instance_root)->name == "Overridden Cube",
        "Undo did not restore both materialized values and prefab overrides.");

    const auto fallback_scene = service.persistent_copy(authoring);
    require(QFile::remove(source_path), "Could not remove temporary source for fallback test.");
    PrefabService missing_source_service;
    missing_source_service.set_metadata(&metadata);
    missing_source_service.set_project_root(temporary.path());
    const auto hydrated = missing_source_service.hydrate(fallback_scene);
    require(hydrated.scene.has_value()
            && hydrated.scene->find_entity(instance_root) != nullptr
            && hydrated.scene->find_entity(instance_root)->name == "Overridden Cube"
            && hydrated.scene->prefab_instances() == fallback_scene.prefab_instances()
            && !missing_source_service.source_available_for_entity(instance_root),
        "Missing source did not materialize and preserve its exact last successful fallback.");
    auto missing_source_scene = std::move(*hydrated.scene);
    const auto unpacked = missing_source_service.unpack(missing_source_scene, instance_root, true);
    require(unpacked.succeeded && missing_source_scene.prefab_instances().empty()
            && missing_source_service.persistent_copy(missing_source_scene).entities().size() == 2,
        "Unpack Completely did not recover missing-source fallback entities as local state.");
}

void newer_and_incompatible_sources_retain_exact_fallback_until_rebase()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Could not create source compatibility test directory.");
    const auto source_path = write_source(temporary.path());
    const auto metadata = dragonpixel::metadata::registry::slice_one_defaults();
    PrefabService service;
    service.set_metadata(&metadata);
    service.set_project_root(temporary.path());
    scene authoring{id("99999999-9999-4999-8999-999999999991"), "Source compatibility Scene"};
    const auto instantiated = service.instantiate(authoring, source_path, std::nullopt);
    require(instantiated.succeeded, "Source compatibility fixture did not instantiate.");
    const auto root_id = instantiated.selection.front();
    const std::vector<command> rename{
        dragonpixel::scene::rename_entity_command{root_id, "Last Known Good"},
    };
    const auto augmented = service.augment_commands(authoring, rename);
    require(augmented.succeeded
            && authoring.apply_transaction(augmented.commands, "Stage exact fallback").succeeded,
        "Could not stage the exact fallback override.");
    service.synchronize(authoring);
    const auto stored = service.persistent_copy(authoring);

    auto newer = read_source(source_path);
    newer.entities.front().name = "Externally Newer";
    newer.revision = dragonpixel::prefab::compute_revision(newer);
    write_document(source_path, newer);

    PrefabService stale_service;
    stale_service.set_metadata(&metadata);
    stale_service.set_project_root(temporary.path());
    auto stale_hydration = stale_service.hydrate(stored);
    require(stale_hydration.scene.has_value()
            && stale_hydration.scene->find_entity(root_id) != nullptr
            && stale_hydration.scene->find_entity(root_id)->name == "Last Known Good"
            && stale_hydration.scene->prefab_instances() == stored.prefab_instances()
            && !stale_service.source_available_for_entity(root_id)
            && stale_hydration.diagnostics.join('\n').contains(
                QStringLiteral("DPE.PREFAB.SOURCE_REVISION_MISMATCH")),
        "A newer source did not preserve the exact fallback and provenance envelope.");
    auto stale_scene = std::move(*stale_hydration.scene);
    const auto rejected_apply = stale_service.apply(stale_scene, root_id, {});
    const auto rejected_revert = stale_service.revert_all(stale_scene, root_id);
    require(!rejected_apply.succeeded && !rejected_revert.succeeded,
        "Apply or Revert remained enabled against a newer source revision.");
    const auto rebased = stale_service.repair_rebase(stale_scene, root_id);
    require(rebased.succeeded && stale_service.source_available_for_entity(root_id)
            && stale_scene.find_entity(root_id) != nullptr
            && stale_scene.find_entity(root_id)->name == "Last Known Good",
        "Explicit Repair/Rebase did not accept the newer source while preserving a surviving override.");

    auto incompatible = nlohmann::ordered_json::parse(file_bytes(source_path).toStdString());
    incompatible["formatVersion"] = 2;
    write_bytes(source_path, QByteArray::fromStdString(incompatible.dump(2) + "\n"));
    PrefabService incompatible_service;
    incompatible_service.set_metadata(&metadata);
    incompatible_service.set_project_root(temporary.path());
    const auto incompatible_hydration = incompatible_service.hydrate(stored);
    require(incompatible_hydration.scene.has_value()
            && incompatible_hydration.scene->find_entity(root_id) != nullptr
            && incompatible_hydration.scene->find_entity(root_id)->name == "Last Known Good"
            && incompatible_hydration.scene->prefab_instances() == stored.prefab_instances()
            && !incompatible_service.source_available_for_entity(root_id)
            && incompatible_hydration.diagnostics.join('\n').contains(
                QStringLiteral("DPE.PREFAB.UNSUPPORTED_FORMAT")),
        "An incompatible source did not preserve the exact fallback and provenance envelope.");
}

void apply_localizes_references_and_journals_source_history()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Could not create Apply test directory.");
    const auto source_path = write_source(temporary.path());
    const auto source_before = file_bytes(source_path);
    const auto source_root_id = id("22222222-2222-4222-8222-222222222222");
    const auto source_child_id = id("22222222-2222-4222-8222-222222222223");

    const auto metadata = dragonpixel::metadata::registry::slice_one_defaults();
    PrefabService service;
    service.set_metadata(&metadata);
    service.set_project_root(temporary.path());
    scene authoring{id("55555555-5555-4555-8555-555555555555"), "Apply Scene"};
    const auto instantiated = service.instantiate(authoring, source_path, std::nullopt);
    require(instantiated.succeeded, "Apply test prefab did not instantiate.");
    const auto instance_root = instantiated.selection.front();
    const auto child = std::find_if(authoring.entities().begin(), authoring.entities().end(), [&](const auto& item) {
        return item.parent_id == instance_root;
    });
    require(child != authoring.entities().end(), "Apply test did not materialize the linked child.");
    const auto instance_child = child->id;

    const std::vector<command> internal_edits{
        dragonpixel::scene::rename_entity_command{instance_root, "Applied Root"},
        dragonpixel::scene::set_component_property_command{
            instance_child,
            std::string{dragonpixel::metadata::builtin_component_ids::rotator},
            "dpe.rotator.target",
            instance_root.to_string()},
        dragonpixel::scene::reparent_entity_command{instance_child, std::nullopt, std::nullopt},
        dragonpixel::scene::reparent_entity_command{instance_child, instance_root, std::nullopt},
    };
    const auto augmented = service.augment_commands(authoring, internal_edits);
    require(augmented.succeeded && augmented.commands.size() == internal_edits.size() + 1,
        "Internal linked edits did not produce one normalized provenance update.");
    require(authoring.apply_transaction(augmented.commands, "Internal linked references").succeeded,
        "Internal linked reference transaction was rejected.");
    service.synchronize(authoring);

    const auto applied = service.apply(authoring, instance_root, {});
    if (!applied.succeeded)
    {
        std::cerr << applied.message.toStdString() << '\n'
                  << applied.diagnostics.join('\n').toStdString() << '\n';
    }
    require(applied.succeeded, "Apply rejected internal entity references.");
    const auto source_after = file_bytes(source_path);
    require(source_after != source_before, "Apply did not durably change the prefab source.");
    const auto persisted = read_source(source_path);
    const auto persisted_root = std::find_if(persisted.entities.begin(), persisted.entities.end(), [&](const auto& item) {
        return item.id == source_root_id;
    });
    const auto persisted_child = std::find_if(persisted.entities.begin(), persisted.entities.end(), [&](const auto& item) {
        return item.id == source_child_id;
    });
    require(persisted_root != persisted.entities.end() && persisted_root->name == "Applied Root",
        "Apply did not update the explicit source root.");
    require(persisted_child != persisted.entities.end() && persisted_child->parent_id == source_root_id,
        "Apply leaked an instance UUID into the source reparent relationship.");
    const auto persisted_rotator = std::find_if(
        persisted_child->components.begin(), persisted_child->components.end(), [&](const auto& component) {
            return component.type_id == dragonpixel::metadata::builtin_component_ids::rotator;
        });
    require(persisted_rotator != persisted_child->components.end()
            && persisted_rotator->properties.at("dpe.rotator.target") == source_root_id.to_string(),
        "Apply leaked an instance UUID into a metadata-declared entity reference.");

    const auto apply_history_position = authoring.history_position();
    QStringList diagnostics;
    require(service.prepare_undo_source_change(apply_history_position, diagnostics)
            && diagnostics.empty() && authoring.undo().succeeded
            && file_bytes(source_path) == source_before,
        "Undo did not restore the exact prefab source before-image.");
    service.synchronize(authoring);
    diagnostics.clear();
    require(service.prepare_redo_source_change(apply_history_position, diagnostics)
            && diagnostics.empty() && authoring.redo().succeeded
            && file_bytes(source_path) == source_after,
        "Redo did not restore the exact prefab source after-image.");
    service.synchronize(authoring);

    const auto external_id = id("66666666-6666-4666-8666-666666666666");
    require(authoring.apply(command{dragonpixel::scene::create_entity_command{
                external_id, "Scene Local", std::nullopt, std::nullopt}}, "Create scene-local reference target").succeeded,
        "Could not create external entity reference target.");
    const std::vector<command> external_property{
        dragonpixel::scene::set_component_property_command{
            instance_child,
            std::string{dragonpixel::metadata::builtin_component_ids::rotator},
            "dpe.rotator.target",
            external_id.to_string()},
    };
    const auto external_augmented = service.augment_commands(authoring, external_property);
    require(external_augmented.succeeded
            && authoring.apply_transaction(external_augmented.commands, "External linked reference").succeeded,
        "Could not stage the scene-local entity-reference override.");
    service.synchronize(authoring);
    const auto bytes_before_rejection = file_bytes(source_path);
    const auto rejected_property = service.apply(authoring, instance_root, {});
    require(!rejected_property.succeeded && file_bytes(source_path) == bytes_before_rejection
            && rejected_property.diagnostics.join('\n').contains(
                QStringLiteral("DPE.PREFAB.APPLY_EXTERNAL_ENTITY_REFERENCE")),
        "Apply did not reject a metadata-declared reference to scene-local state without writing the source.");

    const auto reverted = service.revert_all(authoring, instance_root);
    require(reverted.succeeded, "Could not clear the rejected property override.");
    const std::vector<command> external_reparent{
        dragonpixel::scene::reparent_entity_command{instance_child, external_id, std::nullopt},
    };
    const auto reparent_augmented = service.augment_commands(authoring, external_reparent);
    require(reparent_augmented.succeeded
            && authoring.apply_transaction(reparent_augmented.commands, "External linked parent").succeeded,
        "Could not stage the external reparent override.");
    service.synchronize(authoring);
    const auto rejected_reparent = service.apply(authoring, instance_root, {});
    require(!rejected_reparent.succeeded && file_bytes(source_path) == bytes_before_rejection
            && rejected_reparent.diagnostics.join('\n').contains(
                QStringLiteral("DPE.PREFAB.APPLY_EXTERNAL_ENTITY_REFERENCE")),
        "Apply did not reject a linked GameObject reparented under scene-local state.");
}

document simple_prefab_document(const std::string& name)
{
    document value;
    value.prefab_id = uuid::random_v4();
    value.root_entity_id = uuid::random_v4();
    value.entities = {
        entity{value.root_entity_id, name, std::nullopt, {transform()}, true, 0},
    };
    return value;
}

instance_record nested_prefab_reference(const document& target)
{
    instance_record value;
    value.instance_id = uuid::random_v4();
    value.source_asset_id = target.prefab_id;
    value.root_entity_id = uuid::random_v4();
    return value;
}

void cycle_and_depth_are_rejected_before_scene_mutation()
{
    QTemporaryDir cycle_temporary;
    require(cycle_temporary.isValid(), "Could not create cycle test directory.");
    auto cycle_a = simple_prefab_document("Cycle A");
    auto cycle_b = simple_prefab_document("Cycle B");
    cycle_a.prefab_instances.push_back(nested_prefab_reference(cycle_b));
    cycle_b.prefab_instances.push_back(nested_prefab_reference(cycle_a));
    cycle_a.revision = dragonpixel::prefab::compute_revision(cycle_a);
    cycle_b.revision = dragonpixel::prefab::compute_revision(cycle_b);
    const auto cycle_a_path = QDir{cycle_temporary.path()}.filePath(
        QStringLiteral("Prefabs/CycleA.dpeprefab"));
    const auto cycle_b_path = QDir{cycle_temporary.path()}.filePath(
        QStringLiteral("Prefabs/CycleB.dpeprefab"));
    write_document(cycle_a_path, cycle_a);
    write_document(cycle_b_path, cycle_b);

    PrefabService cycle_service;
    cycle_service.set_project_root(cycle_temporary.path());
    scene cycle_scene{id("99999999-9999-4999-8999-999999999992"), "Cycle Scene"};
    const auto cycle_history = cycle_scene.history_position();
    const auto cycle_result = cycle_service.instantiate(cycle_scene, cycle_a_path, std::nullopt);
    require(!cycle_result.succeeded
            && cycle_result.diagnostics.join('\n').contains(QStringLiteral("DPE.PREFAB.CYCLE"))
            && cycle_scene.entities().empty() && cycle_scene.prefab_instances().empty()
            && cycle_scene.history_position() == cycle_history,
        "A cyclic prefab graph was not rejected before scene mutation.");

    QTemporaryDir depth_temporary;
    require(depth_temporary.isValid(), "Could not create depth-limit test directory.");
    std::vector<document> chain;
    chain.reserve(34);
    for (int index = 0; index < 34; ++index)
    {
        chain.push_back(simple_prefab_document("Depth " + std::to_string(index)));
    }
    for (std::size_t index = 0; index + 1 < chain.size(); ++index)
    {
        chain[index].prefab_instances.push_back(nested_prefab_reference(chain[index + 1]));
    }
    QString first_path;
    for (std::size_t index = 0; index < chain.size(); ++index)
    {
        chain[index].revision = dragonpixel::prefab::compute_revision(chain[index]);
        const auto path = QDir{depth_temporary.path()}.filePath(
            QStringLiteral("Prefabs/Depth%1.dpeprefab").arg(index, 2, 10, QLatin1Char{'0'}));
        write_document(path, chain[index]);
        if (index == 0)
        {
            first_path = path;
        }
    }
    PrefabService depth_service;
    depth_service.set_project_root(depth_temporary.path());
    scene depth_scene{id("99999999-9999-4999-8999-999999999993"), "Depth Scene"};
    const auto depth_history = depth_scene.history_position();
    const auto depth_result = depth_service.instantiate(depth_scene, first_path, std::nullopt);
    require(!depth_result.succeeded
            && depth_result.diagnostics.join('\n').contains(QStringLiteral("DPE.PREFAB.DEPTH_LIMIT"))
            && depth_scene.entities().empty() && depth_scene.prefab_instances().empty()
            && depth_scene.history_position() == depth_history,
        "A prefab graph beyond the expansion-depth limit was not rejected before scene mutation.");
}

void create_source_undo_redo_and_apply_failure_are_atomic()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Could not create source journal test directory.");
    const auto metadata = dragonpixel::metadata::registry::slice_one_defaults();

    PrefabService create_service;
    create_service.set_metadata(&metadata);
    create_service.set_project_root(temporary.path());
    const auto local_id = id("77777777-7777-4777-8777-777777777777");
    scene local_scene{
        id("77777777-7777-4777-8777-777777777778"),
        "Create Prefab Scene",
        {entity{local_id, "Local Root", std::nullopt, {transform()}, true, 0}}};
    const auto created_path = QDir{temporary.path()}.filePath(QStringLiteral("Prefabs/Created.dpeprefab"));
    const auto created = create_service.create_from_selection(local_scene, local_id, created_path);
    require(created.succeeded && QFileInfo::exists(created_path),
        "Create from Selection did not write its prefab source.");
    const auto created_bytes = file_bytes(created_path);
    const auto create_history_position = local_scene.history_position();
    QStringList diagnostics;
    require(create_service.prepare_undo_source_change(create_history_position, diagnostics)
            && local_scene.undo().succeeded && !QFileInfo::exists(created_path),
        "Undo did not remove the source created by Create from Selection.");
    create_service.synchronize(local_scene);
    diagnostics.clear();
    require(create_service.prepare_redo_source_change(create_history_position, diagnostics)
            && local_scene.redo().succeeded && file_bytes(created_path) == created_bytes,
        "Redo did not recreate the exact source produced by Create from Selection.");

    const auto source_path = write_source(temporary.path());
    const auto source_before = file_bytes(source_path);
    PrefabService apply_service;
    apply_service.set_metadata(&metadata);
    apply_service.set_project_root(temporary.path());
    scene authoring{id("88888888-8888-4888-8888-888888888888"), "Apply rollback Scene"};
    const auto instantiated = apply_service.instantiate(authoring, source_path, std::nullopt);
    require(instantiated.succeeded, "Rollback fixture prefab did not instantiate.");
    const auto root = instantiated.selection.front();
    const auto child = std::find_if(authoring.entities().begin(), authoring.entities().end(), [&](const auto& item) {
        return item.parent_id == root;
    });
    require(child != authoring.entities().end(), "Rollback fixture child was missing.");
    const auto child_id = child->id;
    const std::vector<command> rename{dragonpixel::scene::rename_entity_command{root, "Must Roll Back"}};
    const auto renamed = apply_service.augment_commands(authoring, rename);
    require(renamed.succeeded && authoring.apply_transaction(renamed.commands, "Stage rollback override").succeeded,
        "Could not stage Apply rollback override.");
    apply_service.synchronize(authoring);

    require(authoring.apply(command{dragonpixel::scene::delete_subtree_command{root}}, "Remove materialization").succeeded
            && authoring.apply(command{dragonpixel::scene::create_entity_command{
                child_id, "Deliberate mapping collision", std::nullopt, std::nullopt}}, "Create collision").succeeded,
        "Could not arrange a post-write scene transaction failure.");
    const auto rejected = apply_service.apply(authoring, root, {});
    require(!rejected.succeeded && file_bytes(source_path) == source_before
            && rejected.diagnostics.join('\n').contains(QStringLiteral("DPE.PREFAB.APPLY_ROLLBACK")),
        "A failed scene rematerialization did not restore the prefab source before-image.");
    require(!QFileInfo::exists(source_path + QStringLiteral(".dpeapply"))
            && !QFileInfo::exists(source_path + QStringLiteral(".dpebak")),
        "Successful immediate Apply rollback left stale recovery artifacts.");
}

void injected_apply_failures_and_startup_recovery_preserve_both_documents()
{
    const auto metadata = dragonpixel::metadata::registry::slice_one_defaults();
    for (const auto& suffix : {QStringLiteral(".dpebak"), QStringLiteral(".dpeapply")})
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Could not create recovery-artifact collision test directory.");
        const auto source_path = write_source(temporary.path());
        const auto source_before = file_bytes(source_path);
        PrefabService service;
        service.set_metadata(&metadata);
        service.set_project_root(temporary.path());
        scene authoring{uuid::random_v4(), "Recovery Artifact Collision Scene"};
        const auto instantiated = service.instantiate(authoring, source_path, std::nullopt);
        require(instantiated.succeeded, "Recovery-artifact collision fixture did not instantiate.");
        const auto root_id = instantiated.selection.front();
        const std::vector<command> rename{
            dragonpixel::scene::rename_entity_command{root_id, "Collision Override"},
        };
        const auto augmented = service.augment_commands(authoring, rename);
        require(augmented.succeeded
                && authoring.apply_transaction(augmented.commands, "Stage artifact collision").succeeded,
            "Could not stage a recovery-artifact collision override.");
        service.synchronize(authoring);
        const auto instances_before = authoring.prefab_instances();
        const auto history_before = authoring.history_position();
        const auto artifact_path = source_path + suffix;
        const auto sentinel = QByteArrayLiteral("foreign recovery evidence\n");
        write_bytes(artifact_path, sentinel);

        const auto rejected = service.apply(authoring, root_id, {});
        require(!rejected.succeeded && file_bytes(source_path) == source_before
                && file_bytes(artifact_path) == sentinel
                && authoring.prefab_instances() == instances_before
                && authoring.history_position() == history_before
                && rejected.diagnostics.join('\n').contains(
                    QStringLiteral("DPE.PREFAB.APPLY_RECOVERY_ARTIFACT_CONFLICT")),
            "Apply overwrote pre-existing recovery evidence or changed a document.");
    }

    struct FailureCase final
    {
        PrefabApplyFault fault;
        const char* diagnostic;
    };
    const std::vector<FailureCase> failures{
        {PrefabApplyFault::backup_write, "DPE.PREFAB.APPLY_BACKUP_WRITE_FAILED"},
        {PrefabApplyFault::recovery_marker_write, "DPE.PREFAB.APPLY_MARKER_WRITE_FAILED"},
        {PrefabApplyFault::source_write, "DPE.PREFAB.APPLY_SOURCE_WRITE_FAILED"},
        {PrefabApplyFault::scene_transaction, "DPE.PREFAB.APPLY_SCENE_TRANSACTION_FAILED"},
    };
    for (const auto& failure : failures)
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Could not create injected Apply failure test directory.");
        const auto source_path = write_source(temporary.path());
        const auto source_before = file_bytes(source_path);
        PrefabService service;
        service.set_metadata(&metadata);
        service.set_project_root(temporary.path());
        scene authoring{uuid::random_v4(), "Injected Apply Scene"};
        const auto instantiated = service.instantiate(authoring, source_path, std::nullopt);
        require(instantiated.succeeded, "Injected Apply fixture did not instantiate.");
        const auto root_id = instantiated.selection.front();
        const std::vector<command> rename{
            dragonpixel::scene::rename_entity_command{root_id, "Injected Override"},
        };
        const auto augmented = service.augment_commands(authoring, rename);
        require(augmented.succeeded
                && authoring.apply_transaction(augmented.commands, "Stage injected Apply").succeeded,
            "Could not stage an override for injected Apply.");
        service.synchronize(authoring);
        const auto instances_before = authoring.prefab_instances();
        const auto history_before = authoring.history_position();

        const auto rejected = service.apply(authoring, root_id, {}, failure.fault);
        require(!rejected.succeeded && file_bytes(source_path) == source_before
                && authoring.prefab_instances() == instances_before
                && authoring.history_position() == history_before
                && authoring.find_entity(root_id) != nullptr
                && authoring.find_entity(root_id)->name == "Injected Override"
                && rejected.diagnostics.join('\n').contains(QString::fromLatin1(failure.diagnostic))
                && !QFileInfo::exists(source_path + QStringLiteral(".dpeapply"))
                && !QFileInfo::exists(source_path + QStringLiteral(".dpebak")),
            "An injected Apply boundary failure changed a document or leaked recovery artifacts.");
    }

    QTemporaryDir recovery_temporary;
    require(recovery_temporary.isValid(), "Could not create startup recovery test directory.");
    const auto recovery_path = write_source(recovery_temporary.path());
    const auto recovery_before = file_bytes(recovery_path);
    PrefabService service;
    service.set_metadata(&metadata);
    service.set_project_root(recovery_temporary.path());
    scene authoring{uuid::random_v4(), "Startup Recovery Scene"};
    const auto instantiated = service.instantiate(authoring, recovery_path, std::nullopt);
    require(instantiated.succeeded, "Startup recovery fixture did not instantiate.");
    const auto root_id = instantiated.selection.front();
    const std::vector<command> rename{
        dragonpixel::scene::rename_entity_command{root_id, "Recovery Override"},
    };
    const auto augmented = service.augment_commands(authoring, rename);
    require(augmented.succeeded
            && authoring.apply_transaction(augmented.commands, "Stage startup recovery").succeeded,
        "Could not stage the startup recovery override.");
    service.synchronize(authoring);
    const auto stored = service.persistent_copy(authoring);
    const auto interrupted = service.apply(
        authoring,
        root_id,
        {},
        PrefabApplyFault::scene_transaction_and_rollback_write);
    require(!interrupted.succeeded && file_bytes(recovery_path) != recovery_before
            && QFileInfo::exists(recovery_path + QStringLiteral(".dpeapply"))
            && QFileInfo::exists(recovery_path + QStringLiteral(".dpebak"))
            && interrupted.diagnostics.join('\n').contains(
                QStringLiteral("DPE.PREFAB.APPLY_ROLLBACK_FAILED")),
        "An interrupted Apply did not retain exact startup recovery evidence.");

    PrefabService restarted;
    restarted.set_metadata(&metadata);
    restarted.set_project_root(recovery_temporary.path());
    require(file_bytes(recovery_path) == recovery_before
            && !QFileInfo::exists(recovery_path + QStringLiteral(".dpeapply"))
            && !QFileInfo::exists(recovery_path + QStringLiteral(".dpebak")),
        "Startup recovery did not restore the exact source before-image and clear its evidence.");
    const auto recovered = restarted.hydrate(stored);
    require(recovered.scene.has_value() && recovered.scene->find_entity(root_id) != nullptr
            && recovered.scene->find_entity(root_id)->name == "Recovery Override",
        "Startup source recovery did not preserve the matching saved scene and fallback state.");
}

void refresh_rejects_symlinked_source_and_recovery_escapes_when_supported()
{
    int exercised = 0;
    {
        QTemporaryDir project;
        QTemporaryDir outside;
        require(project.isValid() && outside.isValid(), "Could not create source containment test directories.");
        const auto outside_source = write_source(outside.path());
        const auto outside_before = file_bytes(outside_source);
        const auto link_path = QDir{project.path()}.filePath(QStringLiteral("Prefabs/Escape.dpeprefab"));
        QDir{}.mkpath(QFileInfo{link_path}.absolutePath());
        if (create_file_symlink(outside_source, link_path))
        {
            ++exercised;
            PrefabService service;
            service.set_project_root(project.path());
            scene stored{uuid::random_v4(), "Source containment Scene"};
            const auto hydration = service.hydrate(stored);
            scene authoring{uuid::random_v4(), "Source containment Instantiate Scene"};
            const auto instantiated = service.instantiate(authoring, link_path, std::nullopt);
            require(hydration.scene.has_value()
                    && hydration.diagnostics.join('\n').contains(
                        QStringLiteral("DPE.PREFAB.OUTSIDE_PROJECT"))
                    && !instantiated.succeeded
                    && instantiated.diagnostics.join('\n').contains(
                        QStringLiteral("DPE.PREFAB.OUTSIDE_PROJECT"))
                    && file_bytes(outside_source) == outside_before,
                "A symlinked source escape was indexed, loaded, or modified.");
        }
    }
    {
        QTemporaryDir project;
        QTemporaryDir outside;
        require(project.isValid() && outside.isValid(), "Could not create backup containment test directories.");
        const auto source_path = write_source(project.path());
        const auto source_before = file_bytes(source_path);
        const auto marker_path = source_path + QStringLiteral(".dpeapply");
        const auto marker = QByteArrayLiteral("foreign pending marker\n");
        write_bytes(marker_path, marker);
        const auto outside_backup = QDir{outside.path()}.filePath(QStringLiteral("OutsideBackup.bin"));
        const auto backup = QByteArrayLiteral("outside recovery sentinel\n");
        write_bytes(outside_backup, backup);
        if (create_file_symlink(outside_backup, source_path + QStringLiteral(".dpebak")))
        {
            ++exercised;
            PrefabService service;
            service.set_project_root(project.path());
            scene stored{uuid::random_v4(), "Backup containment Scene"};
            const auto hydration = service.hydrate(stored);
            require(hydration.scene.has_value()
                    && hydration.diagnostics.join('\n').contains(
                        QStringLiteral("DPE.PREFAB.RECOVERY_OUTSIDE_PROJECT"))
                    && file_bytes(source_path) == source_before
                    && file_bytes(marker_path) == marker
                    && file_bytes(outside_backup) == backup,
                "Recovery followed or modified a backup symlink outside the project.");
        }
    }
    {
        QTemporaryDir project;
        QTemporaryDir outside;
        require(project.isValid() && outside.isValid(), "Could not create marker containment test directories.");
        const auto source_path = write_source(project.path());
        const auto source_before = file_bytes(source_path);
        const auto backup_path = source_path + QStringLiteral(".dpebak");
        const auto backup = QByteArrayLiteral("inside recovery sentinel\n");
        write_bytes(backup_path, backup);
        const auto outside_marker = QDir{outside.path()}.filePath(QStringLiteral("OutsideMarker.bin"));
        const auto marker = QByteArrayLiteral("outside marker sentinel\n");
        write_bytes(outside_marker, marker);
        if (create_file_symlink(outside_marker, source_path + QStringLiteral(".dpeapply")))
        {
            ++exercised;
            PrefabService service;
            service.set_project_root(project.path());
            scene stored{uuid::random_v4(), "Marker containment Scene"};
            const auto hydration = service.hydrate(stored);
            require(hydration.scene.has_value()
                    && hydration.diagnostics.join('\n').contains(
                        QStringLiteral("DPE.PREFAB.RECOVERY_OUTSIDE_PROJECT"))
                    && file_bytes(source_path) == source_before
                    && file_bytes(backup_path) == backup
                    && file_bytes(outside_marker) == marker,
                "Recovery followed or modified a marker symlink outside the project.");
        }
    }
    if (exercised == 0)
    {
        std::cout << "Prefab containment symlink cases skipped because this host disallows test symlinks.\n";
    }
}
}

int main()
{
    try
    {
        instantiate_override_save_reload_and_fallback();
        newer_and_incompatible_sources_retain_exact_fallback_until_rebase();
        apply_localizes_references_and_journals_source_history();
        cycle_and_depth_are_rejected_before_scene_mutation();
        create_source_undo_redo_and_apply_failure_are_atomic();
        injected_apply_failures_and_startup_recovery_preserve_both_documents();
        refresh_rejects_symlinked_source_and_recovery_escapes_when_supported();
        std::cout << "Prefab editor service passed stable instantiation, exact missing/newer/incompatible "
                     "fallback recovery, cycle/depth guards, override/revert/undo, inverse-reference Apply "
                     "validation, injected atomic source/scene rollback and startup recovery, saved-local "
                     "separation, and Unpack Completely.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
