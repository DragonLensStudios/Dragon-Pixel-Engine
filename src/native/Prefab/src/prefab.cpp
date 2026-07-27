#include <dragonpixel/prefab/prefab.h>

#include <dragonpixel/serialization/scene_json.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dragonpixel::prefab
{
namespace
{
using json = nlohmann::ordered_json;

core::diagnostic diagnostic(
    core::diagnostic_severity severity,
    std::string code,
    std::string message,
    std::string context = {})
{
    return {severity, std::move(code), std::move(message), std::move(context)};
}

std::string owner_name(metadata::runtime_owner owner)
{
    return owner == metadata::runtime_owner::managed ? "managed" : "native";
}

metadata::runtime_owner parse_owner(std::string_view owner)
{
    return owner == "managed" ? metadata::runtime_owner::managed : metadata::runtime_owner::native;
}

json component_json(const scene::component_record& component)
{
    if (component.opaque)
    {
        return serialization::canonicalize_json(component.raw_record);
    }
    return {
        {"enabled", component.enabled},
        {"owner", owner_name(component.owner)},
        {"properties", serialization::canonicalize_json(component.properties)},
        {"qualifiedName", component.qualified_name},
        {"schemaVersion", component.schema_version},
        {"typeId", component.type_id},
    };
}

json entity_json(const scene::entity& entity)
{
    json components = json::array();
    for (const auto& component : entity.components)
    {
        components.push_back(component_json(component));
    }
    return {
        {"components", std::move(components)},
        {"enabled", entity.enabled},
        {"id", entity.id.to_string()},
        {"name", entity.name},
        {"parentId", entity.parent_id ? json(entity.parent_id->to_string()) : json(nullptr)},
        {"siblingOrder", entity.sibling_order},
    };
}

std::optional<scene::component_record> parse_component(const json& value)
{
    if (!value.is_object() || !value.contains("typeId") || !value.contains("schemaVersion"))
    {
        return std::nullopt;
    }
    const auto owner_text = value.value("owner", "unknown");
    const auto opaque = owner_text != "native" && owner_text != "managed";
    scene::component_record component;
    component.type_id = value.value("typeId", std::string{});
    component.schema_version = value.value("schemaVersion", 0U);
    component.qualified_name = value.value("qualifiedName", std::string{});
    component.enabled = value.value("enabled", true);
    component.opaque = opaque;
    component.owner = parse_owner(owner_text);
    component.properties = value.value("properties", json::object());
    if (opaque)
    {
        component.raw_record = value;
    }
    return component;
}

std::optional<scene::entity> parse_entity(const json& value)
{
    if (!value.is_object())
    {
        return std::nullopt;
    }
    const auto id = core::uuid::parse(value.value("id", std::string{}));
    const auto name = value.value("name", std::string{});
    if (!id || name.empty())
    {
        return std::nullopt;
    }
    std::optional<core::uuid> parent;
    if (value.contains("parentId") && !value["parentId"].is_null())
    {
        parent = core::uuid::parse(value["parentId"].get<std::string>());
        if (!parent)
        {
            return std::nullopt;
        }
    }
    scene::entity result{*id, name, parent, {}};
    result.enabled = value.value("enabled", true);
    result.sibling_order = value.value("siblingOrder", 0U);
    if (!value.contains("components") || !value["components"].is_array())
    {
        return std::nullopt;
    }
    for (const auto& component_value : value["components"])
    {
        auto component = parse_component(component_value);
        if (!component)
        {
            return std::nullopt;
        }
        result.components.push_back(std::move(*component));
    }
    return result;
}

json mapping_json(const entity_mapping& mapping)
{
    json path = json::array();
    for (const auto& id : mapping.nested_path)
    {
        path.push_back(id.to_string());
    }
    return {
        {"instanceEntityId", mapping.instance_entity_id.to_string()},
        {"nestedPath", std::move(path)},
        {"sourceEntityId", mapping.source_entity_id.to_string()},
    };
}

json instance_json(const instance_record& instance)
{
    json mappings = json::array();
    for (const auto& mapping : instance.entity_mappings)
    {
        mappings.push_back(mapping_json(mapping));
    }
    json overrides = json::array();
    for (const auto& operation : instance.overrides)
    {
        overrides.push_back(serialization::canonicalize_json(operation));
    }
    json fallback = json::array();
    for (const auto& entity : instance.fallback_entities)
    {
        fallback.push_back(entity_json(entity));
    }
    return {
        {"entityMappings", std::move(mappings)},
        {"fallbackEntities", std::move(fallback)},
        {"instanceId", instance.instance_id.to_string()},
        {"overrides", std::move(overrides)},
        {"placementParentId", instance.placement_parent_id ? json(instance.placement_parent_id->to_string()) : json(nullptr)},
        {"rootEntityId", instance.root_entity_id.to_string()},
        {"sourceAssetId", instance.source_asset_id.to_string()},
        {"sourceRevision", instance.source_revision},
    };
}

json document_json(const document& value, bool include_revision)
{
    json entities = json::array();
    for (const auto& entity : value.entities)
    {
        entities.push_back(entity_json(entity));
    }
    json instances = json::array();
    for (const auto& instance : value.prefab_instances)
    {
        instances.push_back(instance_json(instance));
    }
    json dependencies = json::array();
    std::vector<std::string> dependency_ids;
    dependency_ids.reserve(value.dependencies.size());
    for (const auto& dependency : value.dependencies)
    {
        dependency_ids.push_back(dependency.to_string());
    }
    std::sort(dependency_ids.begin(), dependency_ids.end());
    dependency_ids.erase(std::unique(dependency_ids.begin(), dependency_ids.end()), dependency_ids.end());
    for (const auto& dependency : dependency_ids)
    {
        dependencies.push_back(dependency);
    }

    json root{
        {"$schema", "https://dragonpixel.dev/schemas/v1/prefab.schema.json"},
        {"dependencies", std::move(dependencies)},
        {"engineVersion", value.engine_version},
        {"entities", std::move(entities)},
        {"format", "dpe.prefab"},
        {"formatVersion", 1},
        {"prefabId", value.prefab_id.to_string()},
        {"prefabInstances", std::move(instances)},
        {"revision", include_revision ? value.revision : std::string{}},
        {"rootEntityId", value.root_entity_id.to_string()},
    };
    return serialization::canonicalize_json(root);
}

std::optional<entity_mapping> parse_mapping(const json& value)
{
    if (!value.is_object() || !value.contains("nestedPath") || !value["nestedPath"].is_array())
    {
        return std::nullopt;
    }
    const auto source = core::uuid::parse(value.value("sourceEntityId", std::string{}));
    const auto instance = core::uuid::parse(value.value("instanceEntityId", std::string{}));
    if (!source || !instance)
    {
        return std::nullopt;
    }
    entity_mapping result;
    result.source_entity_id = *source;
    result.instance_entity_id = *instance;
    for (const auto& path_value : value["nestedPath"])
    {
        auto id = core::uuid::parse(path_value.get<std::string>());
        if (!id)
        {
            return std::nullopt;
        }
        result.nested_path.push_back(*id);
    }
    return result;
}

std::optional<instance_record> parse_instance(const json& value)
{
    const auto instance_id = core::uuid::parse(value.value("instanceId", std::string{}));
    const auto source_id = core::uuid::parse(value.value("sourceAssetId", std::string{}));
    const auto root_id = core::uuid::parse(value.value("rootEntityId", std::string{}));
    if (!instance_id || !source_id || !root_id || !value.contains("entityMappings")
        || !value["entityMappings"].is_array() || !value.contains("overrides")
        || !value["overrides"].is_array() || !value.contains("fallbackEntities")
        || !value["fallbackEntities"].is_array())
    {
        return std::nullopt;
    }
    instance_record result;
    result.instance_id = *instance_id;
    result.source_asset_id = *source_id;
    result.source_revision = value.value("sourceRevision", std::string{});
    result.root_entity_id = *root_id;
    if (value.contains("placementParentId") && !value["placementParentId"].is_null())
    {
        result.placement_parent_id = core::uuid::parse(value["placementParentId"].get<std::string>());
        if (!result.placement_parent_id)
        {
            return std::nullopt;
        }
    }
    for (const auto& mapping_value : value["entityMappings"])
    {
        auto mapping = parse_mapping(mapping_value);
        if (!mapping)
        {
            return std::nullopt;
        }
        result.entity_mappings.push_back(std::move(*mapping));
    }
    for (const auto& operation : value["overrides"])
    {
        result.overrides.push_back(operation);
    }
    for (const auto& fallback_value : value["fallbackEntities"])
    {
        auto entity = parse_entity(fallback_value);
        if (!entity)
        {
            return std::nullopt;
        }
        result.fallback_entities.push_back(std::move(*entity));
    }
    return result;
}

class sha256 final
{
public:
    void update(const std::uint8_t* data, std::size_t size)
    {
        total_bits_ += static_cast<std::uint64_t>(size) * 8U;
        while (size > 0)
        {
            const auto copied = std::min(size, block_.size() - block_size_);
            std::memcpy(block_.data() + block_size_, data, copied);
            block_size_ += copied;
            data += copied;
            size -= copied;
            if (block_size_ == block_.size())
            {
                transform();
                block_size_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish()
    {
        block_[block_size_++] = 0x80;
        if (block_size_ > 56)
        {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0);
            transform();
            block_size_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56, 0);
        for (int index = 0; index < 8; ++index)
        {
            block_[63 - index] = static_cast<std::uint8_t>(total_bits_ >> (index * 8));
        }
        transform();
        std::array<std::uint8_t, 32> result{};
        for (std::size_t index = 0; index < state_.size(); ++index)
        {
            result[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24);
            result[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
            result[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
            result[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
        }
        return result;
    }

private:
    static constexpr std::array<std::uint32_t, 64> constants_{
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U,
    };

    void transform()
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index)
        {
            words[index] = (static_cast<std::uint32_t>(block_[index * 4]) << 24)
                | (static_cast<std::uint32_t>(block_[index * 4 + 1]) << 16)
                | (static_cast<std::uint32_t>(block_[index * 4 + 2]) << 8)
                | static_cast<std::uint32_t>(block_[index * 4 + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index)
        {
            const auto s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const auto s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const auto s1 = std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
            const auto choice = (e&f)^((~e)&g);
            const auto t1 = h+s1+choice+constants_[index]+words[index];
            const auto s0 = std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
            const auto majority = (a&b)^(a&c)^(b&c);
            const auto t2 = s0+majority;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_{};
    std::uint64_t total_bits_{};
};

bool same_path(std::span<const core::uuid> left, std::span<const core::uuid> right)
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

const entity_mapping* find_mapping(
    const instance_record& instance,
    std::span<const core::uuid> path,
    const core::uuid& source_id)
{
    const auto found = std::find_if(instance.entity_mappings.begin(), instance.entity_mappings.end(), [&](const auto& mapping) {
        return mapping.source_entity_id == source_id && same_path(mapping.nested_path, path);
    });
    return found == instance.entity_mappings.end() ? nullptr : &*found;
}

void remap_json_references(json& value, const instance_record& instance)
{
    if (value.is_string())
    {
        const auto id = core::uuid::parse(value.get<std::string>());
        if (id)
        {
            const auto found = std::find_if(instance.entity_mappings.begin(), instance.entity_mappings.end(), [&](const auto& mapping) {
                return mapping.source_entity_id == *id;
            });
            if (found != instance.entity_mappings.end())
            {
                value = found->instance_entity_id.to_string();
            }
        }
        return;
    }
    if (value.is_array())
    {
        for (auto& child : value)
        {
            remap_json_references(child, instance);
        }
    }
    else if (value.is_object())
    {
        for (auto& [key, child] : value.items())
        {
            static_cast<void>(key);
            remap_json_references(child, instance);
        }
    }
}

struct resolve_context final
{
    const instance_record& outer;
    const source_provider& sources;
    resolve_limits limits;
    resolve_result result;
    std::vector<core::uuid> dependency_stack;
};

void materialize(
    resolve_context& context,
    const document& source,
    std::vector<core::uuid> path,
    std::optional<core::uuid> placement_parent,
    std::size_t depth)
{
    if (depth > context.limits.maximum_depth)
    {
        context.result.diagnostics.push_back(diagnostic(
            core::diagnostic_severity::error,
            "DPE.PREFAB.DEPTH_LIMIT",
            "Prefab expansion exceeded the configured nesting depth.",
            source.prefab_id.to_string()));
        return;
    }
    if (std::find(context.dependency_stack.begin(), context.dependency_stack.end(), source.prefab_id)
        != context.dependency_stack.end())
    {
        context.result.diagnostics.push_back(diagnostic(
            core::diagnostic_severity::error,
            "DPE.PREFAB.CYCLE",
            "Prefab expansion encountered a dependency cycle.",
            source.prefab_id.to_string()));
        return;
    }
    context.dependency_stack.push_back(source.prefab_id);

    for (const auto& source_entity : source.entities)
    {
        if (context.result.entities.size() >= context.limits.maximum_entities)
        {
            context.result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::error,
                "DPE.PREFAB.ENTITY_LIMIT",
                "Prefab expansion exceeded the configured entity limit.",
                source.prefab_id.to_string()));
            context.dependency_stack.pop_back();
            return;
        }
        const auto* mapping = find_mapping(context.outer, path, source_entity.id);
        if (mapping == nullptr)
        {
            context.result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::error,
                "DPE.PREFAB.MISSING_MAPPING",
                "Prefab instance is missing a stable entity mapping.",
                source_entity.id.to_string()));
            continue;
        }
        auto entity = source_entity;
        entity.id = mapping->instance_entity_id;
        if (source_entity.parent_id)
        {
            const auto* parent_mapping = find_mapping(context.outer, path, *source_entity.parent_id);
            entity.parent_id = parent_mapping
                ? std::optional<core::uuid>{parent_mapping->instance_entity_id}
                : placement_parent;
        }
        else
        {
            entity.parent_id = placement_parent;
        }
        for (auto& component : entity.components)
        {
            if (component.opaque)
            {
                remap_json_references(component.raw_record, context.outer);
            }
            else
            {
                remap_json_references(component.properties, context.outer);
            }
        }
        context.result.entities.push_back(std::move(entity));
    }

    for (const auto& nested : source.prefab_instances)
    {
        auto nested_path = path;
        nested_path.push_back(nested.instance_id);
        std::optional<core::uuid> nested_parent = placement_parent;
        if (nested.placement_parent_id)
        {
            if (const auto* parent_mapping = find_mapping(context.outer, path, *nested.placement_parent_id))
            {
                nested_parent = parent_mapping->instance_entity_id;
            }
        }
        const auto* dependency = context.sources(nested.source_asset_id);
        if (dependency == nullptr)
        {
            context.result.used_fallback = true;
            context.result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::warning,
                "DPE.PREFAB.MISSING_SOURCE",
                "Nested prefab source is unavailable; the last resolved fallback was retained.",
                nested.source_asset_id.to_string()));
            for (auto fallback : nested.fallback_entities)
            {
                if (const auto* mapping = find_mapping(context.outer, nested_path, fallback.id))
                {
                    fallback.id = mapping->instance_entity_id;
                }
                fallback.parent_id = nested_parent;
                context.result.entities.push_back(std::move(fallback));
            }
            continue;
        }
        materialize(context, *dependency, std::move(nested_path), nested_parent, depth + 1);
    }
    context.dependency_stack.pop_back();
}

scene::entity* find_override_target(std::vector<scene::entity>& entities, const instance_record& instance, const json& target)
{
    if (!target.is_object())
    {
        return nullptr;
    }
    std::optional<core::uuid> instance_id;
    if (target.contains("localEntityId"))
    {
        instance_id = core::uuid::parse(target["localEntityId"].get<std::string>());
    }
    else if (target.contains("sourceEntityId"))
    {
        const auto source_id = core::uuid::parse(target["sourceEntityId"].get<std::string>());
        std::vector<core::uuid> path;
        if (target.contains("nestedPath") && target["nestedPath"].is_array())
        {
            for (const auto& value : target["nestedPath"])
            {
                auto id = core::uuid::parse(value.get<std::string>());
                if (!id)
                {
                    return nullptr;
                }
                path.push_back(*id);
            }
        }
        if (source_id)
        {
            if (const auto* mapping = find_mapping(instance, path, *source_id))
            {
                instance_id = mapping->instance_entity_id;
            }
        }
    }
    if (!instance_id)
    {
        return nullptr;
    }
    const auto found = std::find_if(entities.begin(), entities.end(), [&](const auto& entity) {
        return entity.id == *instance_id;
    });
    return found == entities.end() ? nullptr : &*found;
}

void apply_overrides(resolve_result& result, const instance_record& instance)
{
    for (const auto& operation : instance.overrides)
    {
        const auto name = operation.value("op", std::string{});
        if (name == "add-entity")
        {
            auto entity = parse_entity(operation.value("entity", json::object()));
            if (entity)
            {
                result.entities.push_back(std::move(*entity));
            }
            else
            {
                result.diagnostics.push_back(diagnostic(
                    core::diagnostic_severity::warning,
                    "DPE.PREFAB.INVALID_OVERRIDE",
                    "An add-entity override was not valid."));
            }
            continue;
        }
        auto* entity = find_override_target(result.entities, instance, operation.value("target", json::object()));
        if (entity == nullptr)
        {
            result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::warning,
                "DPE.PREFAB.UNRESOLVED_OVERRIDE",
                "A prefab override target could not be resolved.",
                operation.dump()));
            continue;
        }
        if (name == "rename-entity")
        {
            entity->name = operation.value("value", entity->name);
        }
        else if (name == "set-entity-enabled")
        {
            entity->enabled = operation.value("value", entity->enabled);
        }
        else if (name == "reorder-entity")
        {
            entity->sibling_order = operation.value("value", entity->sibling_order);
        }
        else if (name == "reparent-entity")
        {
            const auto parent = core::uuid::parse(operation.value("value", std::string{}));
            entity->parent_id = parent;
        }
        else if (name == "remove-entity")
        {
            const auto root = entity->id;
            std::unordered_set<core::uuid, core::uuid_hash> removed{root};
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (const auto& candidate : result.entities)
                {
                    if (candidate.parent_id && removed.contains(*candidate.parent_id)
                        && removed.insert(candidate.id).second)
                    {
                        changed = true;
                    }
                }
            }
            std::erase_if(result.entities, [&](const auto& candidate) { return removed.contains(candidate.id); });
        }
        else
        {
            const auto component_type = operation.value("componentTypeId", std::string{});
            auto component = std::find_if(entity->components.begin(), entity->components.end(), [&](const auto& value) {
                return value.type_id == component_type;
            });
            if (name == "remove-component")
            {
                std::erase_if(entity->components, [&](const auto& value) { return value.type_id == component_type; });
            }
            else if (name == "add-component")
            {
                if (auto added = parse_component(operation.value("component", json::object())))
                {
                    entity->components.push_back(std::move(*added));
                }
            }
            else if (component != entity->components.end() && name == "set-component-enabled")
            {
                component->enabled = operation.value("value", component->enabled);
            }
            else if (component != entity->components.end() && name == "reorder-component")
            {
                const auto destination = operation.value("value", entity->components.size());
                if (destination >= entity->components.size())
                {
                    result.diagnostics.push_back(diagnostic(
                        core::diagnostic_severity::warning,
                        "DPE.PREFAB.INVALID_COMPONENT_ORDER",
                        "A prefab component reorder destination was out of range.",
                        component_type));
                    continue;
                }
                auto moved = std::move(*component);
                entity->components.erase(component);
                entity->components.insert(
                    entity->components.begin() + static_cast<std::ptrdiff_t>(destination),
                    std::move(moved));
            }
            else if (component != entity->components.end() && name == "set-property" && !component->opaque)
            {
                const auto property_id = operation.value("propertyId", std::string{});
                component->properties[property_id] = operation.value("value", json{nullptr});
            }
            else
            {
                result.diagnostics.push_back(diagnostic(
                    core::diagnostic_severity::warning,
                    "DPE.PREFAB.UNSUPPORTED_OVERRIDE",
                    "A prefab override could not be applied.",
                    name));
            }
        }
    }
}

