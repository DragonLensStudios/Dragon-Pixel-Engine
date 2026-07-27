#pragma once

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/scene/component_record.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dragonpixel::scene
{
struct create_entity_command final
{
    core::uuid entity_id;
    std::string name;
    std::optional<core::uuid> parent_id{};
    std::optional<std::size_t> sibling_index{};
};

enum class entity_preset
{
    empty,
    sprite,
    cube,
    camera,
    light,
};

struct create_preset_command final
{
    core::uuid entity_id;
    std::string name;
    entity_preset preset{entity_preset::empty};
    std::optional<core::uuid> parent_id{};
    std::optional<std::size_t> sibling_index{};
    std::optional<std::string> primary_asset{};
    std::optional<std::string> material_asset{};
};

struct rename_entity_command final
{
    core::uuid entity_id;
    std::string name;
};

struct reparent_entity_command final
{
    core::uuid entity_id;
    std::optional<core::uuid> parent_id{};
    std::optional<std::size_t> sibling_index{};
};

struct reorder_entity_command final
{
    core::uuid entity_id;
    std::size_t sibling_index{};
};

struct delete_subtree_command final
{
    core::uuid root_entity_id;
};

struct entity_id_remap final
{
    core::uuid source_id;
    core::uuid duplicate_id;
};

struct duplicate_subtree_command final
{
    core::uuid root_entity_id;
    std::vector<entity_id_remap> id_remaps;
    std::optional<core::uuid> destination_parent_id{};
    std::optional<std::size_t> sibling_index{};
    std::optional<std::string> duplicate_root_name{};
    bool use_source_parent{true};
};

struct set_entity_enabled_command final
{
    core::uuid entity_id;
    bool enabled{true};
};

struct upsert_component_command final
{
    core::uuid entity_id;
    component_record component;
};

struct remove_component_command final
{
    core::uuid entity_id;
    std::string type_id;
};

struct reorder_component_command final
{
    core::uuid entity_id;
    std::string type_id;
    std::size_t destination_index{};
};

struct set_component_enabled_command final
{
    core::uuid entity_id;
    std::string type_id;
    bool enabled{true};
};

struct set_component_property_command final
{
    core::uuid entity_id;
    std::string type_id;
    std::string property_id;
    nlohmann::ordered_json value;
};

// Addresses a value below a component's stable root property. Object members
// use their stable property ID, polymorphic envelopes use "properties" followed
// by the stable property ID, dictionary members use their key, and list members
// use a zero-based decimal index. Validation always evaluates the rebuilt root
// value before any authoring state is changed.
struct set_component_property_path_command final
{
    core::uuid entity_id;
    std::string type_id;
    std::string property_id;
    std::vector<std::string> path;
    nlohmann::ordered_json value;
};

struct set_prefab_instances_command final
{
    nlohmann::ordered_json instances = nlohmann::ordered_json::array();
};

using command = std::variant<
    create_entity_command,
    create_preset_command,
    rename_entity_command,
    reparent_entity_command,
    reorder_entity_command,
    delete_subtree_command,
    duplicate_subtree_command,
    set_entity_enabled_command,
    upsert_component_command,
    remove_component_command,
    reorder_component_command,
    set_component_enabled_command,
    set_component_property_command,
    set_component_property_path_command,
    set_prefab_instances_command>;
}
