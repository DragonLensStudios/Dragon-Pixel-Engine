#include <dragonpixel/core/uuid.h>
#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/commands.h>
#include <dragonpixel/scene/scene.h>
#include <dragonpixel/serialization/atomic_file.h>
#include <dragonpixel/serialization/scene_json.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using dragonpixel::core::uuid;
using dragonpixel::metadata::runtime_owner;
using dragonpixel::scene::command;
using dragonpixel::scene::component_record;
using dragonpixel::scene::create_entity_command;
using dragonpixel::scene::create_preset_command;
using dragonpixel::scene::delete_subtree_command;
using dragonpixel::scene::duplicate_subtree_command;
using dragonpixel::scene::entity_id_remap;
using dragonpixel::scene::entity_preset;
using dragonpixel::scene::reparent_entity_command;
using dragonpixel::scene::reorder_entity_command;
using dragonpixel::scene::scene;
using dragonpixel::scene::upsert_component_command;

constexpr auto unknown_component_id = "e1a3d322-b5bc-40db-8a2a-b3041baa6402";

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error{message};
    }
}

uuid parse_uuid(const char* value)
{
    const auto parsed = uuid::parse(value);
    require(parsed.has_value(), "Test fixture UUID was invalid.");
    return *parsed;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

scene create_sample_scene()
{
    const auto scene_id = parse_uuid("a7b88e34-56ec-42bb-8894-fd3e8c48213c");
    const auto root_id = parse_uuid("b3a46ac0-d9c1-4d39-8624-6ba901ece75c");
    const auto child_id = parse_uuid("5e08f200-eab4-4b99-b1ac-06aab424047d");
    scene result{scene_id, "Slice 1 Sample"};
    require(result.apply(command{create_entity_command{root_id, "Root", std::nullopt}}).succeeded,
        "Could not create root entity.");
    require(result.apply(command{create_entity_command{child_id, "Dragon", root_id}}).succeeded,
        "Could not create child entity.");
    require(!result.apply(command{reparent_entity_command{root_id, child_id}}).succeeded,
        "Hierarchy cycle was accepted.");

    component_record transform{
        std::string{dragonpixel::metadata::builtin_component_ids::transform},
        2,
        runtime_owner::native,
        {
            {"dpe.transform.position", {{"x", 1.25}, {"y", 2.5}, {"z", -3.75}}},
            {"dpe.transform.rotation", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"w", 1.0}}},
            {"dpe.transform.scale", {{"x", 1.0}, {"y", 1.0}, {"z", 1.0}}},
        },
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Native.TransformComponent",
    };
    component_record rotator{
        std::string{dragonpixel::metadata::builtin_component_ids::rotator},
        1,
        runtime_owner::managed,
        {
            {"dpe.rotator.degrees_per_second", 22.5},
            {"dpe.rotator.target", root_id.to_string()},
        },
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Managed.RotatorComponent",
    };
    auto unknown_raw = nlohmann::ordered_json{
        {"typeId", unknown_component_id},
        {"qualifiedName", "Vendor.Missing.SparkleComponent"},
        {"schemaVersion", 17},
        {"owner", "unknown"},
        {"enabled", true},
        {"vendorExtension", {{"preserve", true}}},
        {"properties", {{"precise", 0.1234567890123456789}, {"nested", {1, "two", nullptr}}}},
    };
    component_record unknown{
        unknown_component_id,
        17,
        runtime_owner::native,
        unknown_raw.at("properties"),
        true,
        unknown_raw,
        true,
        "Vendor.Missing.SparkleComponent",
    };
    component_record newer{
        std::string{dragonpixel::metadata::builtin_component_ids::transform},
        99,
        runtime_owner::native,
        {{"future", {{"value", 4}}}},
        true,
        {
            {"typeId", dragonpixel::metadata::builtin_component_ids::transform},
            {"qualifiedName", "DragonPixel.Native.TransformComponent"},
            {"schemaVersion", 99},
            {"owner", "native"},
            {"enabled", false},
            {"properties", {{"future", {{"value", 4}}}}},
        },
        false,
        "DragonPixel.Native.TransformComponent",
    };
    require(result.apply(command{upsert_component_command{child_id, transform}}).succeeded,
        "Could not add native component.");
    require(result.apply(command{upsert_component_command{child_id, rotator}}).succeeded,
        "Could not add managed component.");
    require(result.apply(command{upsert_component_command{child_id, unknown}}).succeeded,
        "Could not add unknown component.");
    require(result.apply(command{upsert_component_command{root_id, newer}}).succeeded,
        "Could not add newer component.");
    return result;
}

const component_record* find_component(const dragonpixel::scene::entity& value, std::string_view type_id)
{
    const auto found = std::find_if(value.components.begin(), value.components.end(), [&](const auto& item) {
        return item.type_id == type_id;
    });
    return found == value.components.end() ? nullptr : &*found;
}

