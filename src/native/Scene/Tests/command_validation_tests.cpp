#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/command_validation.h>
#include <dragonpixel/scene/scene.h>

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using dragonpixel::core::uuid;
using dragonpixel::metadata::component_descriptor;
using dragonpixel::metadata::property_descriptor;
using dragonpixel::metadata::runtime_owner;
using dragonpixel::metadata::value_type;
using dragonpixel::scene::command;
using dragonpixel::scene::component_record;
using json = nlohmann::ordered_json;

constexpr std::string_view known_type = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr std::string_view unknown_type = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
constexpr std::string_view required_type = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
constexpr std::string_view scene_id_text = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view entity_a_text = "20000000-0000-4000-8000-000000000001";
constexpr std::string_view entity_b_text = "20000000-0000-4000-8000-000000000002";
constexpr std::string_view entity_c_text = "20000000-0000-4000-8000-000000000003";
constexpr std::string_view entity_d_text = "20000000-0000-4000-8000-000000000004";
constexpr std::string_view entity_e_text = "20000000-0000-4000-8000-000000000005";

uuid id(std::string_view value)
{
    const auto parsed = uuid::parse(value);
    if (!parsed)
    {
        throw std::runtime_error{"Invalid test UUID"};
    }
    return *parsed;
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error{std::string{message}};
    }
}

property_descriptor property(
    std::string property_id,
    value_type type,
    std::uint32_t order,
    std::string default_json)
{
    property_descriptor result{};
    result.property_id = std::move(property_id);
    result.display_name = "Test property";
    result.type = type;
    result.order = order;
    result.default_json = std::move(default_json);
    return result;
}

dragonpixel::metadata::registry make_registry()
{
    auto boolean = property("p.bool", value_type::boolean, 0, "true");
    auto integer = property("p.int", value_type::integer, 1, "1");
    integer.minimum = 0.0;
    integer.maximum = 10.0;
    auto number = property("p.number", value_type::number, 2, "0.25");
    number.minimum = -1.0;
    number.maximum = 1.0;
    auto enumeration = property("p.enum", value_type::string, 3, R"("red")");
    enumeration.enum_choices = {"red", "green"};
    auto vector2 = property("p.vector2", value_type::vector2, 4, R"({"x":1.0,"y":2.0})");
    auto vector3 = property("p.vector3", value_type::vector3, 5, R"({"x":1.0,"y":2.0,"z":3.0})");
    auto quaternion = property("p.quaternion", value_type::quaternion, 6, R"({"w":1.0,"x":0.0,"y":0.0,"z":0.0})");
    auto color = property("p.color", value_type::color, 7, R"({"r":1.0,"g":0.5,"b":0.25,"a":1.0})");
    color.minimum = 0.0;
    color.maximum = 1.0;
    auto entity_reference = property(
        "p.entity",
        value_type::entity_reference,
        8,
        std::string{"\""} + std::string{entity_a_text} + "\"");
    auto asset_reference = property("p.asset", value_type::asset_reference, 9, R"("asset-ok")");
    asset_reference.reference_filter = "sprite";
    auto nullable = property("p.optional", value_type::string, 10, "null");
    nullable.nullable = true;

    dragonpixel::metadata::registry registry;
    require(registry.add(component_descriptor{
                std::string{known_type},
                "Tests.KnownComponent",
                "Known Component",
                1,
                runtime_owner::native,
                {
                    std::move(boolean),
                    std::move(integer),
                    std::move(number),
                    std::move(enumeration),
                    std::move(vector2),
                    std::move(vector3),
                    std::move(quaternion),
                    std::move(color),
                    std::move(entity_reference),
                    std::move(asset_reference),
                    std::move(nullable),
                },
            }),
        "Could not register known component descriptor");
    require(registry.add(component_descriptor{
                std::string{required_type},
                "Tests.RequiredComponent",
                "Required Component",
                1,
                runtime_owner::native,
                {property("p.required", value_type::string, 0, {})},
            }),
        "Could not register required component descriptor");
    return registry;
}

json valid_properties()
{
    return {
        {"p.bool", true},
        {"p.int", 2},
        {"p.number", 0.5},
        {"p.enum", "green"},
        {"p.vector2", {{"x", 1.0}, {"y", 2.0}}},
        {"p.vector3", {{"x", 1.0}, {"y", 2.0}, {"z", 3.0}}},
        {"p.quaternion", {{"w", 1.0}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}},
        {"p.color", {{"r", 1.0}, {"g", 0.5}, {"b", 0.25}, {"a", 1.0}}},
        {"p.entity", std::string{entity_a_text}},
        {"p.asset", "asset-ok"},
        {"p.optional", nullptr},
    };
}

component_record known_component(std::uint32_t schema_version = 1)
{
    return {
        std::string{known_type},
        schema_version,
        runtime_owner::native,
        valid_properties(),
        false,
        json::object(),
        true,
        "Tests.KnownComponent",
    };
}

dragonpixel::scene::scene make_scene()
{
    auto opaque = component_record{
        std::string{unknown_type},
        9,
        runtime_owner::managed,
        json::object(),
        true,
        {{"typeId", std::string{unknown_type}}, {"vendor", {{"future", true}}}},
        true,
        "Vendor.FutureComponent",
    };
    return {
        id(scene_id_text),
        "Command validation",
        {
            {id(entity_a_text), "A", std::nullopt, {known_component()}, true, 0},
            {id(entity_b_text), "B", std::nullopt, {}, true, 1},
            {id(entity_c_text), "C", std::nullopt, {known_component(2)}, true, 2},
            {id(entity_d_text), "D", std::nullopt, {std::move(opaque)}, true, 3},
        },
    };
}

