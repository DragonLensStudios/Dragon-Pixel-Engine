#include <dragonpixel/scene/scene.h>

#include <dragonpixel/metadata/builtin_ids.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dragonpixel::scene
{
namespace
{
std::string hierarchy_key(const std::optional<core::uuid>& parent_id)
{
    return parent_id ? parent_id->to_string() : std::string{};
}

component_record make_component(
    std::string_view type_id,
    std::string qualified_name,
    nlohmann::ordered_json properties)
{
    return {
        std::string{type_id},
        type_id == metadata::builtin_component_ids::transform ? 2U : 1U,
        metadata::runtime_owner::native,
        std::move(properties),
        false,
        nlohmann::ordered_json::object(),
        true,
        std::move(qualified_name),
    };
}

component_record make_transform_component()
{
    return make_component(
        metadata::builtin_component_ids::transform,
        "DragonPixel.Native.TransformComponent",
        {
            {"dpe.transform.position", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}},
            {"dpe.transform.rotation", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"w", 1.0}}},
            {"dpe.transform.scale", {{"x", 1.0}, {"y", 1.0}, {"z", 1.0}}},
        });
}

std::vector<component_record> make_preset_components(const create_preset_command& value)
{
    std::vector<component_record> components;
    components.push_back(make_transform_component());
    switch (value.preset)
    {
    case entity_preset::empty:
        break;
    case entity_preset::sprite:
        components.push_back(make_component(
            metadata::builtin_component_ids::sprite,
            "DragonPixel.Native.SpriteComponent",
            {
                {"dpe.sprite.asset", value.primary_asset.value_or("builtin://default-sprite")},
                {"dpe.sprite.color", {{"r", 1.0}, {"g", 1.0}, {"b", 1.0}, {"a", 1.0}}},
                {"dpe.sprite.layer", 0},
            }));
        break;
    case entity_preset::cube:
    {
        const auto material = value.material_asset.value_or("builtin://default-material");
        components.push_back(make_component(
            metadata::builtin_component_ids::mesh,
            "DragonPixel.Native.StaticMeshComponent",
            {
                {"dpe.mesh.asset", value.primary_asset.value_or("builtin://cube")},
                {"dpe.mesh.material", material},
            }));
        components.push_back(make_component(
            metadata::builtin_component_ids::material,
            "DragonPixel.Native.MaterialComponent",
            {
                {"dpe.material.base_color", {{"r", 1.0}, {"g", 1.0}, {"b", 1.0}, {"a", 1.0}}},
                {"dpe.material.roughness", 0.5},
            }));
        break;
    }
    case entity_preset::camera:
        components.push_back(make_component(
            metadata::builtin_component_ids::camera,
            "DragonPixel.Native.CameraComponent",
            {
                {"dpe.camera.primary", true},
                {"dpe.camera.projection", "perspective"},
                {"dpe.camera.field_of_view", 60.0},
                {"dpe.camera.orthographic_size", 10.0},
                {"dpe.camera.near", 0.1},
                {"dpe.camera.far", 1000.0},
            }));
        break;
    case entity_preset::light:
        components.push_back(make_component(
            metadata::builtin_component_ids::light,
            "DragonPixel.Native.LightComponent",
            {
                {"dpe.light.kind", "directional"},
                {"dpe.light.color", {{"r", 1.0}, {"g", 1.0}, {"b", 1.0}, {"a", 1.0}}},
                {"dpe.light.intensity", 1.0},
                {"dpe.light.range", 10.0},
            }));
        break;
    case entity_preset::tilemap:
        components.push_back(make_component(
            metadata::builtin_component_ids::tilemap_2d,
            "DragonPixel.Native.Tilemap2DComponent",
            {
                {"dpe.tilemap.asset", value.primary_asset.value_or("")},
                {"dpe.tilemap.tint", {{"r", 1.0}, {"g", 1.0}, {"b", 1.0}, {"a", 1.0}}},
                {"dpe.tilemap.layer", 0},
            }));
        break;
    }
    return components;
}