void verify_scene_round_trip()
{
    const auto registry = dragonpixel::metadata::registry::slice_one_defaults();
    require(registry.size() >= 7, "Default metadata registration failed.");
    require(registry.find(dragonpixel::metadata::builtin_component_ids::transform)->schema_version == 2,
        "Transform schema version was wrong.");
    const auto* camera_descriptor = registry.find(dragonpixel::metadata::builtin_component_ids::camera);
    require(camera_descriptor != nullptr, "Camera metadata registration failed.");
    const auto primary_property = std::find_if(
        camera_descriptor->properties.begin(),
        camera_descriptor->properties.end(),
        [](const auto& property) { return property.property_id == "dpe.camera.primary"; });
    require(primary_property != camera_descriptor->properties.end()
            && primary_property->default_json == "true",
        "Camera metadata did not provide a valid primary-camera default.");
    const auto sprite_descriptor = registry.find(dragonpixel::metadata::builtin_component_ids::sprite);
    require(sprite_descriptor != nullptr, "Sprite metadata registration failed.");
    const auto sprite_asset = std::find_if(
        sprite_descriptor->properties.begin(),
        sprite_descriptor->properties.end(),
        [](const auto& property) { return property.property_id == "dpe.sprite.asset"; });
    require(sprite_asset != sprite_descriptor->properties.end()
            && sprite_asset->reference_filter == "sprite"
            && !sprite_asset->default_json.empty(),
        "Sprite metadata did not provide its filtered asset-reference contract.");

    const auto source = create_sample_scene();
    const auto first_json = dragonpixel::serialization::write_scene_json(source);
    const auto loaded = dragonpixel::serialization::read_scene_json(first_json, registry);
    const auto load_message = loaded.diagnostics.empty()
        ? std::string{"Serialized scene did not reload."}
        : std::string{"Serialized scene did not reload: "} + loaded.diagnostics.front().code + " "
            + loaded.diagnostics.front().message + " " + loaded.diagnostics.front().context;
    require(loaded.value.has_value(), load_message);
    require(loaded.diagnostics.empty(), "Serialized scene produced load diagnostics.");
    const auto second_json = dragonpixel::serialization::write_scene_json(*loaded.value);
    require(first_json == second_json, "Scene JSON was not byte-deterministic after reload.");

    const auto parsed = nlohmann::ordered_json::parse(second_json);
    require(parsed.at("$schema") == "https://dragonpixel.dev/schemas/v3/scene.schema.json"
            && parsed.at("engineVersion") == "0.2.0-slice2"
            && parsed.at("formatVersion") == 3 && parsed.at("entities").at(1).at("enabled") == true
            && parsed.at("entities").at(1).at("siblingOrder") == 0
            && parsed.at("physicsSettings").at("maxCatchUpTicks") == 4
            && parsed.at("prefabInstances").empty(),
        "Scene v3 ordering/physics/prefab envelope was not serialized.");
    const auto& components = parsed.at("entities").at(1).at("components");
    const auto unknown = std::find_if(components.begin(), components.end(), [](const auto& item) {
        return item.at("typeId") == unknown_component_id;
    });
    require(unknown != components.end(), "Unknown component disappeared.");
    require(unknown->at("vendorExtension").at("preserve") == true, "Unknown extension was not preserved.");
    require(unknown->at("properties").at("nested").at(2).is_null(), "Unknown nested payload changed.");
    require(unknown->at("qualifiedName") == "Vendor.Missing.SparkleComponent" && unknown->at("enabled") == true,
        "Opaque component identity or enabled state changed.");

    auto missing_schema = parsed;
    missing_schema.erase("$schema");
    const auto rejected = dragonpixel::serialization::read_scene_json(missing_schema.dump(), registry);
    require(!rejected.value.has_value() && !rejected.diagnostics.empty(),
        "Scene v3 without its canonical schema URI was accepted.");
}

void verify_component_migration()
{
    const auto registry = dragonpixel::metadata::registry::slice_one_defaults();
    auto root = nlohmann::ordered_json{
        {"format", "dpe.scene"},
        {"formatVersion", 1},
        {"sceneId", "a7b88e34-56ec-42bb-8894-fd3e8c48213c"},
        {"name", "Legacy"},
        {"entities", nlohmann::ordered_json::array({
            {
                {"id", "b3a46ac0-d9c1-4d39-8624-6ba901ece75c"},
                {"name", "Legacy entity"},
                {"parentId", nullptr},
                {"components", nlohmann::ordered_json::array({
                    {
                        {"typeId", dragonpixel::metadata::builtin_component_ids::transform},
                        {"schemaVersion", 1},
                        {"owner", "native"},
                        {"properties", {{"dpe.transform.translation", {{"x", 4}, {"y", 5}, {"z", 6}}}}},
                    },
                })},
            },
        })},
    };
    const auto loaded = dragonpixel::serialization::read_scene_json(root.dump(), registry);
    require(loaded.value.has_value() && loaded.migrations.size() == 3,
        "Scene v1-to-v3 and Transform migrations did not each run once.");
    const auto migrated = nlohmann::ordered_json::parse(dragonpixel::serialization::write_scene_json(*loaded.value));
    const auto& component = migrated.at("entities").at(0).at("components").at(0);
    require(component.at("schemaVersion") == 2, "Migrated component version was not advanced.");
    require(component.at("properties").contains("dpe.transform.position"), "Migrated property ID was missing.");
    require(!component.at("properties").contains("dpe.transform.translation"), "Legacy property ID survived migration.");
}

