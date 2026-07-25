#include <dragonpixel/scene/command_validation.h>

#include <dragonpixel/scene/scene.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dragonpixel::scene
{
namespace
{
using json = nlohmann::ordered_json;

struct component_key final
{
    core::uuid entity_id;
    std::string type_id;

    friend bool operator==(const component_key&, const component_key&) = default;
};

struct component_key_hash final
{
    std::size_t operator()(const component_key& value) const noexcept
    {
        const auto entity_hash = core::uuid_hash{}(value.entity_id);
        const auto type_hash = std::hash<std::string>{}(value.type_id);
        return entity_hash ^ (type_hash + 0x9e3779b9U + (entity_hash << 6U) + (entity_hash >> 2U));
    }
};

using projected_components = std::unordered_map<
    component_key,
    std::optional<component_record>,
    component_key_hash>;

void add_error(
    std::vector<core::diagnostic>& diagnostics,
    std::string code,
    std::string message,
    std::string context)
{
    diagnostics.push_back({
        core::diagnostic_severity::error,
        std::move(code),
        std::move(message),
        std::move(context),
    });
}

std::string command_context(
    std::size_t command_index,
    const core::uuid& entity_id,
    std::string_view type_id,
    std::string_view property_id = {})
{
    auto result = std::string{"command["} + std::to_string(command_index) + "] entity="
        + entity_id.to_string() + " component=" + std::string{type_id};
    if (!property_id.empty())
    {
        result += " property=" + std::string{property_id};
    }
    return result;
}

bool is_entity_available(
    const command_validation_context& context,
    const std::unordered_set<core::uuid, core::uuid_hash>& created_entities,
    const core::uuid& entity_id)
{
    if (created_entities.contains(entity_id))
    {
        return true;
    }
    if (context.entity_exists && context.entity_exists(entity_id))
    {
        return true;
    }
    return context.current_scene != nullptr && context.current_scene->find_entity(entity_id) != nullptr;
}

std::optional<component_record>& projected_component(
    projected_components& projected,
    const command_validation_context& context,
    const core::uuid& entity_id,
    std::string_view type_id)
{
    const component_key key{entity_id, std::string{type_id}};
    if (const auto found = projected.find(key); found != projected.end())
    {
        return found->second;
    }

    std::optional<component_record> initial;
    if (context.current_scene != nullptr)
    {
        if (const auto* entity = context.current_scene->find_entity(entity_id); entity != nullptr)
        {
            const auto component = std::find_if(
                entity->components.begin(),
                entity->components.end(),
                [&](const auto& value) { return value.type_id == type_id; });
            if (component != entity->components.end())
            {
                initial = *component;
            }
        }
    }
    return projected.emplace(std::move(key), std::move(initial)).first->second;
}

bool validate_editable_component(
    const metadata::component_descriptor* descriptor,
    const std::optional<component_record>& component,
    bool require_component,
    std::size_t command_index,
    const core::uuid& entity_id,
    std::string_view type_id,
    std::vector<core::diagnostic>& diagnostics)
{
    const auto context = command_context(command_index, entity_id, type_id);
    if (descriptor == nullptr)
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.COMPONENT_DESCRIPTOR_UNAVAILABLE",
            "The component descriptor is unavailable; unknown component records are opaque and read-only.",
            context);
        return false;
    }
    if (!component)
    {
        if (require_component)
        {
            add_error(
                diagnostics,
                "DPE.COMMAND.COMPONENT_MISSING",
                "The component mutation targets a component that is not present.",
                context);
            return false;
        }
        return true;
    }
    if (component->opaque)
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.OPAQUE_COMPONENT_READ_ONLY",
            "Opaque component records must round-trip unchanged and cannot be mutated.",
            context);
        return false;
    }
    if (component->schema_version != descriptor->schema_version)
    {
        add_error(
            diagnostics,
            component->schema_version > descriptor->schema_version
                ? "DPE.COMMAND.NEWER_COMPONENT_READ_ONLY"
                : "DPE.COMMAND.COMPONENT_SCHEMA_MISMATCH",
            "The component schema does not match the available descriptor and must be migrated before editing.",
            context);
        return false;
    }
    return true;
}