void remap_known_references(
    nlohmann::ordered_json& value,
    const std::unordered_map<std::string, std::string>& remaps)
{
    if (value.is_string())
    {
        const auto found = remaps.find(value.get<std::string>());
        if (found != remaps.end())
        {
            value = found->second;
        }
        return;
    }
    if (value.is_array())
    {
        for (auto& item : value)
        {
            remap_known_references(item, remaps);
        }
        return;
    }
    if (value.is_object())
    {
        for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
        {
            remap_known_references(iterator.value(), remaps);
        }
    }
}

bool set_json_path(
    nlohmann::ordered_json& root,
    const std::vector<std::string>& path,
    const nlohmann::ordered_json& value)
{
    if (path.empty())
    {
        return false;
    }
    auto* cursor = &root;
    for (std::size_t path_index = 0; path_index < path.size(); ++path_index)
    {
        const auto& segment = path[path_index];
        const auto is_leaf = path_index + 1 == path.size();
        if (cursor->is_object())
        {
            if (!cursor->contains(segment) && !is_leaf)
            {
                return false;
            }
            if (is_leaf)
            {
                (*cursor)[segment] = value;
                return true;
            }
            cursor = &(*cursor)[segment];
            continue;
        }
        if (!cursor->is_array())
        {
            return false;
        }
        std::size_t parsed{};
        const auto [end, error] = std::from_chars(segment.data(), segment.data() + segment.size(), parsed);
        if (error != std::errc{} || end != segment.data() + segment.size() || parsed >= cursor->size())
        {
            return false;
        }
        if (is_leaf)
        {
            (*cursor)[parsed] = value;
            return true;
        }
        cursor = &(*cursor)[parsed];
    }
    return false;
}

std::unordered_set<core::uuid, core::uuid_hash> collect_subtree(
    std::span<const entity> entities,
    const core::uuid& root_id)
{
    std::unordered_set<core::uuid, core::uuid_hash> result;
    result.insert(root_id);
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& item : entities)
        {
            if (item.parent_id && result.contains(*item.parent_id) && result.insert(item.id).second)
            {
                changed = true;
            }
        }
    }
    return result;
}
}

scene::scene(core::uuid id, std::string name) : id_(id), name_(std::move(name)) {}

scene::scene(core::uuid id, std::string name, std::vector<entity> entities)
    : scene(id, std::move(name), std::move(entities), scene_document_extras{})
{
}

scene::scene(core::uuid id, std::string name, std::vector<entity> entities, scene_document_extras extras)
    : id_(id),
      name_(std::move(name)),
      entities_(std::move(entities)),
      physics_settings_(extras.physics),
      prefab_instances_(std::move(extras.prefab_instances))
{
    if (!extras.has_explicit_sibling_order)
    {
        derive_sibling_order_from_storage();
    }
}

const entity* scene::find_entity(const core::uuid& id) const noexcept
{
    const auto found = std::find_if(entities_.begin(), entities_.end(), [&id](const auto& value) {
        return value.id == id;
    });
    return found == entities_.end() ? nullptr : &*found;
}

entity* scene::find_entity_mutable(const core::uuid& id) noexcept
{
    const auto found = std::find_if(entities_.begin(), entities_.end(), [&id](const auto& value) {
        return value.id == id;
    });
    return found == entities_.end() ? nullptr : &*found;
}

command_result scene::apply(const command& value, std::string description)
{
    auto before = capture_state();
    auto result = apply_untracked(value);
    if (!result.succeeded)
    {
        restore_state(std::move(before));
        return result;
    }
    if (const auto validation = validate())
    {
        restore_state(std::move(before));
        return {false, validation};
    }
    record_history(std::move(before), std::move(description));
    return result;
}

transaction_result scene::apply_transaction(std::span<const command> commands, std::string description)
{
    auto before = capture_state();
    auto result = apply_transaction_untracked(commands);
    if (!result.succeeded)
    {
        restore_state(std::move(before));
        return result;
    }
    if (const auto validation = validate())
    {
        restore_state(std::move(before));
        return {false, commands.size(), validation};
    }
    record_history(std::move(before), std::move(description));
    return result;
}

command_result scene::dry_run(const command& value) const
{
    auto candidate = *this;
    auto result = candidate.apply_untracked(value);
    if (!result.succeeded)
    {
        return result;
    }
    if (const auto validation = candidate.validate())
    {
        return {false, validation};
    }
    return result;
}

