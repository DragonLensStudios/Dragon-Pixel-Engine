#pragma once

#include <dragonpixel/core/diagnostic.h>
#include <dragonpixel/core/uuid.h>
#include <dragonpixel/scene/commands.h>
#include <dragonpixel/scene/entity.h>

#include <optional>
#include <span>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dragonpixel::scene
{
struct command_result final
{
    bool succeeded{};
    std::optional<core::diagnostic> diagnostic;
};

struct transaction_result final
{
    bool succeeded{};
    std::size_t applied_count{};
    std::optional<core::diagnostic> diagnostic;
};

struct physics_vector2 final
{
    double x{};
    double y{-9.81};

    friend bool operator==(const physics_vector2&, const physics_vector2&) = default;
};

struct physics_vector3 final
{
    double x{};
    double y{-9.81};
    double z{};

    friend bool operator==(const physics_vector3&, const physics_vector3&) = default;
};

struct scene_physics_settings final
{
    double fixed_time_step_seconds{1.0 / 60.0};
    std::uint32_t max_catch_up_ticks{4};
    std::uint32_t box2d_solver_substeps{4};
    std::uint32_t jolt_collision_steps{1};
    physics_vector2 gravity_2d;
    physics_vector3 gravity_3d;

    friend bool operator==(const scene_physics_settings&, const scene_physics_settings&) = default;
};

struct scene_document_extras final
{
    scene_physics_settings physics;
    nlohmann::ordered_json prefab_instances = nlohmann::ordered_json::array();
    bool has_explicit_sibling_order{};
};

class scene final
{
public:
    scene(core::uuid id, std::string name);
    scene(core::uuid id, std::string name, std::vector<entity> entities);
    scene(core::uuid id, std::string name, std::vector<entity> entities, scene_document_extras extras);

    [[nodiscard]] const core::uuid& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::span<const entity> entities() const noexcept { return entities_; }
    [[nodiscard]] const scene_physics_settings& physics_settings() const noexcept { return physics_settings_; }
    [[nodiscard]] const nlohmann::ordered_json& prefab_instances() const noexcept { return prefab_instances_; }
    [[nodiscard]] const entity* find_entity(const core::uuid& id) const noexcept;
    [[nodiscard]] command_result apply(const command& value, std::string description = {});
    [[nodiscard]] transaction_result apply_transaction(
        std::span<const command> commands,
        std::string description = {});
    [[nodiscard]] command_result dry_run(const command& value) const;
    [[nodiscard]] transaction_result dry_run_transaction(std::span<const command> commands) const;
    [[nodiscard]] bool can_undo() const noexcept { return history_position_ > 0; }
    [[nodiscard]] bool can_redo() const noexcept { return history_position_ < history_.size(); }
    [[nodiscard]] bool is_dirty() const noexcept
    {
        return !savepoint_position_ || *savepoint_position_ != history_position_;
    }
    [[nodiscard]] std::size_t history_size() const noexcept { return history_.size(); }
    [[nodiscard]] std::size_t history_position() const noexcept { return history_position_; }
    [[nodiscard]] command_result undo();
    [[nodiscard]] command_result redo();
    void mark_savepoint() noexcept { savepoint_position_ = history_position_; }
    [[nodiscard]] std::optional<core::diagnostic> validate() const;

private:
    struct state_snapshot final
    {
        std::vector<entity> entities;
        scene_physics_settings physics;
        nlohmann::ordered_json prefab_instances;

        friend bool operator==(const state_snapshot&, const state_snapshot&) = default;
    };

    struct history_entry final
    {
        state_snapshot before;
        state_snapshot after;
        std::string description;
    };

    [[nodiscard]] entity* find_entity_mutable(const core::uuid& id) noexcept;
    [[nodiscard]] bool would_create_cycle(const core::uuid& entity_id, const core::uuid& parent_id) const noexcept;
    [[nodiscard]] command_result apply_untracked(const command& value);
    [[nodiscard]] transaction_result apply_transaction_untracked(std::span<const command> commands);
    [[nodiscard]] state_snapshot capture_state() const;
    void restore_state(state_snapshot state);
    void record_history(state_snapshot before, std::string description);
    void derive_sibling_order_from_storage();
    void compact_sibling_order();
    [[nodiscard]] std::size_t sibling_count(
        const std::optional<core::uuid>& parent_id,
        const std::optional<core::uuid>& excluded = std::nullopt) const noexcept;
    [[nodiscard]] command_result move_entity(
        const core::uuid& entity_id,
        const std::optional<core::uuid>& parent_id,
        std::size_t sibling_index);
    [[nodiscard]] static command_result failure(std::string code, std::string message, std::string context = {});

    core::uuid id_;
    std::string name_;
    std::vector<entity> entities_;
    scene_physics_settings physics_settings_;
    nlohmann::ordered_json prefab_instances_ = nlohmann::ordered_json::array();
    std::vector<history_entry> history_;
    std::size_t history_position_{};
    std::optional<std::size_t> savepoint_position_{0};
};
}
