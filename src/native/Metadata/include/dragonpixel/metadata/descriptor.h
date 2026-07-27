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
    data_only,
};

enum class implementation_language
{
    cpp,
    csharp,
    data_only,
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
    component_reference,
    object,
    list,
    dictionary,
    polymorphic_object,
};

struct value_shape final
{
    value_type type{value_type::string};
    bool nullable{};
    std::string object_type_id;
    std::string contract_id;
    std::string reference_filter;
    std::vector<value_shape> arguments;
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
    std::optional<value_shape> shape;
};

struct component_descriptor final
{
    std::string type_id;
    std::string qualified_name;
    std::string display_name;
    std::uint32_t schema_version{1};
    runtime_owner owner{runtime_owner::native};
    std::vector<property_descriptor> properties;
    std::string category;
    std::string tooltip;
    bool addable{true};
    bool removable{true};
    bool resettable{true};
    implementation_language language{implementation_language::cpp};
    std::string source_path;
    std::string runtime_module_id;
};

struct contract_descriptor final
{
    std::string contract_id;
    std::string qualified_name;
    std::string display_name;
    std::string tooltip;
};

struct object_type_descriptor final
{
    std::string type_id;
    std::string qualified_name;
    std::string display_name;
    std::uint32_t schema_version{1};
    std::vector<std::string> contracts;
    std::vector<property_descriptor> properties;
};
}
