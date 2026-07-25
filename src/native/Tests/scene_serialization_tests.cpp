#include <dragonpixel/core/uuid.h>
#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/commands.h>
#include <dragonpixel/scene/scene.h>
#include <dragonpixel/serialization/atomic_file.h>
#include <dragonpixel/serialization/scene_json.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using dragonpixel::core::uuid;
using dragonpixel::metadata::runtime_owner;
using dragonpixel::scene::command;
using dragonpixel::scene::component_record;
using dragonpixel::scene::create_entity_command;
using dragonpixel::scene::reparent_entity_command;
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

void verify_scene_round_trip()
{
    const auto registry = dragonpixel::metadata::registry::slice_one_defaults();
    require(registry.size() == 7, "Default metadata registration failed.");
    require(registry.find(dragonpixel::metadata::builtin_component_ids::transform)->schema_version == 2,
        "Transform schema version was wrong.");

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
    require(parsed.at("$schema") == "https://dragonpixel.dev/schemas/v2/scene.schema.json"
            && parsed.at("engineVersion") == "0.1.0-slice1"
            && parsed.at("formatVersion") == 2 && parsed.at("entities").at(1).at("enabled") == true,
        "Scene v2 enabled-state envelope was not serialized.");
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
        "Scene v2 without its canonical schema URI was accepted.");
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
    require(loaded.value.has_value() && loaded.migrations.size() == 2,
        "Scene/document and Transform migrations did not each run once.");
    const auto migrated = nlohmann::ordered_json::parse(dragonpixel::serialization::write_scene_json(*loaded.value));
    const auto& component = migrated.at("entities").at(0).at("components").at(0);
    require(component.at("schemaVersion") == 2, "Migrated component version was not advanced.");
    require(component.at("properties").contains("dpe.transform.position"), "Migrated property ID was missing.");
    require(!component.at("properties").contains("dpe.transform.translation"), "Legacy property ID survived migration.");
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
}

int main(int argc, char* argv[])
{
    try
    {
        require(argc == 2, "Expected a generated test directory argument.");
        const auto generated_root = std::filesystem::absolute(argv[1]);
        verify_scene_round_trip();
        verify_component_migration();
        verify_atomic_transactions();
        verify_atomic_save(generated_root);
        std::cout << "S1 native core passed: commands, hierarchy, metadata, deterministic round trip, "
                     "opaque preservation, migration, atomic save, and recovery.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