dragonpixel::scene::command_validation_context context_for(
    const dragonpixel::metadata::registry& registry,
    const dragonpixel::scene::scene& current_scene)
{
    dragonpixel::scene::command_validation_context context{registry};
    context.current_scene = &current_scene;
    context.asset_is_compatible = [](std::string_view reference, const property_descriptor& descriptor) {
        return reference == "asset-ok" && descriptor.reference_filter == "sprite";
    };
    return context;
}

bool has_code(
    const dragonpixel::scene::command_validation_result& result,
    std::string_view code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

void valid_transaction_fills_defaults_and_preserves_order()
{
    const auto registry = make_registry();
    const auto current_scene = make_scene();
    const auto target = id(entity_e_text);
    auto new_component = known_component();
    new_component.properties = json::object();
    const std::vector<command> commands{
        dragonpixel::scene::create_entity_command{target, "E", std::nullopt, std::nullopt},
        dragonpixel::scene::upsert_component_command{target, std::move(new_component)},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.int", 7},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.asset", "asset-ok"},
    };
    const auto result = dragonpixel::scene::validate_and_normalize_commands(
        commands,
        context_for(registry, current_scene));
    require(result.succeeded && result.diagnostics.empty() && result.commands.size() == commands.size(),
        "Valid multi-command transaction was rejected");
    const auto& upsert = std::get<dragonpixel::scene::upsert_component_command>(result.commands.at(1));
    require(upsert.component.properties.size() == 11
            && upsert.component.properties.at("p.bool") == true
            && upsert.component.properties.at("p.optional").is_null(),
        "Upsert metadata defaults were not filled and normalized");
    require(std::get<dragonpixel::scene::set_component_property_command>(result.commands.at(2)).value == 7,
        "Normalized command ordering changed");
}

void invalid_types_ranges_enums_shapes_and_properties_are_rejected()
{
    const auto registry = make_registry();
    const auto current_scene = make_scene();
    const auto target = id(entity_a_text);
    const std::vector<command> commands{
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.bool", "true"},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.int", 11},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.enum", "blue"},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.vector2", {{"x", 1.0}}},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.number", std::numeric_limits<double>::infinity()},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.unknown", 1},
    };
    const auto result = dragonpixel::scene::validate_and_normalize_commands(
        commands,
        context_for(registry, current_scene));
    require(!result.succeeded && result.commands.empty(), "Invalid transaction returned applicable commands");
    require(has_code(result, "DPE.COMMAND.INVALID_PROPERTY_TYPE")
            && has_code(result, "DPE.COMMAND.PROPERTY_OUT_OF_RANGE")
            && has_code(result, "DPE.COMMAND.INVALID_ENUM_VALUE")
            && has_code(result, "DPE.COMMAND.INVALID_PROPERTY_SHAPE")
            && has_code(result, "DPE.COMMAND.NON_FINITE_NUMBER")
            && has_code(result, "DPE.COMMAND.UNKNOWN_PROPERTY"),
        "Invalid property diagnostics were incomplete");
}

void invalid_references_and_null_are_rejected()
{
    const auto registry = make_registry();
    const auto current_scene = make_scene();
    const auto target = id(entity_a_text);
    const std::vector<command> commands{
        dragonpixel::scene::set_component_property_command{
            target,
            std::string{known_type},
            "p.entity",
            "ffffffff-ffff-4fff-8fff-ffffffffffff"},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.asset", "asset-wrong"},
        dragonpixel::scene::set_component_property_command{target, std::string{known_type}, "p.enum", nullptr},
    };
    const auto result = dragonpixel::scene::validate_and_normalize_commands(
        commands,
        context_for(registry, current_scene));
    require(!result.succeeded && result.commands.empty(), "Invalid references returned applicable commands");
    require(has_code(result, "DPE.COMMAND.INVALID_ENTITY_REFERENCE")
            && has_code(result, "DPE.COMMAND.INVALID_ASSET_REFERENCE")
            && has_code(result, "DPE.COMMAND.NULL_PROPERTY_NOT_ALLOWED"),
        "Reference/null diagnostics were incomplete");
}

void opaque_newer_unavailable_and_missing_defaults_are_rejected()
{
    const auto registry = make_registry();
    const auto current_scene = make_scene();
    auto required = component_record{
        std::string{required_type},
        1,
        runtime_owner::native,
        json::object(),
        false,
        json::object(),
        true,
        "Tests.RequiredComponent",
    };
    const std::vector<command> commands{
        dragonpixel::scene::set_component_enabled_command{id(entity_d_text), std::string{unknown_type}, false},
        dragonpixel::scene::set_component_property_command{id(entity_c_text), std::string{known_type}, "p.bool", false},
        dragonpixel::scene::upsert_component_command{id(entity_c_text), known_component()},
        dragonpixel::scene::upsert_component_command{id(entity_b_text), std::move(required)},
    };
    const auto result = dragonpixel::scene::validate_and_normalize_commands(
        commands,
        context_for(registry, current_scene));
    require(!result.succeeded && result.commands.empty(), "Read-only/incomplete mutations returned applicable commands");
    require(has_code(result, "DPE.COMMAND.COMPONENT_DESCRIPTOR_UNAVAILABLE")
            && has_code(result, "DPE.COMMAND.NEWER_COMPONENT_READ_ONLY")
            && has_code(result, "DPE.COMMAND.MISSING_REQUIRED_PROPERTY"),
        "Opaque/newer/default diagnostics were incomplete");
}
}

int main()
{
    try
    {
        valid_transaction_fills_defaults_and_preserves_order();
        invalid_types_ranges_enums_shapes_and_properties_are_rejected();
        invalid_references_and_null_are_rejected();
        opaque_newer_unavailable_and_missing_defaults_are_rejected();
        std::cout << "Scene command metadata validation tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
