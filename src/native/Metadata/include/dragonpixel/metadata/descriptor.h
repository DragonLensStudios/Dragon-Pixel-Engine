#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dragonpixel::metadata
{
enum class runtime_owner
{
    native,
    managed,
};

enum class value_type
{
    boolean,
    integer,
    number,
    string,
    vector2,
    vector3,
    quaternion,
    color,
    entity_reference,
    asset_reference,
};

struct property_descriptor final
{
    std::string property_id;
    std::string display_name;
    value_type type{value_type::string};
    std::uint32_t order{};
    bool read_only{};
    std::string default_json;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    std::string units;
    std::vector<std::string> enum_choices;
    bool nullable{};
    std::string reference_filter;
    std::string category;
    std::string tooltip;
    std::string drawer_key;
};

struct component_descriptor final
{
    std::string type_id;
    std::string qualified_name;
    std::string display_name;
    std::uint32_t schema_version{1};
    runtime_owner owner{runtime_owner::native};
    std::vector<property_descriptor> properties;
};
}