void verify_scene_v2_migration_and_v3_preservation()
{
    const auto registry = dragonpixel::metadata::registry::slice_one_defaults();
    auto legacy = nlohmann::ordered_json{
        {"$schema", "https://dragonpixel.dev/schemas/v2/scene.schema.json"},
        {"format", "dpe.scene"},
        {"formatVersion", 2},
        {"engineVersion", "0.1.0-slice1"},
        {"sceneId", "20000000-0000-4000-8000-000000000001"},
        {"name", "Version two"},
        {"entities", nlohmann::ordered_json::array({
            {
                {"id", "20000000-0000-4000-8000-000000000011"},
                {"name", "First root"},
                {"parentId", nullptr},
                {"enabled", true},
                {"components", nlohmann::ordered_json::array()},
            },
            {
                {"id", "20000000-0000-4000-8000-000000000012"},
                {"name", "Second root"},
                {"parentId", nullptr},
                {"enabled", true},
                {"components", nlohmann::ordered_json::array()},
            },
            {
                {"id", "20000000-0000-4000-8000-000000000013"},
                {"name", "Child"},
                {"parentId", "20000000-0000-4000-8000-000000000011"},
                {"enabled", true},
                {"components", nlohmann::ordered_json::array()},
            },
        })},
    };
    const auto migrated = dragonpixel::serialization::read_scene_json(legacy.dump(), registry);
    require(migrated.value.has_value() && migrated.migrations.size() == 1,
        "Scene v2 did not report exactly one document migration to v3.");
    require(migrated.value->entities()[0].sibling_order == 0
            && migrated.value->entities()[1].sibling_order == 1
            && migrated.value->entities()[2].sibling_order == 0,
        "Scene v2 storage order was not deterministically migrated per parent.");

    auto version_three = nlohmann::ordered_json::parse(
        dragonpixel::serialization::write_scene_json(*migrated.value));
    version_three["physicsSettings"]["fixedTimeStepSeconds"] = 0.02;
    version_three["physicsSettings"]["gravity2D"]["y"] = -12.5;
    version_three["physicsSettings"]["gravity3D"]["z"] = 1.25;
    version_three["prefabInstances"] = nlohmann::ordered_json::array({
        {
            {"instanceId", "20000000-0000-4000-8000-000000000021"},
            {"sourceAssetId", "20000000-0000-4000-8000-000000000022"},
            {"vendorFallback", {{"preserve", true}, {"payload", {1, "two", nullptr}}}},
        },
    });
    const auto loaded = dragonpixel::serialization::read_scene_json(version_three.dump(), registry);
    require(loaded.value.has_value() && loaded.migrations.empty(), "Valid scene v3 did not load directly.");
    require(loaded.value->physics_settings().fixed_time_step_seconds == 0.02
            && loaded.value->physics_settings().gravity_2d.y == -12.5
            && loaded.value->physics_settings().gravity_3d.z == 1.25,
        "Scene v3 physics settings changed during load.");
    require(loaded.value->prefab_instances().at(0).at("vendorFallback").at("preserve") == true,
        "Opaque linked-prefab fallback data was not retained.");
    const auto reloaded = dragonpixel::serialization::read_scene_json(
        dragonpixel::serialization::write_scene_json(*loaded.value), registry);
    require(reloaded.value.has_value()
            && reloaded.value->prefab_instances() == loaded.value->prefab_instances(),
        "Scene v3 prefab instance records did not round-trip losslessly.");
}

void verify_history_validation_and_presets()
{
    scene value{parse_uuid("30000000-0000-4000-8000-000000000001"), "History"};
    const auto empty_id = parse_uuid("30000000-0000-4000-8000-000000000011");
    const auto sprite_id = parse_uuid("30000000-0000-4000-8000-000000000012");
    const std::vector<command> create_presets{
        create_preset_command{empty_id, "Empty GameObject", entity_preset::empty},
        create_preset_command{
            sprite_id,
            "Sprite GameObject",
            entity_preset::sprite,
            std::nullopt,
            std::nullopt,
            std::string{"30000000-0000-4000-8000-000000000099"},
        },
    };

    value.mark_savepoint();
    require(!value.is_dirty() && !value.can_undo(), "New scene did not start at a clean savepoint.");
    const auto preview = value.dry_run_transaction(create_presets);
    require(preview.succeeded && value.entities().empty() && value.history_size() == 0,
        "Dry-run validation mutated authoritative scene state or history.");
    const auto committed = value.apply_transaction(create_presets, "Create two GameObjects");
    require(committed.succeeded && committed.applied_count == 2
            && value.history_size() == 1 && value.history_position() == 1 && value.is_dirty(),
        "Compound preset transaction was not recorded as one dirty history item.");
    const auto* empty = value.find_entity(empty_id);
    const auto* sprite = value.find_entity(sprite_id);
    const auto* sprite_component = sprite == nullptr ? nullptr
        : find_component(*sprite, dragonpixel::metadata::builtin_component_ids::sprite);
    require(empty != nullptr && sprite != nullptr
            && find_component(*empty, dragonpixel::metadata::builtin_component_ids::transform) != nullptr
            && find_component(*sprite, dragonpixel::metadata::builtin_component_ids::transform) != nullptr
            && sprite_component != nullptr,
        "GameObject presets did not include mandatory Transform and preset components.");
    require(sprite_component->properties.at("dpe.sprite.asset")
                == "30000000-0000-4000-8000-000000000099",
        "The transactional Sprite preset did not preserve its requested primitive/asset binding.");

    require(value.undo().succeeded && value.entities().empty() && !value.is_dirty() && value.can_redo(),
        "Undo did not restore the initial clean savepoint for a compound transaction.");
    require(value.redo().succeeded && value.entities().size() == 2 && value.is_dirty(),
        "Redo did not restore the complete compound transaction.");
    value.mark_savepoint();
    require(!value.is_dirty(), "Marking the current history position as saved did not clear dirty state.");

    const auto original_history_size = value.history_size();
    require(value.apply(command{dragonpixel::scene::rename_entity_command{empty_id, "Empty GameObject"}}).succeeded
            && value.history_size() == original_history_size && !value.is_dirty(),
        "A no-op command incorrectly created history or dirty state.");
    require(value.apply(command{dragonpixel::scene::rename_entity_command{empty_id, "Renamed"}}).succeeded
            && value.is_dirty(),
        "A real edit did not advance dirty history.");
    require(value.undo().succeeded && !value.is_dirty(), "Undo to the savepoint did not clear dirty state.");
    require(value.apply(command{dragonpixel::scene::set_entity_enabled_command{empty_id, false}}).succeeded
            && !value.can_redo() && value.is_dirty(),
        "A new edit after Undo did not truncate the redo branch.");

    const auto history_before_rejection = value.history_size();
    const std::vector<command> invalid{
        dragonpixel::scene::rename_entity_command{empty_id, "Must roll back"},
        reparent_entity_command{empty_id, parse_uuid("30000000-0000-4000-8000-000000000099")},
    };
    require(!value.dry_run_transaction(invalid).succeeded
            && !value.apply_transaction(invalid).succeeded
            && value.find_entity(empty_id)->name == "Empty GameObject"
            && value.history_size() == history_before_rejection,
        "Rejected validation/transaction leaked state or history.");
}