void validate_graph_recursive(
    const document& current,
    const source_provider& sources,
    resolve_limits limits,
    std::vector<core::uuid>& path,
    std::unordered_set<core::uuid, core::uuid_hash>& visited,
    std::vector<core::diagnostic>& diagnostics)
{
    if (path.size() > limits.maximum_depth)
    {
        diagnostics.push_back(diagnostic(
            core::diagnostic_severity::error,
            "DPE.PREFAB.DEPTH_LIMIT",
            "Prefab dependency graph exceeded the nesting-depth limit.",
            current.prefab_id.to_string()));
        return;
    }
    if (std::find(path.begin(), path.end(), current.prefab_id) != path.end())
    {
        diagnostics.push_back(diagnostic(
            core::diagnostic_severity::error,
            "DPE.PREFAB.CYCLE",
            "Prefab dependency graph contains a direct or indirect cycle.",
            current.prefab_id.to_string()));
        return;
    }
    if (visited.contains(current.prefab_id))
    {
        return;
    }
    path.push_back(current.prefab_id);
    for (const auto& dependency_id : current.dependencies)
    {
        const auto* dependency = sources(dependency_id);
        if (dependency == nullptr)
        {
            diagnostics.push_back(diagnostic(
                core::diagnostic_severity::warning,
                "DPE.PREFAB.MISSING_SOURCE",
                "Prefab dependency source is unavailable.",
                dependency_id.to_string()));
            continue;
        }
        validate_graph_recursive(*dependency, sources, limits, path, visited, diagnostics);
    }
    path.pop_back();
    visited.insert(current.prefab_id);
}
}

