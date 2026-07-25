#pragma once

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/scene/component_record.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <variant>

namespace dragonpixel::scene
{
struct create_entity_command final
{
    core::uuid entity_id;
    std::string name;
    std::optional<core::uuid> parent_id;
};

struct rename_entity_command final
{
    core::uuid entity_id;
    std::string name;
};

struct reparent_entity_command final
{
    core::uuid entity_id;
    std::optional<core::uuid> parent_id;
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

using command = std::variant<
    create_entity_command,
    rename_entity_command,
    reparent_entity_command,
    set_entity_enabled_command,
    upsert_component_command,
    remove_component_command,
    set_component_enabled_command,
    set_component_property_command>;
}