void verify_hierarchy_duplicate_and_delete()
{
    const auto root_id = parse_uuid("40000000-0000-4000-8000-000000000011");
    const auto external_id = parse_uuid("40000000-0000-4000-8000-000000000012");
    const auto child_id = parse_uuid("40000000-0000-4000-8000-000000000013");
    const auto grandchild_id = parse_uuid("40000000-0000-4000-8000-000000000014");
    const auto duplicate_root_id = parse_uuid("40000000-0000-4000-8000-000000000021");
    const auto duplicate_child_id = parse_uuid("40000000-0000-4000-8000-000000000022");
    const auto duplicate_grandchild_id = parse_uuid("40000000-0000-4000-8000-000000000023");
    scene value{parse_uuid("40000000-0000-4000-8000-000000000001"), "Hierarchy"};

    require(value.apply(command{create_preset_command{root_id, "Root", entity_preset::empty}}).succeeded
            && value.apply(command{create_preset_command{external_id, "External", entity_preset::empty}}).succeeded
            && value.apply(command{create_preset_command{child_id, "Child", entity_preset::empty, root_id}}).succeeded
            && value.apply(command{create_preset_command{grandchild_id, "Grandchild", entity_preset::empty, child_id}}).succeeded,
        "Could not build hierarchy fixture.");

    component_record child_rotator{
        std::string{dragonpixel::metadata::builtin_component_ids::rotator},
        1,
        runtime_owner::managed,
        {{"dpe.rotator.degrees_per_second", 15.0}, {"dpe.rotator.target", grandchild_id.to_string()}},
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Managed.RotatorComponent",
    };
    component_record external_rotator = child_rotator;
    external_rotator.properties["dpe.rotator.target"] = child_id.to_string();
    auto opaque_raw = nlohmann::ordered_json{
        {"typeId", unknown_component_id},
        {"qualifiedName", "Vendor.Opaque.Reference"},
        {"schemaVersion", 9},
        {"owner", "unknown"},
        {"enabled", true},
        {"properties", {{"untypedEntity", child_id.to_string()}, {"keep", true}}},
        {"vendorData", {{"root", root_id.to_string()}}},
    };
    component_record opaque{
        unknown_component_id,
        9,
        runtime_owner::native,
        opaque_raw.at("properties"),
        true,
        opaque_raw,
        true,
        "Vendor.Opaque.Reference",
    };
    require(value.apply(command{upsert_component_command{child_id, child_rotator}}).succeeded
            && value.apply(command{upsert_component_command{external_id, external_rotator}}).succeeded
            && value.apply(command{upsert_component_command{grandchild_id, opaque}}).succeeded,
        "Could not attach duplication/reference fixtures.");

    const auto history_before_invalid_move = value.history_size();
    require(value.apply(command{reorder_entity_command{external_id, 0}}).succeeded
            && value.find_entity(external_id)->sibling_order == 0
            && value.find_entity(root_id)->sibling_order == 1,
        "Root sibling reorder did not update explicit order.");
    require(value.apply(command{reparent_entity_command{external_id, root_id, 0}}).succeeded
            && value.find_entity(external_id)->parent_id == root_id
            && value.find_entity(external_id)->sibling_order == 0
            && value.find_entity(child_id)->sibling_order == 1,
        "Reparent with an explicit sibling insertion point failed.");
    require(!value.apply(command{reparent_entity_command{root_id, grandchild_id}}).succeeded
            && value.history_size() == history_before_invalid_move + 2,
        "Hierarchy cycle rejection changed state or history.");
    require(value.undo().succeeded && !value.find_entity(external_id)->parent_id
            && value.find_entity(external_id)->sibling_order == 0,
        "Undo did not restore reparent and root ordering.");

    const duplicate_subtree_command duplicate{
        root_id,
        {
            entity_id_remap{root_id, duplicate_root_id},
            entity_id_remap{child_id, duplicate_child_id},
            entity_id_remap{grandchild_id, duplicate_grandchild_id},
        },
    };
    require(value.apply(command{duplicate}, "Duplicate hierarchy").succeeded,
        "Subtree duplication with deterministic caller-supplied IDs failed.");
    const auto* duplicated_root = value.find_entity(duplicate_root_id);
    const auto* duplicated_child = value.find_entity(duplicate_child_id);
    const auto* duplicated_grandchild = value.find_entity(duplicate_grandchild_id);
    require(duplicated_root != nullptr && duplicated_child != nullptr && duplicated_grandchild != nullptr
            && duplicated_root->name == "Root Copy"
            && duplicated_child->parent_id == duplicate_root_id
            && duplicated_grandchild->parent_id == duplicate_child_id,
        "Duplicated hierarchy identities or parents were incorrect.");
    const auto* duplicated_rotator = find_component(
        *duplicated_child, dragonpixel::metadata::builtin_component_ids::rotator);
    const auto* duplicated_opaque = find_component(*duplicated_grandchild, unknown_component_id);
    require(duplicated_rotator != nullptr
            && duplicated_rotator->properties.at("dpe.rotator.target") == duplicate_grandchild_id.to_string(),
        "Known entity reference values were not remapped inside the duplicated subtree.");
    require(duplicated_opaque != nullptr && duplicated_opaque->raw_record == opaque_raw,
        "Opaque component payload was altered while duplicating its entity.");

    const auto before_delete = dragonpixel::serialization::write_scene_json(value);
    require(value.apply(command{delete_subtree_command{root_id}}, "Delete subtree").succeeded
            && value.find_entity(root_id) == nullptr
            && value.find_entity(child_id) == nullptr
            && value.find_entity(grandchild_id) == nullptr,
        "Subtree delete did not remove every descendant.");
    const auto* retained_external = value.find_entity(external_id);
    const auto* retained_reference = find_component(
        *retained_external, dragonpixel::metadata::builtin_component_ids::rotator);
    require(retained_reference != nullptr
            && retained_reference->properties.at("dpe.rotator.target") == child_id.to_string(),
        "Subtree delete erased an incoming UUID instead of preserving a repairable dangling reference.");
    require(value.undo().succeeded
            && dragonpixel::serialization::write_scene_json(value) == before_delete,
        "Undo did not restore deleted descendants, opaque payloads, references, and ordering exactly.");
}