transaction_result scene::dry_run_transaction(std::span<const command> commands) const
{
    auto candidate = *this;
    auto result = candidate.apply_transaction_untracked(commands);
    if (!result.succeeded)
    {
        return result;
    }
    if (const auto validation = candidate.validate())
    {
        return {false, commands.size(), validation};
    }
    return result;
}

command_result scene::undo()
{
    if (!can_undo())
    {
        return failure("DPE.SCENE.NOTHING_TO_UNDO", "Command history has no earlier state.");
    }
    const auto entry_index = history_position_ - 1;
    restore_state(history_[entry_index].before);
    history_position_ = entry_index;
    return {true, std::nullopt};
}

command_result scene::redo()
{
    if (!can_redo())
    {
        return failure("DPE.SCENE.NOTHING_TO_REDO", "Command history has no later state.");
    }
    restore_state(history_[history_position_].after);
    ++history_position_;
    return {true, std::nullopt};
}

command_result scene::apply_untracked(const command& value)
{
    return std::visit([this](const auto& concrete) -> command_result {
        using command_type = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<command_type, create_entity_command>)
        {
            if (concrete.entity_id.is_nil() || concrete.name.empty())
            {
                return failure("DPE.SCENE.INVALID_ENTITY", "Entity ID and name are required.");
            }
            if (find_entity(concrete.entity_id) != nullptr)
            {
                return failure("DPE.SCENE.DUPLICATE_ENTITY", "Entity ID already exists.", concrete.entity_id.to_string());
            }
            if (concrete.parent_id && find_entity(*concrete.parent_id) == nullptr)
            {
                return failure("DPE.SCENE.MISSING_PARENT", "Parent entity does not exist.", concrete.parent_id->to_string());
            }
            const auto count = sibling_count(concrete.parent_id);
            const auto index = concrete.sibling_index.value_or(count);
            if (index > count || index > std::numeric_limits<std::uint32_t>::max())
            {
                return failure("DPE.SCENE.INVALID_SIBLING_INDEX", "Sibling insertion index was out of range.");
            }
            for (auto& item : entities_)
            {
                if (item.parent_id == concrete.parent_id && item.sibling_order >= index)
                {
                    ++item.sibling_order;
                }
            }
            entities_.push_back(entity{
                concrete.entity_id,
                concrete.name,
                concrete.parent_id,
                {},
                true,
                static_cast<std::uint32_t>(index),
            });
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, create_preset_command>)
        {
            const auto created = apply_untracked(command{create_entity_command{
                concrete.entity_id,
                concrete.name,
                concrete.parent_id,
                concrete.sibling_index,
            }});
            if (!created.succeeded)
            {
                return created;
            }
            find_entity_mutable(concrete.entity_id)->components = make_preset_components(concrete);
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, rename_entity_command>)
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr || concrete.name.empty())
            {
                return failure("DPE.SCENE.RENAME_REJECTED", "Entity was missing or name was empty.");
            }
            target->name = concrete.name;
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, reparent_entity_command>)
        {
            const auto count = sibling_count(concrete.parent_id, concrete.entity_id);
            return move_entity(concrete.entity_id, concrete.parent_id, concrete.sibling_index.value_or(count));
        }
        else if constexpr (std::is_same_v<command_type, reorder_entity_command>)
        {
            const auto* target = find_entity(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.", concrete.entity_id.to_string());
            }
            return move_entity(concrete.entity_id, target->parent_id, concrete.sibling_index);
        }
        else if constexpr (std::is_same_v<command_type, delete_subtree_command>)
        {
            if (find_entity(concrete.root_entity_id) == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Subtree root does not exist.", concrete.root_entity_id.to_string());
            }
            const auto removed = collect_subtree(entities_, concrete.root_entity_id);
            std::erase_if(entities_, [&removed](const auto& item) { return removed.contains(item.id); });
            compact_sibling_order();
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, duplicate_subtree_command>)
        {
            const auto* source_root = find_entity(concrete.root_entity_id);
            if (source_root == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Duplicate source does not exist.", concrete.root_entity_id.to_string());
            }
            const auto source_ids = collect_subtree(entities_, concrete.root_entity_id);
            if (concrete.id_remaps.size() != source_ids.size())
            {
                return failure("DPE.SCENE.INCOMPLETE_ID_REMAP", "Duplicate requires exactly one new ID per subtree entity.");
            }

            std::unordered_map<core::uuid, core::uuid, core::uuid_hash> remaps;
            std::unordered_set<core::uuid, core::uuid_hash> duplicate_ids;
            std::unordered_map<std::string, std::string> string_remaps;
            for (const auto& remap : concrete.id_remaps)
            {
                if (!source_ids.contains(remap.source_id) || remap.duplicate_id.is_nil()
                    || find_entity(remap.duplicate_id) != nullptr || !duplicate_ids.insert(remap.duplicate_id).second
                    || !remaps.emplace(remap.source_id, remap.duplicate_id).second)
                {
                    return failure("DPE.SCENE.INVALID_ID_REMAP", "Duplicate ID mapping was incomplete, repeated, or collided.");
                }
                string_remaps.emplace(remap.source_id.to_string(), remap.duplicate_id.to_string());
            }

            const auto destination_parent = concrete.use_source_parent
                ? source_root->parent_id
                : concrete.destination_parent_id;
            if (destination_parent && find_entity(*destination_parent) == nullptr)
            {
                return failure("DPE.SCENE.MISSING_PARENT", "Duplicate destination parent does not exist.");
            }
            const auto destination_count = sibling_count(destination_parent);
            const auto default_index = concrete.use_source_parent
                ? std::min<std::size_t>(static_cast<std::size_t>(source_root->sibling_order) + 1U, destination_count)
                : destination_count;
            const auto destination_index = concrete.sibling_index.value_or(default_index);
            if (destination_index > destination_count || destination_index > std::numeric_limits<std::uint32_t>::max())
            {
                return failure("DPE.SCENE.INVALID_SIBLING_INDEX", "Duplicate insertion index was out of range.");
            }
            if (concrete.duplicate_root_name && concrete.duplicate_root_name->empty())
            {
                return failure("DPE.SCENE.INVALID_ENTITY", "Duplicate root name cannot be empty.");
            }
            std::unordered_set<std::string> overridden_component_types;
            for (const auto& component : concrete.duplicate_root_component_overrides)
            {
                if (!core::uuid::parse(component.type_id) || component.qualified_name.empty()
                    || component.schema_version == 0 || !component.properties.is_object()
                    || !overridden_component_types.insert(component.type_id).second)
                {
                    return failure("DPE.SCENE.INVALID_COMPONENT",
                        "Duplicate root component overrides must have unique valid identities and object properties.");
                }
            }

            for (auto& item : entities_)
            {
                if (item.parent_id == destination_parent && item.sibling_order >= destination_index)
                {
                    ++item.sibling_order;
                }
            }

            std::vector<entity> duplicates;
            duplicates.reserve(source_ids.size());
            for (const auto& source : entities_)
            {
                if (!source_ids.contains(source.id))
                {
                    continue;
                }
                auto duplicate = source;
                duplicate.id = remaps.at(source.id);
                if (source.id == concrete.root_entity_id)
                {
                    duplicate.parent_id = destination_parent;
                    duplicate.sibling_order = static_cast<std::uint32_t>(destination_index);
                    duplicate.name = concrete.duplicate_root_name.value_or(source.name + " Copy");
                    for (const auto& override_component : concrete.duplicate_root_component_overrides)
                    {
                        const auto existing = std::find_if(
                            duplicate.components.begin(), duplicate.components.end(),
                            [&](const auto& component) {
                                return component.type_id == override_component.type_id;
                            });
                        if (existing == duplicate.components.end())
                            duplicate.components.push_back(override_component);
                        else *existing = override_component;
                    }
                }
                else
                {
                    duplicate.parent_id = remaps.at(*source.parent_id);
                }
                for (auto& component : duplicate.components)
                {
                    if (!component.opaque)
                    {
                        remap_known_references(component.properties, string_remaps);
                    }
                }
                duplicates.push_back(std::move(duplicate));
            }
            entities_.insert(
                entities_.end(),
                std::make_move_iterator(duplicates.begin()),
                std::make_move_iterator(duplicates.end()));
            compact_sibling_order();
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, set_entity_enabled_command>)
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.", concrete.entity_id.to_string());
            }
            target->enabled = concrete.enabled;
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, upsert_component_command>)
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr || !core::uuid::parse(concrete.component.type_id)
                || concrete.component.qualified_name.empty() || concrete.component.schema_version == 0)
            {
                return failure("DPE.SCENE.INVALID_COMPONENT", "Entity or component identity was invalid.");
            }
            const auto existing = std::find_if(target->components.begin(), target->components.end(), [&](const auto& component) {
                return component.type_id == concrete.component.type_id;
            });
            if (existing == target->components.end())
            {
                target->components.push_back(concrete.component);
            }
            else
            {
                *existing = concrete.component;
            }
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, remove_component_command>)
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.");
            }
            const auto old_size = target->components.size();
            std::erase_if(target->components, [&](const auto& component) { return component.type_id == concrete.type_id; });
            if (target->components.size() == old_size)
            {
                return failure("DPE.SCENE.MISSING_COMPONENT", "Component does not exist.", concrete.type_id);
            }
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, reorder_component_command>)
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.");
            }
            const auto component = std::find_if(target->components.begin(), target->components.end(), [&](const auto& item) {
                return item.type_id == concrete.type_id;
            });
            if (component == target->components.end() || concrete.destination_index >= target->components.size())
            {
                return failure("DPE.SCENE.INVALID_COMPONENT_ORDER", "Component or destination index was invalid.", concrete.type_id);
            }
            const auto source_index = static_cast<std::size_t>(std::distance(target->components.begin(), component));
            if (source_index == concrete.destination_index)
            {
                return {true, std::nullopt};
            }
            auto moved = std::move(*component);
            target->components.erase(target->components.begin() + static_cast<std::ptrdiff_t>(source_index));
            target->components.insert(
                target->components.begin() + static_cast<std::ptrdiff_t>(concrete.destination_index),
                std::move(moved));
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, set_component_enabled_command>)
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.");
            }
            const auto component = std::find_if(target->components.begin(), target->components.end(), [&](const auto& item) {
                return item.type_id == concrete.type_id;
            });
            if (component == target->components.end() || component->opaque)
            {
                return failure("DPE.SCENE.OPAQUE_OR_MISSING_COMPONENT", "Opaque or missing components cannot be edited.");
            }
            component->enabled = concrete.enabled;
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, set_component_property_path_command>)
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.");
            }
            const auto component = std::find_if(target->components.begin(), target->components.end(), [&](const auto& item) {
                return item.type_id == concrete.type_id;
            });
            if (component == target->components.end() || component->opaque)
            {
                return failure("DPE.SCENE.OPAQUE_OR_MISSING_COMPONENT", "Opaque or missing components cannot be edited.");
            }
            const auto property = component->properties.find(concrete.property_id);
            if (property == component->properties.end()
                || !set_json_path(*property, concrete.path, concrete.value))
            {
                return failure("DPE.SCENE.INVALID_PROPERTY_PATH", "Nested property path could not be applied.");
            }
            return {true, std::nullopt};
        }
        else if constexpr (std::is_same_v<command_type, set_prefab_instances_command>)
        {
            if (!concrete.instances.is_array())
            {
                return failure(
                    "DPE.SCENE.INVALID_PREFAB_INSTANCES",
                    "Prefab instance records must be represented by a JSON array.");
            }
            prefab_instances_ = concrete.instances;
            return {true, std::nullopt};
        }
        else
        {
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.");
            }
            const auto component = std::find_if(target->components.begin(), target->components.end(), [&](const auto& item) {
                return item.type_id == concrete.type_id;
            });
            if (component == target->components.end() || component->opaque)
            {
                return failure("DPE.SCENE.OPAQUE_OR_MISSING_COMPONENT", "Opaque or missing components cannot be edited.");
            }
            component->properties[concrete.property_id] = concrete.value;
            return {true, std::nullopt};
        }
    }, value);
}

