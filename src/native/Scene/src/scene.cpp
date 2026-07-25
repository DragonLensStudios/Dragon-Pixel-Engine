#include <dragonpixel/scene/scene.h>

#include <algorithm>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace dragonpixel::scene
{
scene::scene(core::uuid id, std::string name) : id_(id), name_(std::move(name)) {}

scene::scene(core::uuid id, std::string name, std::vector<entity> entities)
    : id_(id), name_(std::move(name)), entities_(std::move(entities))
{
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

command_result scene::apply(const command& value)
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
            entities_.push_back(entity{concrete.entity_id, concrete.name, concrete.parent_id, {}});
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
            auto* target = find_entity_mutable(concrete.entity_id);
            if (target == nullptr)
            {
                return failure("DPE.SCENE.MISSING_ENTITY", "Entity does not exist.", concrete.entity_id.to_string());
            }
            if (concrete.parent_id && (find_entity(*concrete.parent_id) == nullptr
                                       || would_create_cycle(concrete.entity_id, *concrete.parent_id)))
            {
                return failure("DPE.SCENE.INVALID_PARENT", "Parent was missing or would create a hierarchy cycle.");
            }
            target->parent_id = concrete.parent_id;
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

transaction_result scene::apply_transaction(std::span<const command> commands)
{
    const auto checkpoint = entities_;
    std::size_t applied_count = 0;
    for (const auto& item : commands)
    {
        auto result = apply(item);
        if (!result.succeeded)
        {
            entities_ = checkpoint;
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
    std::unordered_set<core::uuid, core::uuid_hash> ids;
    for (const auto& item : entities_)
    {
        if (item.id.is_nil() || !ids.insert(item.id).second)
        {
            return core::diagnostic{
                core::diagnostic_severity::error,
                "DPE.SCENE.INVALID_ENTITY_ID",
                "Entity IDs must be non-nil and unique.",
                item.id.to_string(),
            };
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

command_result scene::failure(std::string code, std::string message, std::string context)
{
    return {
        false,
        core::diagnostic{core::diagnostic_severity::error, std::move(code), std::move(message), std::move(context)},
    };
}
}