void verify_atomic_transactions()
{
    const auto root_id = parse_uuid("b3a46ac0-d9c1-4d39-8624-6ba901ece75c");
    const auto child_id = parse_uuid("5e08f200-eab4-4b99-b1ac-06aab424047d");
    auto value = create_sample_scene();

    const std::vector<command> accepted{
        dragonpixel::scene::rename_entity_command{child_id, "Transaction committed"},
        dragonpixel::scene::set_entity_enabled_command{child_id, false},
        dragonpixel::scene::set_component_enabled_command{
            child_id, std::string{dragonpixel::metadata::builtin_component_ids::rotator}, false},
    };
    const auto accepted_result = value.apply_transaction(accepted);
    require(accepted_result.succeeded && accepted_result.applied_count == accepted.size(),
        "Valid command transaction did not commit atomically.");
    const auto* committed = value.find_entity(child_id);
    require(committed != nullptr && committed->name == "Transaction committed" && !committed->enabled,
        "Committed entity state was incomplete.");

    const std::vector<command> rejected{
        dragonpixel::scene::rename_entity_command{child_id, "Must roll back"},
        reparent_entity_command{root_id, child_id},
    };
    const auto rejected_result = value.apply_transaction(rejected);
    require(!rejected_result.succeeded && rejected_result.applied_count == 1,
        "Invalid command transaction did not report its rejected operation.");
    require(value.find_entity(child_id)->name == "Transaction committed",
        "Rejected transaction leaked a partial entity rename.");

    const auto before_order = value.find_entity(child_id)->components;
    require(before_order.size() >= 2, "Component reorder fixture was incomplete.");
    require(value.apply(command{dragonpixel::scene::reorder_component_command{
                child_id, before_order.front().type_id, before_order.size() - 1}},
                "Reorder component").succeeded,
        "Valid component reorder was rejected.");
    require(value.find_entity(child_id)->components.back().type_id == before_order.front().type_id,
        "Component reorder did not move the requested record.");
    require(value.undo().succeeded && value.find_entity(child_id)->components == before_order,
        "Undo did not restore exact component ordering and payloads.");
}

