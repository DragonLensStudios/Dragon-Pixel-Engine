#include <dragonpixel/serialization/scene_json.h>

#include <dragonpixel/metadata/builtin_ids.h>

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>

namespace dragonpixel::serialization
{
namespace
{
constexpr auto scene_schema = "https://dragonpixel.dev/schemas/v2/scene.schema.json";
constexpr auto producer_version = "0.1.0-slice1";

std::string owner_name(metadata::runtime_owner owner)
{
    return owner == metadata::runtime_owner::managed ? "managed" : "native";
}

metadata::runtime_owner parse_owner(const nlohmann::ordered_json& record)
{
    return record.value("owner", "native") == "managed"
        ? metadata::runtime_owner::managed
        : metadata::runtime_owner::native;
}

core::diagnostic load_error(std::string code, std::string message, std::string context = {})
{
    return {core::diagnostic_severity::error, std::move(code), std::move(message), std::move(context)};
}

scene::component_record read_component(
    const nlohmann::ordered_json& raw,
    const metadata::registry& registry,
    std::vector<migration_record>& migrations)
{
    scene::component_record result;
    result.raw_record = raw;
    result.type_id = raw.at("typeId").get<std::string>();
    result.qualified_name = raw.value("qualifiedName", result.type_id);
    result.schema_version = raw.at("schemaVersion").get<std::uint32_t>();
    result.owner = parse_owner(raw);
    result.enabled = raw.value("enabled", true);
    result.properties = raw.at("properties");
    const auto* descriptor = registry.find(result.type_id);
    if (descriptor == nullptr || result.schema_version > descriptor->schema_version)
    {
        result.opaque = true;
        return result;
    }

    result.qualified_name = descriptor->qualified_name;

    if (result.type_id == metadata::builtin_component_ids::transform && result.schema_version == 1
        && descriptor->schema_version == 2)
    {
        constexpr auto old_id = "dpe.transform.translation";
        constexpr auto new_id = "dpe.transform.position";
        if (result.properties.contains(old_id) && !result.properties.contains(new_id))
        {
            result.properties[new_id] = result.properties[old_id];
            result.properties.erase(old_id);
        }
        result.schema_version = 2;
        migrations.push_back({result.type_id, 1, 2, "Renamed translation property identity to position."});
    }
    else if (result.schema_version < descriptor->schema_version)
    {
        result.opaque = true;
    }
    return result;
}
}

nlohmann::ordered_json canonicalize_json(const nlohmann::ordered_json& value)
{
    if (value.is_object())
    {
        std::vector<std::string> keys;
        keys.reserve(value.size());
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
        {
            keys.push_back(iterator.key());
        }
        std::sort(keys.begin(), keys.end());
        auto result = nlohmann::ordered_json::object();
        for (const auto& key : keys)
        {
            result[key] = canonicalize_json(value.at(key));
        }
        return result;
    }
    if (value.is_array())
    {
        auto result = nlohmann::ordered_json::array();
        for (const auto& item : value)
        {
            result.push_back(canonicalize_json(item));
        }
        return result;
    }
    return value;
}

std::string write_scene_json(const scene::scene& value)
{
    auto root = nlohmann::ordered_json::object();
    root["$schema"] = scene_schema;
    root["format"] = "dpe.scene";
    root["formatVersion"] = 2;
    root["engineVersion"] = producer_version;
    root["sceneId"] = value.id().to_string();
    root["name"] = value.name();
    root["entities"] = nlohmann::ordered_json::array();

    for (const auto& entity : value.entities())
    {
        auto entity_json = nlohmann::ordered_json::object();
        entity_json["id"] = entity.id.to_string();
        entity_json["name"] = entity.name;
        entity_json["enabled"] = entity.enabled;
        if (entity.parent_id)
        {
            entity_json["parentId"] = entity.parent_id->to_string();
        }
        else
        {
            entity_json["parentId"] = nullptr;
        }
        entity_json["components"] = nlohmann::ordered_json::array();
        for (const auto& component : entity.components)
        {
            auto component_json = component.raw_record.is_object()
                ? component.raw_record
                : nlohmann::ordered_json::object();
            if (!component.opaque)
            {
                component_json["typeId"] = component.type_id;
                component_json["qualifiedName"] = component.qualified_name;
                component_json["schemaVersion"] = component.schema_version;
                component_json["owner"] = owner_name(component.owner);
                component_json["enabled"] = component.enabled;
                component_json["properties"] = component.properties;
            }
            else
            {
                if (!component_json.contains("qualifiedName"))
                {
                    component_json["qualifiedName"] = component.qualified_name;
                }
                if (!component_json.contains("enabled"))
                {
                    component_json["enabled"] = component.enabled;
                }
            }
            entity_json["components"].push_back(canonicalize_json(component_json));
        }
        root["entities"].push_back(canonicalize_json(entity_json));
    }

    return canonicalize_json(root).dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::strict) + "\n";
}