bool numeric_value(
    const json& value,
    const metadata::property_descriptor& property,
    std::string_view field,
    std::size_t command_index,
    const core::uuid& entity_id,
    std::string_view type_id,
    std::vector<core::diagnostic>& diagnostics)
{
    if (!value.is_number())
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.INVALID_PROPERTY_TYPE",
            "A numeric property or tuple field did not contain a JSON number.",
            command_context(command_index, entity_id, type_id, property.property_id) + std::string{field});
        return false;
    }
    const auto number = value.get<double>();
    if (!std::isfinite(number))
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.NON_FINITE_NUMBER",
            "Numeric component values must be finite.",
            command_context(command_index, entity_id, type_id, property.property_id) + std::string{field});
        return false;
    }
    if ((property.minimum && number < *property.minimum)
        || (property.maximum && number > *property.maximum))
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.PROPERTY_OUT_OF_RANGE",
            "The component value is outside its metadata range.",
            command_context(command_index, entity_id, type_id, property.property_id) + std::string{field});
        return false;
    }
    return true;
}

bool normalize_tuple(
    const json& value,
    const metadata::property_descriptor& property,
    std::span<const std::string_view> fields,
    std::size_t command_index,
    const core::uuid& entity_id,
    std::string_view type_id,
    json& normalized,
    std::vector<core::diagnostic>& diagnostics)
{
    if (!value.is_object() || value.size() != fields.size()
        || std::any_of(fields.begin(), fields.end(), [&](const auto field) {
            return !value.contains(std::string{field});
        }))
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.INVALID_PROPERTY_SHAPE",
            "The tuple property must contain exactly the metadata-defined numeric fields.",
            command_context(command_index, entity_id, type_id, property.property_id));
        return false;
    }

    normalized = json::object();
    bool valid = true;
    for (const auto field : fields)
    {
        const auto& child = value.at(std::string{field});
        valid = numeric_value(
                    child,
                    property,
                    std::string{"."} + std::string{field},
                    command_index,
                    entity_id,
                    type_id,
                    diagnostics)
            && valid;
        normalized[std::string{field}] = child;
    }
    return valid;
}

bool normalize_property_value(
    const json& value,
    const metadata::property_descriptor& property,
    std::size_t command_index,
    const core::uuid& entity_id,
    std::string_view type_id,
    const command_validation_context& validation_context,
    const std::unordered_set<core::uuid, core::uuid_hash>& created_entities,
    json& normalized,
    std::vector<core::diagnostic>& diagnostics)
{
    const auto context = command_context(command_index, entity_id, type_id, property.property_id);
    if (value.is_null())
    {
        if (!property.nullable)
        {
            add_error(
                diagnostics,
                "DPE.COMMAND.NULL_PROPERTY_NOT_ALLOWED",
                "The property is not nullable.",
                context);
            return false;
        }
        normalized = nullptr;
        return true;
    }

    using metadata::value_type;
    switch (property.type)
    {
    case value_type::boolean:
        if (!value.is_boolean())
        {
            add_error(diagnostics, "DPE.COMMAND.INVALID_PROPERTY_TYPE", "The property requires a JSON boolean.", context);
            return false;
        }
        normalized = value;
        return true;
    case value_type::integer:
        if (!value.is_number_integer() && !value.is_number_unsigned())
        {
            add_error(diagnostics, "DPE.COMMAND.INVALID_PROPERTY_TYPE", "The property requires a JSON integer.", context);
            return false;
        }
        if (!numeric_value(value, property, {}, command_index, entity_id, type_id, diagnostics))
        {
            return false;
        }
        normalized = value;
        return true;
    case value_type::number:
        if (!numeric_value(value, property, {}, command_index, entity_id, type_id, diagnostics))
        {
            return false;
        }
        normalized = value;
        return true;
    case value_type::vector2:
    {
        constexpr std::string_view fields[]{"x", "y"};
        return normalize_tuple(value, property, fields, command_index, entity_id, type_id, normalized, diagnostics);
    }
    case value_type::vector3:
    {
        constexpr std::string_view fields[]{"x", "y", "z"};
        return normalize_tuple(value, property, fields, command_index, entity_id, type_id, normalized, diagnostics);
    }
    case value_type::quaternion:
    {
        constexpr std::string_view fields[]{"w", "x", "y", "z"};
        return normalize_tuple(value, property, fields, command_index, entity_id, type_id, normalized, diagnostics);
    }
    case value_type::color:
    {
        constexpr std::string_view fields[]{"r", "g", "b", "a"};
        return normalize_tuple(value, property, fields, command_index, entity_id, type_id, normalized, diagnostics);
    }
    case value_type::string:
    case value_type::entity_reference:
    case value_type::asset_reference:
        if (!value.is_string())
        {
            add_error(diagnostics, "DPE.COMMAND.INVALID_PROPERTY_TYPE", "The property requires a JSON string.", context);
            return false;
        }
        break;
    }

    const auto& string_value = value.get_ref<const std::string&>();
    if (!property.enum_choices.empty()
        && std::find(property.enum_choices.begin(), property.enum_choices.end(), string_value)
            == property.enum_choices.end())
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.INVALID_ENUM_VALUE",
            "The property value is not one of its metadata enum choices.",
            context);
        return false;
    }
    if (property.type == value_type::entity_reference)
    {
        const auto referenced = core::uuid::parse(string_value);
        if (!referenced || !is_entity_available(validation_context, created_entities, *referenced))
        {
            add_error(
                diagnostics,
                "DPE.COMMAND.INVALID_ENTITY_REFERENCE",
                "The entity reference does not resolve in the validation context.",
                context);
            return false;
        }
    }
    else if (property.type == value_type::asset_reference)
    {
        if (string_value.empty() || !validation_context.asset_is_compatible
            || !validation_context.asset_is_compatible(string_value, property))
        {
            add_error(
                diagnostics,
                "DPE.COMMAND.INVALID_ASSET_REFERENCE",
                "The asset reference is missing or incompatible with its metadata filter.",
                context);
            return false;
        }
    }
    normalized = value;
    return true;
}