std::string write_json(const document& value)
{
    auto copy = value;
    if (copy.revision.empty())
    {
        copy.revision = compute_revision(copy);
    }
    return document_json(copy, true).dump(2) + "\n";
}

load_result read_json(std::string_view text)
{
    load_result result;
    try
    {
        const auto root = json::parse(text);
        if (!root.is_object() || root.value("format", std::string{}) != "dpe.prefab"
            || root.value("formatVersion", 0) != 1
            || root.value("$schema", std::string{}) != "https://dragonpixel.dev/schemas/v1/prefab.schema.json")
        {
            result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::error,
                "DPE.PREFAB.UNSUPPORTED_FORMAT",
                "Prefab document format or schema was unsupported."));
            return result;
        }
        const auto prefab_id = core::uuid::parse(root.value("prefabId", std::string{}));
        const auto root_id = core::uuid::parse(root.value("rootEntityId", std::string{}));
        if (!prefab_id || !root_id || !root.contains("entities") || !root["entities"].is_array()
            || !root.contains("prefabInstances") || !root["prefabInstances"].is_array()
            || !root.contains("dependencies") || !root["dependencies"].is_array())
        {
            result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::error,
                "DPE.PREFAB.INVALID_DOCUMENT",
                "Prefab identity or required arrays were invalid."));
            return result;
        }
        document value;
        value.prefab_id = *prefab_id;
        value.root_entity_id = *root_id;
        value.engine_version = root.value("engineVersion", std::string{});
        value.revision = root.value("revision", std::string{});
        for (const auto& entity_value : root["entities"])
        {
            auto entity = parse_entity(entity_value);
            if (!entity)
            {
                throw std::invalid_argument{"invalid prefab entity"};
            }
            value.entities.push_back(std::move(*entity));
        }
        for (const auto& instance_value : root["prefabInstances"])
        {
            auto instance = parse_instance(instance_value);
            if (!instance)
            {
                throw std::invalid_argument{"invalid nested prefab instance"};
            }
            value.prefab_instances.push_back(std::move(*instance));
        }
        for (const auto& dependency_value : root["dependencies"])
        {
            auto dependency = core::uuid::parse(dependency_value.get<std::string>());
            if (!dependency)
            {
                throw std::invalid_argument{"invalid prefab dependency"};
            }
            value.dependencies.push_back(*dependency);
        }
        if (value.revision.size() != 64 || compute_revision(value) != value.revision)
        {
            result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::warning,
                "DPE.PREFAB.STALE_REVISION",
                "Prefab canonical source revision did not match its content.",
                value.prefab_id.to_string()));
        }
        result.value = std::move(value);
    }
    catch (const std::exception& exception)
    {
        result.diagnostics.push_back(diagnostic(
            core::diagnostic_severity::error,
            "DPE.PREFAB.PARSE_ERROR",
            exception.what()));
    }
    return result;
}