transaction_result scene::apply_transaction_untracked(std::span<const command> commands)
{
    const auto checkpoint = capture_state();
    std::size_t applied_count = 0;
    for (const auto& item : commands)
    {
        auto result = apply_untracked(item);
        if (!result.succeeded)
        {
            restore_state(checkpoint);
            return {false, applied_count, std::move(result.diagnostic)};
        }
        ++applied_count;
    }
    return {true, applied_count, std::nullopt};
}

std::optional<core::diagnostic> scene::validate() const
{
    if (id_.is_nil())
    {
        return core::diagnostic{core::diagnostic_severity::error, "DPE.SCENE.NIL_ID", "Scene ID cannot be nil.", {}};
    }
    if (name_.empty())
    {
        return core::diagnostic{core::diagnostic_severity::error, "DPE.SCENE.EMPTY_NAME", "Scene name cannot be empty.", {}};
    }
    if (!std::isfinite(physics_settings_.fixed_time_step_seconds)
        || physics_settings_.fixed_time_step_seconds <= 0.0
        || physics_settings_.max_catch_up_ticks == 0
        || physics_settings_.box2d_solver_substeps == 0
        || physics_settings_.jolt_collision_steps == 0
        || !std::isfinite(physics_settings_.gravity_2d.x)
        || !std::isfinite(physics_settings_.gravity_2d.y)
        || !std::isfinite(physics_settings_.gravity_3d.x)
        || !std::isfinite(physics_settings_.gravity_3d.y)
        || !std::isfinite(physics_settings_.gravity_3d.z))
    {
        return core::diagnostic{
            core::diagnostic_severity::error,
            "DPE.SCENE.INVALID_PHYSICS_SETTINGS",
            "Physics settings require finite gravity, a positive fixed step, and positive step limits.",
            {},
        };
    }
    if (!prefab_instances_.is_array())
    {
        return core::diagnostic{
            core::diagnostic_severity::error,
            "DPE.SCENE.INVALID_PREFAB_INSTANCES",
            "Prefab instances must be an array.",
            {},
        };
    }

    std::unordered_set<core::uuid, core::uuid_hash> ids;
    std::unordered_map<std::string, std::vector<std::uint32_t>> sibling_orders;
    for (const auto& item : entities_)
    {
        if (item.id.is_nil() || item.name.empty() || !ids.insert(item.id).second)
        {
            return core::diagnostic{
                core::diagnostic_severity::error,
                "DPE.SCENE.INVALID_ENTITY_ID",
                "Entities require a non-nil unique ID and non-empty name.",
                item.id.to_string(),
            };
        }
        sibling_orders[hierarchy_key(item.parent_id)].push_back(item.sibling_order);
        std::unordered_set<std::string> component_ids;
        for (const auto& component : item.components)
        {
            if (!core::uuid::parse(component.type_id) || component.qualified_name.empty()
                || component.schema_version == 0 || !component_ids.insert(component.type_id).second)
            {
                return core::diagnostic{
                    core::diagnostic_severity::error,
                    "DPE.SCENE.INVALID_COMPONENT",
                    "Component identities must be valid and unique per entity.",
                    item.id.to_string(),
                };
            }
        }
    }
    for (const auto& item : entities_)
    {
        if (item.parent_id && (find_entity(*item.parent_id) == nullptr || would_create_cycle(item.id, *item.parent_id)))
        {
            return core::diagnostic{
                core::diagnostic_severity::error,
                "DPE.SCENE.INVALID_HIERARCHY",
                "Entity hierarchy contains a missing parent or cycle.",
                item.id.to_string(),
            };
        }
    }
    for (auto& [parent, orders] : sibling_orders)
    {
        std::sort(orders.begin(), orders.end());
        for (std::size_t index = 0; index < orders.size(); ++index)
        {
            if (orders[index] != index)
            {
                return core::diagnostic{
                    core::diagnostic_severity::error,
                    "DPE.SCENE.INVALID_SIBLING_ORDER",
                    "Sibling order must be unique and contiguous for each parent.",
                    parent,
                };
            }
        }
    }
    return std::nullopt;
}

