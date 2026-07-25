#include "PrefabService.h"

#include <dragonpixel/metadata/builtin_ids.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
using dragonpixel::core::uuid;
using dragonpixel::prefab::document;
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
            && !missing_source_service.source_available_for_entity(instance_root),
        "Missing source did not materialize its last successful fallback.");
    auto missing_source_scene = std::move(*hydrated.scene);
    const auto unpacked = missing_source_service.unpack(missing_source_scene, instance_root, true);
    require(unpacked.succeeded && missing_source_scene.prefab_instances().empty()
            && missing_source_service.persistent_copy(missing_source_scene).entities().size() == 2,
        "Unpack Completely did not recover missing-source fallback entities as local state.");
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
}
}

int main()
{
    try
    {
        instantiate_override_save_reload_and_fallback();
        apply_localizes_references_and_journals_source_history();
        create_source_undo_redo_and_apply_failure_are_atomic();
        std::cout << "Prefab editor service passed stable instantiation, override/revert/undo, "
                     "inverse-reference Apply validation, source history/rollback, saved-local separation, "
                     "missing-source fallback, and Unpack Completely.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