std::string compute_revision(const document& value)
{
    const auto canonical = document_json(value, false).dump();
    sha256 hash;
    hash.update(reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size());
    const auto digest = hash.finish();
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto byte : digest)
    {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

resolve_result resolve(
    const instance_record& instance,
    const source_provider& sources,
    resolve_limits limits)
{
    const auto* source = sources(instance.source_asset_id);
    if (source == nullptr)
    {
        resolve_result fallback;
        fallback.entities = instance.fallback_entities;
        fallback.used_fallback = true;
        fallback.diagnostics.push_back(diagnostic(
            core::diagnostic_severity::warning,
            "DPE.PREFAB.MISSING_SOURCE",
            "Prefab source is unavailable; the last resolved fallback was retained.",
            instance.source_asset_id.to_string()));
        return fallback;
    }

    resolve_context context{instance, sources, limits, {}, {}};
    materialize(context, *source, {}, instance.placement_parent_id, 0);
    apply_overrides(context.result, instance);
    std::sort(context.result.entities.begin(), context.result.entities.end(), [](const auto& left, const auto& right) {
        const auto left_parent = left.parent_id ? left.parent_id->to_string() : std::string{};
        const auto right_parent = right.parent_id ? right.parent_id->to_string() : std::string{};
        if (left_parent != right_parent)
        {
            return left_parent < right_parent;
        }
        if (left.sibling_order != right.sibling_order)
        {
            return left.sibling_order < right.sibling_order;
        }
        return left.id.to_string() < right.id.to_string();
    });
    return context.result;
}

std::vector<core::diagnostic> validate_dependency_graph(
    const document& root,
    const source_provider& sources,
    resolve_limits limits)
{
    std::vector<core::diagnostic> diagnostics;
    std::vector<core::uuid> path;
    std::unordered_set<core::uuid, core::uuid_hash> visited;
    validate_graph_recursive(root, sources, limits, path, visited, diagnostics);
    return diagnostics;
}

rebase_result rebase(
    const instance_record& current,
    const document& new_source,
    std::span<const rebase_allocation> allocations)
{
    rebase_result result;
    result.instance = current;
    result.instance.source_revision = compute_revision(new_source);

    std::vector<entity_mapping> retained;
    retained.reserve(current.entity_mappings.size() + allocations.size());
    auto source_exists = [&](const entity_mapping& mapping) {
        if (!mapping.nested_path.empty())
        {
            return true;
        }
        return std::any_of(new_source.entities.begin(), new_source.entities.end(), [&](const auto& entity) {
            return entity.id == mapping.source_entity_id;
        });
    };
    for (const auto& mapping : current.entity_mappings)
    {
        if (source_exists(mapping))
        {
            retained.push_back(mapping);
        }
        else
        {
            result.diagnostics.push_back(diagnostic(
                core::diagnostic_severity::warning,
                "DPE.PREFAB.REBASE_REMOVED_SOURCE",
                "A removed source entity mapping was retained only in fallback diagnostics.",
                mapping.source_entity_id.to_string()));
        }
    }
    for (const auto& allocation : allocations)
    {
        const auto duplicate = std::any_of(retained.begin(), retained.end(), [&](const auto& mapping) {
            return mapping.source_entity_id == allocation.source_entity_id
                && same_path(mapping.nested_path, allocation.nested_path);
        });
        if (!duplicate)
        {
            retained.push_back({
                allocation.nested_path,
                allocation.source_entity_id,
                allocation.instance_entity_id,
            });
        }
    }
    result.instance.entity_mappings = std::move(retained);
    return result;
}

apply_result apply_to_source(
    const instance_record& current,
    const document& target_source,
    std::span<const core::uuid> nesting_path)
{
    apply_result result;
    result.remaining_instance = current;
    if (nesting_path.empty() && current.source_asset_id != target_source.prefab_id)
    {
        result.diagnostics.push_back(diagnostic(
            core::diagnostic_severity::error,
            "DPE.PREFAB.APPLY_SOURCE_MISMATCH",
            "Apply target does not match the instance source asset.",
            target_source.prefab_id.to_string()));
        return result;
    }

    auto matches_path = [&](const json& operation, bool& valid) {
        valid = true;
        const json* path_value = nullptr;
        if (operation.contains("target") && operation["target"].is_object()
            && operation["target"].contains("nestedPath"))
        {
            path_value = &operation["target"].at("nestedPath");
        }
        else if (operation.contains("nestedPath"))
        {
            path_value = &operation.at("nestedPath");
        }
        std::vector<core::uuid> operation_path;
        if (path_value != nullptr && !path_value->is_null())
        {
            if (!path_value->is_array())
            {
                valid = false;
                return false;
            }
            for (const auto& value : *path_value)
            {
                if (!value.is_string())
                {
                    valid = false;
                    return false;
                }
                const auto parsed = core::uuid::parse(value.get<std::string>());
                if (!parsed)
                {
                    valid = false;
                    return false;
                }
                operation_path.push_back(*parsed);
            }
        }
        return same_path(operation_path, nesting_path);
    };

    instance_record local;
    for (const auto& entity : target_source.entities)
    {
        local.entity_mappings.push_back({{}, entity.id, entity.id});
    }
    std::unordered_map<std::string, std::string> inverse_entity_ids;
    for (const auto& mapping : current.entity_mappings)
    {
        if (same_path(mapping.nested_path, nesting_path))
        {
            inverse_entity_ids.insert_or_assign(
                mapping.instance_entity_id.to_string(),
                mapping.source_entity_id.to_string());
        }
    }
    const std::function<void(json&)> localize_entity_ids = [&](json& value) {
        if (value.is_string())
        {
            const auto found = inverse_entity_ids.find(value.get<std::string>());
            if (found != inverse_entity_ids.end())
            {
                value = found->second;
            }
            return;
        }
        if (value.is_array() || value.is_object())
        {
            for (auto& child : value)
            {
                localize_entity_ids(child);
            }
        }
    };
    std::vector<json> remaining;
    for (const auto& operation : current.overrides)
    {
        bool valid = false;
        if (!matches_path(operation, valid))
        {
            remaining.push_back(operation);
            if (!valid)
            {
                result.diagnostics.push_back(diagnostic(
                    core::diagnostic_severity::warning,
                    "DPE.PREFAB.INVALID_OVERRIDE_PATH",
                    "An override path was malformed and was retained instead of being applied.",
                    operation.dump()));
            }
            continue;
        }
        auto localized = operation;
        if (localized.contains("target") && localized["target"].is_object())
        {
            localized["target"]["nestedPath"] = json::array();
        }
        localized["nestedPath"] = json::array();
        for (const auto* payload_key : {"value", "component", "entity"})
        {
            if (localized.contains(payload_key))
            {
                localize_entity_ids(localized[payload_key]);
            }
        }
        local.overrides.push_back(std::move(localized));
    }

    auto updated = target_source;
    resolve_result applied;
    applied.entities = updated.entities;
    apply_overrides(applied, local);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(applied.diagnostics.begin()),
        std::make_move_iterator(applied.diagnostics.end()));
    if (std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& value) {
            return value.severity == core::diagnostic_severity::error;
        }))
    {
        return result;
    }
    const auto root_exists = std::any_of(applied.entities.begin(), applied.entities.end(), [&](const auto& entity) {
        return entity.id == updated.root_entity_id;
    });
    if (!root_exists)
    {
        result.diagnostics.push_back(diagnostic(
            core::diagnostic_severity::error,
            "DPE.PREFAB.APPLY_REMOVED_ROOT",
            "Apply cannot remove the prefab root entity.",
            updated.root_entity_id.to_string()));
        return result;
    }
    updated.entities = std::move(applied.entities);
    updated.revision = compute_revision(updated);
    result.remaining_instance.overrides = std::move(remaining);
    if (nesting_path.empty())
    {
        result.remaining_instance.source_revision = updated.revision;
    }
    result.source = std::move(updated);
    return result;
}

resolve_result unpack_completely(
    const instance_record& instance,
    const source_provider& sources,
    resolve_limits limits)
{
    return resolve(instance, sources, limits);
}

void revert_selected(instance_record& instance, std::span<const std::size_t> override_indexes)
{
    std::unordered_set<std::size_t> removed;
    for (const auto index : override_indexes)
    {
        if (index < instance.overrides.size())
        {
            removed.insert(index);
        }
    }
    std::vector<json> retained;
    retained.reserve(instance.overrides.size() - removed.size());
    for (std::size_t index = 0; index < instance.overrides.size(); ++index)
    {
        if (!removed.contains(index))
        {
            retained.push_back(std::move(instance.overrides[index]));
        }
    }
    instance.overrides = std::move(retained);
}

void revert_all(instance_record& instance) noexcept
{
    instance.overrides.clear();
}
}