void verify_prefab_instance_command_history()
{
    const auto scene_id = parse_uuid("88888888-8888-4888-8888-888888888888");
    scene value{scene_id, "Prefab command test"};
    const auto opaque_instances = nlohmann::ordered_json::array({
        {
            {"instanceId", "99999999-9999-4999-8999-999999999999"},
            {"sourceAssetId", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"},
            {"vendorExtension", {
                {"opaque", true},
                {"ordering", nlohmann::ordered_json::array({3, 1, 2})},
            }},
        },
    });

    const auto applied = value.apply(
        command{dragonpixel::scene::set_prefab_instances_command{opaque_instances}},
        "Set linked prefab instances");
    require(applied.succeeded, "Typed prefab instance command was rejected.");
    require(value.prefab_instances() == opaque_instances && value.is_dirty(),
        "Typed prefab command did not preserve the complete opaque record.");
    require(value.undo().succeeded && value.prefab_instances().empty() && !value.is_dirty(),
        "Undo did not restore the exact prefab before-image and clean savepoint.");
    require(value.redo().succeeded && value.prefab_instances() == opaque_instances,
        "Redo did not restore the exact opaque prefab record.");

    const auto rejected = value.apply(command{dragonpixel::scene::set_prefab_instances_command{
        nlohmann::ordered_json::object()}});
    require(!rejected.succeeded && value.prefab_instances() == opaque_instances,
        "A non-array prefab envelope was accepted or mutated state on failure.");
}

void verify_atomic_save(const std::filesystem::path& root)
{
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto target = root / "sample.dpescene";
    require(dragonpixel::serialization::save_utf8_atomic(target, "first\n").succeeded, "Initial atomic save failed.");
    require(read_file(target) == "first\n", "Initial save contents were wrong.");
    require(dragonpixel::serialization::save_utf8_atomic(target, "second\n").succeeded, "Replacement atomic save failed.");
    require(read_file(target) == "second\n", "Replacement contents were wrong.");

    const auto failed = dragonpixel::serialization::save_utf8_atomic(
        target,
        "must-not-appear\n",
        dragonpixel::serialization::save_fault::after_temporary_flush);
    require(!failed.succeeded && read_file(target) == "second\n", "Injected save failure damaged the valid target.");

    std::filesystem::remove(target);
    require(dragonpixel::serialization::recover_backup(target).succeeded, "Backup recovery failed.");
    require(read_file(target) == "first\n", "Backup did not retain the previous valid document.");
}

void verify_atomic_multi_document_save(const std::filesystem::path& root)
{
    const auto transaction_root = root / "transaction";
    std::error_code error;
    std::filesystem::remove_all(transaction_root, error);
    std::filesystem::create_directories(transaction_root);
    const auto scene_target = transaction_root / "sample.dpescene";
    const auto tile_target = transaction_root / "sample.dpetilemap";
    require(dragonpixel::serialization::save_utf8_atomic(scene_target, "scene-before\n").succeeded,
        "Could not arrange the transaction scene fixture.");
    require(dragonpixel::serialization::save_utf8_atomic(tile_target, "tile-before\n").succeeded,
        "Could not arrange the transaction tile fixture.");

    const std::vector<dragonpixel::serialization::utf8_transaction_write> committed{
        {scene_target, "scene-after\n"},
        {tile_target, "tile-after\n"},
    };
    const auto committed_result =
        dragonpixel::serialization::save_utf8_transaction(committed, transaction_root);
    require(committed_result.succeeded,
        "The valid multi-document save transaction failed: " + committed_result.error);
    require(read_file(scene_target) == "scene-after\n" && read_file(tile_target) == "tile-after\n",
        "The valid multi-document transaction did not commit every target.");
    auto scene_backup = scene_target;
    scene_backup += ".bak";
    auto tile_backup = tile_target;
    tile_backup += ".bak";
    require(read_file(scene_backup) == "scene-before\n" && read_file(tile_backup) == "tile-before\n",
        "A successful transaction did not retain exact bounded per-target recovery backups.");

    const std::vector<dragonpixel::serialization::utf8_transaction_write> rejected{
        {scene_target, "scene-must-not-appear\n"},
        {tile_target, "tile-must-not-appear\n"},
    };
    const auto failed = dragonpixel::serialization::save_utf8_transaction(
        rejected,
        transaction_root,
        dragonpixel::serialization::transaction_save_fault::after_first_replace);
    require(!failed.succeeded && failed.error.find("restored") != std::string::npos,
        "The injected transaction failure did not report successful recovery.");
    require(read_file(scene_target) == "scene-after\n" && read_file(tile_target) == "tile-after\n",
        "The injected transaction failure leaked partially committed contents.");

    const auto staging_failed = dragonpixel::serialization::save_utf8_transaction(
        rejected,
        transaction_root,
        dragonpixel::serialization::transaction_save_fault::after_staging);
    require(!staging_failed.succeeded
            && read_file(scene_target) == "scene-after\n"
            && read_file(tile_target) == "tile-after\n",
        "The injected staging failure changed a transaction target.");

    const auto new_target = transaction_root / "new-document.json";
    const std::vector<dragonpixel::serialization::utf8_transaction_write> rejected_creation{
        {new_target, "new-must-not-appear\n"},
        {scene_target, "scene-must-not-appear\n"},
    };
    require(!dragonpixel::serialization::save_utf8_transaction(
                 rejected_creation,
                 transaction_root,
                 dragonpixel::serialization::transaction_save_fault::after_first_replace)
                 .succeeded,
        "The injected new-file transaction unexpectedly succeeded.");
    require(!std::filesystem::exists(new_target) && read_file(scene_target) == "scene-after\n",
        "Rollback did not remove a newly created transaction target.");

    const std::vector<dragonpixel::serialization::utf8_transaction_write> duplicate{
        {scene_target, "one\n"},
        {scene_target.parent_path() / "." / scene_target.filename(), "two\n"},
    };
    require(!dragonpixel::serialization::save_utf8_transaction(duplicate, transaction_root).succeeded
            && read_file(scene_target) == "scene-after\n",
        "A duplicate transaction target was accepted or changed the valid document.");

    const auto artifacts = transaction_root / ".dragonpixel" / "Recovery" / "Transactions";
    const auto artifact_count = [&] {
        if (!std::filesystem::exists(artifacts))
        {
            return std::size_t{};
        }
        return static_cast<std::size_t>(std::distance(
            std::filesystem::directory_iterator{artifacts}, std::filesystem::directory_iterator{}));
    };
    require(artifact_count() == 0, "Completed or synchronously recovered transactions left artifacts behind.");

    const std::vector<dragonpixel::serialization::utf8_transaction_write> interrupted{
        {scene_target, "scene-interrupted\n"},
        {tile_target, "tile-interrupted\n"},
    };
    const auto interrupted_result = dragonpixel::serialization::save_utf8_transaction(
        interrupted,
        transaction_root,
        dragonpixel::serialization::transaction_save_fault::leave_interrupted_after_first_replace);
    require(!interrupted_result.succeeded
            && read_file(scene_target) == "scene-interrupted\n"
            && read_file(tile_target) == "tile-after\n"
            && artifact_count() == 1,
        "The interruption seam did not leave the expected durable partial transaction.");
    const auto transaction_directory = *std::filesystem::directory_iterator{artifacts};
    const auto journal_path = transaction_directory.path() / "journal.json";
    const auto journal = nlohmann::ordered_json::parse(read_file(journal_path));
    require(journal.at("format") == "dpe.utf8-transaction-journal"
            && journal.at("formatVersion") == 1
            && journal.at("phase") == "prepared"
            && journal.at("entries").at(0).at("preimageSha256")
                == "1126ad4fc09a81f331c6493165cb804ca9a5673f0e0488fea2dc8a36c0498c9a"
            && journal.at("entries").at(0).at("postimageSha256")
                == "427b894085bbb69babd0503c4401731b8d7cf33a596d7be0b4b1ead9b277f07b",
        "The interruption seam did not retain a versioned prepared journal with standard SHA-256 hashes.");

    const auto protected_target = transaction_root / "protected-from-journal-cleanup.json";
    require(dragonpixel::serialization::save_utf8_atomic(protected_target, "protected\n").succeeded,
        "Could not arrange the forged-journal cleanup fixture.");
    auto forged_journal = journal;
    forged_journal["phase"] = "committed";
    forged_journal["entries"][0]["staged"] = "protected-from-journal-cleanup.json";
    require(dragonpixel::serialization::save_utf8_atomic(
                journal_path, forged_journal.dump(2) + "\n").succeeded,
        "Could not arrange the forged-journal cleanup fixture.");
    require(!dragonpixel::serialization::recover_utf8_transactions(transaction_root).succeeded
            && read_file(protected_target) == "protected\n"
            && read_file(scene_target) == "scene-interrupted\n"
            && read_file(tile_target) == "tile-after\n",
        "A structurally invalid committed journal deleted or changed a contained project file.");
    require(std::filesystem::remove(journal_path)
            && dragonpixel::serialization::recover_backup(journal_path).succeeded,
        "Could not restore the valid prepared journal after the adversarial recovery check.");
    require(dragonpixel::serialization::save_utf8_atomic(tile_target, "external-change\n").succeeded,
        "Could not arrange the unexpected-target recovery fixture.");
    require(!dragonpixel::serialization::recover_utf8_transactions(transaction_root).succeeded
            && read_file(scene_target) == "scene-interrupted\n"
            && read_file(tile_target) == "external-change\n",
        "Recovery overwrote an unexpected target or partially restored another target.");
    require(dragonpixel::serialization::save_utf8_atomic(tile_target, "tile-after\n").succeeded,
        "Could not restore the expected pre-image for startup recovery.");
    require(dragonpixel::serialization::recover_utf8_transactions(transaction_root).succeeded,
        "Fresh startup-style transaction recovery failed.");
    require(read_file(scene_target) == "scene-after\n"
            && read_file(tile_target) == "tile-after\n"
            && artifact_count() == 0,
        "Startup-style recovery did not restore the exact transaction pre-images.");
    require(dragonpixel::serialization::recover_utf8_transactions(transaction_root).succeeded,
        "Transaction recovery was not idempotent after cleanup.");

    const auto interrupted_new_target = transaction_root / "interrupted-new.json";
    const std::vector<dragonpixel::serialization::utf8_transaction_write> interrupted_creation{
        {interrupted_new_target, "created-before-interruption\n"},
        {scene_target, "scene-must-not-appear\n"},
    };
    require(!dragonpixel::serialization::save_utf8_transaction(
                 interrupted_creation,
                 transaction_root,
                 dragonpixel::serialization::transaction_save_fault::leave_interrupted_after_first_replace)
                 .succeeded
            && std::filesystem::exists(interrupted_new_target),
        "The new-target interruption seam did not leave its recoverable created target.");
    require(dragonpixel::serialization::recover_utf8_transactions(transaction_root).succeeded
            && !std::filesystem::exists(interrupted_new_target)
            && read_file(scene_target) == "scene-after\n",
        "Startup recovery did not remove a transaction-created target or preserve the existing target.");

    const auto collision_target = transaction_root / "collision.dpescene";
    auto collision_backup = collision_target;
    collision_backup += ".bak";
    require(dragonpixel::serialization::save_utf8_atomic(collision_target, "collision-target-before\n").succeeded
            && dragonpixel::serialization::save_utf8_atomic(collision_backup, "collision-backup-before\n").succeeded,
        "Could not arrange the target/backup collision fixture.");
    const std::vector<dragonpixel::serialization::utf8_transaction_write> artifact_collision{
        {collision_target, "collision-target-after\n"},
        {collision_backup, "collision-backup-after\n"},
    };
    require(!dragonpixel::serialization::save_utf8_transaction(artifact_collision, transaction_root).succeeded
            && read_file(collision_target) == "collision-target-before\n"
            && read_file(collision_backup) == "collision-backup-before\n",
        "A target/recovery-backup intersection was accepted or damaged a pre-image.");

    const auto hardlink_source = transaction_root / "hardlink-source.dpescene";
    const auto hardlink_alias = transaction_root / "hardlink-alias.dpescene";
    require(dragonpixel::serialization::save_utf8_atomic(hardlink_source, "hardlink-before\n").succeeded,
        "Could not arrange the hard-link alias fixture.");
    std::filesystem::create_hard_link(hardlink_source, hardlink_alias, error);
    require(!error, "The baseline filesystem could not create the hard-link alias fixture.");
    const std::vector<dragonpixel::serialization::utf8_transaction_write> hardlink_collision{
        {hardlink_source, "hardlink-source-after\n"},
        {hardlink_alias, "hardlink-alias-after\n"},
    };
    require(!dragonpixel::serialization::save_utf8_transaction(hardlink_collision, transaction_root).succeeded
            && read_file(hardlink_source) == "hardlink-before\n"
            && read_file(hardlink_alias) == "hardlink-before\n",
        "Resolved hard-link aliases were accepted as different transaction targets.");

    const auto symlink_alias = transaction_root / "symlink-alias.dpescene";
    error.clear();
    std::filesystem::create_symlink(hardlink_source, symlink_alias, error);
    if (!error)
    {
        const std::vector<dragonpixel::serialization::utf8_transaction_write> symlink_collision{
            {symlink_alias, "symlink-must-not-appear\n"},
        };
        require(!dragonpixel::serialization::save_utf8_transaction(symlink_collision, transaction_root).succeeded
                && read_file(hardlink_source) == "hardlink-before\n",
            "A symbolic-link transaction target was accepted or changed its referent.");
        std::filesystem::remove(symlink_alias, error);
    }

#if defined(_WIN32) || defined(__APPLE__)
    const auto upper_case_target = transaction_root / "CaseOnly.dpescene";
    const auto lower_case_target = transaction_root / "caseonly.dpescene";
    const std::vector<dragonpixel::serialization::utf8_transaction_write> case_collision{
        {upper_case_target, "upper\n"},
        {lower_case_target, "lower\n"},
    };
    require(!dragonpixel::serialization::save_utf8_transaction(case_collision, transaction_root).succeeded
            && !std::filesystem::exists(upper_case_target)
            && !std::filesystem::exists(lower_case_target),
        "Case-folded transaction aliases were accepted on a baseline case-folding platform.");
#endif

#if defined(_WIN32)
    const auto temporary_artifact_count = [&] {
        std::size_t count = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator{transaction_root})
        {
            if (entry.path().filename().native().find(L".tmp-") != std::wstring::npos)
            {
                ++count;
            }
        }
        return count;
    };
    const std::vector<dragonpixel::serialization::utf8_transaction_write> transient_journal_retry{
        {scene_target, "scene-after-transient-journal-retry\n"},
        {tile_target, "tile-after-transient-journal-retry\n"},
    };
    const auto transient_journal_result = dragonpixel::serialization::save_utf8_transaction(
        transient_journal_retry,
        transaction_root,
        dragonpixel::serialization::transaction_save_fault::committed_journal_transient_sharing_violation);
    require(transient_journal_result.succeeded
            && read_file(scene_target) == "scene-after-transient-journal-retry\n"
            && read_file(tile_target) == "tile-after-transient-journal-retry\n"
            && read_file(scene_backup) == "scene-after\n"
            && read_file(tile_backup) == "tile-after\n"
            && artifact_count() == 0
            && temporary_artifact_count() == 0,
        "A transient committed-journal sharing violation did not retry to a clean commit: "
            + transient_journal_result.error);

    const std::vector<dragonpixel::serialization::utf8_transaction_write> persistent_journal_retry{
        {scene_target, "scene-must-not-survive-journal-exhaustion\n"},
        {tile_target, "tile-must-not-survive-journal-exhaustion\n"},
    };
    const auto persistent_journal_result = dragonpixel::serialization::save_utf8_transaction(
        persistent_journal_retry,
        transaction_root,
        dragonpixel::serialization::transaction_save_fault::committed_journal_persistent_sharing_violation);
    constexpr std::string_view persistent_error_prefix =
        "The committed marker could not be flushed; pre-images restored: "
        "Could not publish the transaction journal: MoveFileExW failed with error 32 (";
    require(!persistent_journal_result.succeeded
            && persistent_journal_result.error.starts_with(persistent_error_prefix)
            && persistent_journal_result.error.find(") after 7 attempt(s); target=\"") != std::string::npos
            && persistent_journal_result.error.find("; staged=\"") != std::string::npos
            && persistent_journal_result.error.ends_with("; backup=<none>.")
            && persistent_journal_result.error.find("system message unavailable") == std::string::npos
            && read_file(scene_target) == "scene-after-transient-journal-retry\n"
            && read_file(tile_target) == "tile-after-transient-journal-retry\n"
            && read_file(scene_backup) == "scene-after-transient-journal-retry\n"
            && read_file(tile_backup) == "tile-after-transient-journal-retry\n"
            && artifact_count() == 0
            && temporary_artifact_count() == 0,
        "Persistent committed-journal retry exhaustion did not preserve the exact error and rollback state: "
            + persistent_journal_result.error);
#endif

    const std::u8string unicode_name = u8"unicode-\u573a\u666f-\u00f1.dpescene";
    const auto unicode_target = transaction_root / std::filesystem::path{unicode_name};
    require(dragonpixel::serialization::save_utf8_atomic(unicode_target, "unicode-before\n").succeeded,
        "The atomic writer could not create a non-ASCII target.");
    const std::vector<dragonpixel::serialization::utf8_transaction_write> unicode_transaction{
        {unicode_target, "unicode-after\n"},
    };
    const auto unicode_result =
        dragonpixel::serialization::save_utf8_transaction(unicode_transaction, transaction_root);
    require(unicode_result.succeeded && read_file(unicode_target) == "unicode-after\n",
        "The transaction writer could not replace a non-ASCII target: " + unicode_result.error);
    auto unicode_backup = unicode_target;
    unicode_backup += ".bak";
    require(read_file(unicode_backup) == "unicode-before\n",
        "The non-ASCII target did not retain its exact recovery backup.");
}
}

int main(int argc, char* argv[])
{
    try
    {
        require(argc == 2, "Expected a generated test directory argument.");
        const auto generated_root = std::filesystem::absolute(argv[1]);
        verify_scene_round_trip();
        verify_component_migration();
        verify_scene_v2_migration_and_v3_preservation();
        verify_history_validation_and_presets();
        verify_hierarchy_duplicate_and_delete();
        verify_atomic_transactions();
        verify_prefab_instance_command_history();
        verify_atomic_save(generated_root);
        verify_atomic_multi_document_save(generated_root);
        std::cout << "Native authoring core passed: validated command history, presets, hierarchy, duplication, "
                     "subtree deletion, deterministic scene v3, opaque preservation, migration, atomic single/multi-document save, and recovery.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