std::optional<component_record> normalize_upsert(
    const upsert_component_command& value,
    std::size_t command_index,
    const command_validation_context& context,
    const std::unordered_set<core::uuid, core::uuid_hash>& created_entities,
    const metadata::component_descriptor& descriptor,
    std::vector<core::diagnostic>& diagnostics)
{
    const auto diagnostic_count = diagnostics.size();
    const auto base_context = command_context(command_index, value.entity_id, value.component.type_id);
    if (value.component.opaque)
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.OPAQUE_COMPONENT_READ_ONLY",
            "Opaque component records must round-trip unchanged and cannot be upserted.",
            base_context);
        return std::nullopt;
    }
    if (value.component.schema_version != descriptor.schema_version)
    {
        add_error(
            diagnostics,
            value.component.schema_version > descriptor.schema_version
                ? "DPE.COMMAND.NEWER_COMPONENT_READ_ONLY"
                : "DPE.COMMAND.COMPONENT_SCHEMA_MISMATCH",
            "The upsert component schema does not match its descriptor.",
            base_context);
    }
    if (value.component.owner != descriptor.owner
        || value.component.qualified_name != descriptor.qualified_name)
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.COMPONENT_IDENTITY_MISMATCH",
            "The component owner or qualified name does not match its descriptor.",
            base_context);
    }
    if (!value.component.properties.is_object())
    {
        add_error(
            diagnostics,
            "DPE.COMMAND.INVALID_COMPONENT_PROPERTIES",
            "Known component properties must be a JSON object.",
            base_context);
        return std::nullopt;
    }

    for (const auto& [property_id, ignored] : value.component.properties.items())
    {
        static_cast<void>(ignored);
        if (std::none_of(descriptor.properties.begin(), descriptor.properties.end(), [&](const auto& property) {
                return property.property_id == property_id;
            }))
        {
            add_error(
                diagnostics,
                "DPE.COMMAND.UNKNOWN_PROPERTY",
                "The known component payload contains a property absent from its descriptor.",
                command_context(command_index, value.entity_id, value.component.type_id, property_id));
        }
    }

    auto normalized = value.component;
    normalized.properties = json::object();
    for (const auto& property : descriptor.properties)
    {
        json property_value;
        if (const auto found = value.component.properties.find(property.property_id);
            found != value.component.properties.end())
        {
            property_value = *found;
        }
        else if (!property.default_json.empty())
        {
            property_value = json::parse(property.default_json, nullptr, false);
            if (property_value.is_discarded())
            {
                add_error(
                    diagnostics,
                    "DPE.COMMAND.INVALID_METADATA_DEFAULT",
                    "The property metadata default is not valid JSON.",
                    command_context(command_index, value.entity_id, value.component.type_id, property.property_id));
                continue;
            }
        }
        else
        {
            add_error(
                diagnostics,
                "DPE.COMMAND.MISSING_REQUIRED_PROPERTY",
                "The upsert omitted a property that has no metadata default.",
                command_context(command_index, value.entity_id, value.component.type_id, property.property_id));
            continue;
        }

        json normalized_value;
        if (normalize_property_value(
                property_value,
                property,
                command_index,
                value.entity_id,
                value.component.type_id,
                context,
                created_entities,
                normalized_value,
                diagnostics))
        {
            normalized.properties[property.property_id] = std::move(normalized_value);
        }
    }
    if (diagnostics.size() != diagnostic_count)
    {
        return std::nullopt;
    }
    return normalized;
}
}