bool scene::would_create_cycle(const core::uuid& entity_id, const core::uuid& parent_id) const noexcept
{
    auto current = &parent_id;
    std::size_t visited = 0;
    while (current != nullptr && visited <= entities_.size())
    {
        if (*current == entity_id)
        {
            return true;
        }
        const auto* parent = find_entity(*current);
        if (parent == nullptr || !parent->parent_id)
        {
            return false;
        }
        current = &*parent->parent_id;
        ++visited;
    }
    return visited > entities_.size();
}

scene::state_snapshot scene::capture_state() const
{
    return {entities_, physics_settings_, prefab_instances_};
}

void scene::restore_state(state_snapshot state)
{
    entities_ = std::move(state.entities);
    physics_settings_ = state.physics;
    prefab_instances_ = std::move(state.prefab_instances);
}

void scene::record_history(state_snapshot before, std::string description)
{
    auto after = capture_state();
    if (before == after)
    {
        return;
    }
    if (history_position_ < history_.size())
    {
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(history_position_), history_.end());
        if (savepoint_position_ && *savepoint_position_ > history_position_)
        {
            savepoint_position_.reset();
        }
    }
    history_.push_back({std::move(before), std::move(after), std::move(description)});
    ++history_position_;
}

void scene::derive_sibling_order_from_storage()
{
    std::unordered_map<std::string, std::uint32_t> next_orders;
    for (auto& item : entities_)
    {
        item.sibling_order = next_orders[hierarchy_key(item.parent_id)]++;
    }
}