scene_load_result read_scene_json(std::string_view json, const metadata::registry& registry)
{
    scene_load_result result;
    try
    {
        const auto root = nlohmann::ordered_json::parse(json.begin(), json.end());
        const auto format_version = root.value("formatVersion", 0);
        if (!root.is_object() || root.value("format", "") != "dpe.scene"
            || (format_version != 1 && format_version != 2))
        {
            result.diagnostics.push_back(load_error(
                "DPE.SERIALIZATION.UNSUPPORTED_FORMAT",
                "Document is not a supported dpe.scene version 1 or 2 document."));
            return result;
        }
        if (format_version == 2
            && (root.value("$schema", "") != scene_schema
                || root.value("engineVersion", "").empty()))
        {
            result.diagnostics.push_back(load_error(
                "DPE.SERIALIZATION.INVALID_ENVELOPE",
                "Scene version 2 requires the canonical schema URI and a producer engineVersion."));
            return result;
        }
        if (format_version == 1)
        {
            result.migrations.push_back({
                "dpe.document.scene",
                1,
                2,
                "Added explicit entity/component enabled state and diagnostic component qualified names.",
            });
        }
        const auto scene_id = core::uuid::parse(root.at("sceneId").get<std::string>());
        if (!scene_id)
        {
            result.diagnostics.push_back(load_error("DPE.SERIALIZATION.INVALID_SCENE_ID", "Scene UUID was invalid."));
            return result;
        }

        std::vector<scene::entity> entities;
        for (const auto& entity_json : root.at("entities"))
        {
            const auto entity_id = core::uuid::parse(entity_json.at("id").get<std::string>());
            if (!entity_id)
            {
                result.diagnostics.push_back(load_error("DPE.SERIALIZATION.INVALID_ENTITY_ID", "Entity UUID was invalid."));
                return result;
            }
            std::optional<core::uuid> parent_id;
            if (!entity_json.at("parentId").is_null())
            {
                parent_id = core::uuid::parse(entity_json.at("parentId").get<std::string>());
                if (!parent_id)
                {
                    result.diagnostics.push_back(load_error(
                        "DPE.SERIALIZATION.INVALID_PARENT_ID",
                        "Parent UUID was invalid.",
                        entity_id->to_string()));
                    return result;
                }
            }
            std::vector<scene::component_record> components;
            for (const auto& component_json : entity_json.at("components"))
            {
                components.push_back(read_component(component_json, registry, result.migrations));
            }
            entities.push_back({
                *entity_id,
                entity_json.at("name").get<std::string>(),
                parent_id,
                std::move(components),
                entity_json.value("enabled", true),
            });
        }

        scene::scene loaded{*scene_id, root.value("name", "Untitled Scene"), std::move(entities)};
        if (const auto validation = loaded.validate())
        {
            result.diagnostics.push_back(*validation);
            return result;
        }
        result.value.emplace(std::move(loaded));
    }
    catch (const std::exception& exception)
    {
        result.diagnostics.push_back(load_error("DPE.SERIALIZATION.INVALID_JSON", exception.what()));
    }
    return result;
}
}