command_validation_result validate_and_normalize_commands(
    std::span<const command> commands,
    const command_validation_context& context)
{
    command_validation_result result;
    result.commands.reserve(commands.size());
    projected_components projected;
    std::unordered_set<core::uuid, core::uuid_hash> created_entities;
    for (const auto& value : commands)
    {
        std::visit([&](const auto& concrete) {
            using command_type = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<command_type, create_entity_command>
                || std::is_same_v<command_type, create_preset_command>)
            {
                created_entities.insert(concrete.entity_id);
            }
        }, value);
    }

    for (std::size_t command_index = 0; command_index < commands.size(); ++command_index)
    {
        const auto& value = commands[command_index];
        std::visit([&](const auto& concrete) {
            using command_type = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<command_type, upsert_component_command>)
            {
                const auto* descriptor = context.descriptors.find(concrete.component.type_id);
                auto& existing = projected_component(
                    projected,
                    context,
                    concrete.entity_id,
                    concrete.component.type_id);
                if (!validate_editable_component(
                        descriptor,
                        existing,
                        false,
                        command_index,
                        concrete.entity_id,
                        concrete.component.type_id,
                        result.diagnostics))
                {
                    return;
                }
                auto normalized = normalize_upsert(
                    concrete,
                    command_index,
                    context,
                    created_entities,
                    *descriptor,
                    result.diagnostics);
                if (normalized)
                {
                    existing = *normalized;
                    result.commands.emplace_back(upsert_component_command{
                        concrete.entity_id,
                        std::move(*normalized),
                    });
                }
            }
            else if constexpr (std::is_same_v<command_type, set_component_property_command>)
            {
                const auto* descriptor = context.descriptors.find(concrete.type_id);
                auto& component = projected_component(projected, context, concrete.entity_id, concrete.type_id);
                if (!validate_editable_component(
                        descriptor,
                        component,
                        true,
                        command_index,
                        concrete.entity_id,
                        concrete.type_id,
                        result.diagnostics))
                {
                    return;
                }
                const auto property = std::find_if(
                    descriptor->properties.begin(),
                    descriptor->properties.end(),
                    [&](const auto& candidate) { return candidate.property_id == concrete.property_id; });
                if (property == descriptor->properties.end())
                {
                    add_error(
                        result.diagnostics,
                        "DPE.COMMAND.UNKNOWN_PROPERTY",
                        "The property is absent from the component descriptor.",
                        command_context(command_index, concrete.entity_id, concrete.type_id, concrete.property_id));
                    return;
                }
                if (property->read_only)
                {
                    add_error(
                        result.diagnostics,
                        "DPE.COMMAND.READ_ONLY_PROPERTY",
                        "The property metadata marks this value as read-only.",
                        command_context(command_index, concrete.entity_id, concrete.type_id, concrete.property_id));
                    return;
                }
                json normalized;
                if (!normalize_property_value(
                        concrete.value,
                        *property,
                        command_index,
                        concrete.entity_id,
                        concrete.type_id,
                        context,
                        created_entities,
                        normalized,
                        result.diagnostics))
                {
                    return;
                }
                component->properties[concrete.property_id] = normalized;
                result.commands.emplace_back(set_component_property_command{
                    concrete.entity_id,
                    concrete.type_id,
                    concrete.property_id,
                    std::move(normalized),
                });
            }
            else if constexpr (std::is_same_v<command_type, remove_component_command>
                || std::is_same_v<command_type, reorder_component_command>
                || std::is_same_v<command_type, set_component_enabled_command>)
            {
                const auto* descriptor = context.descriptors.find(concrete.type_id);
                auto& component = projected_component(projected, context, concrete.entity_id, concrete.type_id);
                if (!validate_editable_component(
                        descriptor,
                        component,
                        true,
                        command_index,
                        concrete.entity_id,
                        concrete.type_id,
                        result.diagnostics))
                {
                    return;
                }
                result.commands.push_back(value);
                if constexpr (std::is_same_v<command_type, remove_component_command>)
                {
                    component.reset();
                }
                else if constexpr (std::is_same_v<command_type, set_component_enabled_command>)
                {
                    component->enabled = concrete.enabled;
                }
            }
            else
            {
                result.commands.push_back(value);
            }
        }, value);
    }

    result.succeeded = result.diagnostics.empty();
    if (!result.succeeded)
    {
        result.commands.clear();
    }
    return result;
}
}