void scene::compact_sibling_order()
{
    std::unordered_map<std::string, std::vector<entity*>> groups;
    for (auto& item : entities_)
    {
        groups[hierarchy_key(item.parent_id)].push_back(&item);
    }
    for (auto& [key, siblings] : groups)
    {
        static_cast<void>(key);
        std::sort(siblings.begin(), siblings.end(), [](const auto* left, const auto* right) {
            if (left->sibling_order != right->sibling_order)
            {
                return left->sibling_order < right->sibling_order;
            }
            return left->id < right->id;
        });
        for (std::size_t index = 0; index < siblings.size(); ++index)
        {
            siblings[index]->sibling_order = static_cast<std::uint32_t>(index);
        }
    }
}

std::size_t scene::sibling_count(
    const std::optional<core::uuid>& parent_id,
    const std::optional<core::uuid>& excluded) const noexcept
{
    return static_cast<std::size_t>(std::count_if(entities_.begin(), entities_.end(), [&](const auto& item) {
        return item.parent_id == parent_id && (!excluded || item.id != *excluded);
    }));
}

command_result scene::move_entity(
    const core::uuid& entity_id,
    const std::optional<core::uuid>& parent_id,
    std::size_t sibling_index)
{
    auto* target = find_entity_mutable(entity_id);
    if (target == nullptr)
    {
        return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.", entity_id.to_string());
    }
    if (parent_id && (find_entity(*parent_id) == nullptr || would_create_cycle(entity_id, *parent_id)))
    {
        return failure("DPE.SCENE.INVALID_PARENT", "Parent was missing or would create a hierarchy cycle.");
    }
    const auto destination_count = sibling_count(parent_id, entity_id);
    if (sibling_index > destination_count || sibling_index > std::numeric_limits<std::uint32_t>::max())
    {
        return failure("DPE.SCENE.INVALID_SIBLING_INDEX", "Sibling insertion index was out of range.");
    }

    const auto old_parent = target->parent_id;
    const auto old_order = target->sibling_order;
    for (auto& item : entities_)
    {
        if (item.id != entity_id && item.parent_id == old_parent && item.sibling_order > old_order)
        {
            --item.sibling_order;
        }
    }
    target = find_entity_mutable(entity_id);
    target->parent_id = parent_id;
    for (auto& item : entities_)
    {
        if (item.id != entity_id && item.parent_id == parent_id && item.sibling_order >= sibling_index)
        {
            ++item.sibling_order;
        }
    }
    target = find_entity_mutable(entity_id);
    target->sibling_order = static_cast<std::uint32_t>(sibling_index);
    compact_sibling_order();
    return {true, std::nullopt};
}

command_result scene::failure(std::string code, std::string message, std::string context)
{
    return {
        false,
        core::diagnostic{core::diagnostic_severity::error, std::move(code), std::move(message), std::move(context)},
    };
}
}
