#pragma once

#include <dragonpixel/core/diagnostic.h>
#include <dragonpixel/core/uuid.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace dragonpixel::physics
{
struct vector2 final
{
    double x{};
    double y{};
};

struct vector3 final
{
    double x{};
    double y{};
    double z{};
};

struct quaternion final
{
    double x{};
    double y{};
    double z{};
    double w{1.0};
};

enum class body_dimension : std::uint8_t
{
    two_d,
    three_d,
};

enum class body_mode : std::uint8_t
{
    static_body,
    kinematic,
    dynamic,
};

enum class collider_shape : std::uint8_t
{
    box,
    circle_or_sphere,
};

struct collider_descriptor final
{
    collider_shape shape{collider_shape::box};
    vector3 size{1.0, 1.0, 1.0};
    vector3 offset{};
    bool sensor{};
    double density{1.0};
    double friction{0.5};
    double restitution{};
    std::uint16_t layer{};
    std::uint16_t mask{0xffff};
};

struct body_descriptor final
{
    core::uuid entity_id;
    body_dimension dimension{body_dimension::three_d};
    body_mode mode{body_mode::dynamic};
    vector3 position{};
    quaternion rotation{};
    vector3 linear_velocity{};
    vector3 angular_velocity{};
    double linear_damping{};
    double angular_damping{};
    double gravity_scale{1.0};
    bool continuous_collision{};
    std::vector<collider_descriptor> colliders;
};

struct world_settings final
{
    vector2 gravity_2d{0.0, -9.81};
    vector3 gravity_3d{0.0, -9.81, 0.0};
    double fixed_time_step_seconds{1.0 / 60.0};
    std::uint32_t maximum_catch_up_ticks{4};
    std::uint32_t box2d_solver_substeps{4};
    std::uint32_t jolt_collision_steps{1};
};

struct body_transform final
{
    core::uuid entity_id;
    vector3 position;
    quaternion rotation;
    vector3 linear_velocity;
};

enum class command_kind : std::uint8_t
{
    force,
    impulse,
    set_linear_velocity,
    set_angular_velocity,
    teleport,
};

struct physics_command final
{
    command_kind kind{command_kind::force};
    core::uuid entity_id;
    vector3 value{};
    quaternion rotation{};
};

enum class contact_kind : std::uint8_t
{
    begin_contact,
    end_contact,
    begin_trigger,
    end_trigger,
};

struct contact_event final
{
    std::uint64_t tick{};
    contact_kind kind{contact_kind::begin_contact};
    core::uuid entity_a;
    core::uuid entity_b;
    vector3 point{};
    vector3 normal{};
};

struct step_result final
{
    std::uint32_t ticks{};
    double dropped_seconds{};
    std::vector<body_transform> transforms;
    std::vector<contact_event> contacts;
    std::vector<core::diagnostic> diagnostics;
};

struct raycast_hit final
{
    bool hit{};
    core::uuid entity_id;
    double fraction{};
    vector3 point;
    vector3 normal;
};

class physics_world final
{
public:
    explicit physics_world(world_settings settings = {});
    ~physics_world();

    physics_world(const physics_world&) = delete;
    physics_world& operator=(const physics_world&) = delete;
    physics_world(physics_world&&) noexcept;
    physics_world& operator=(physics_world&&) noexcept;

    [[nodiscard]] std::vector<core::diagnostic> rebuild(std::span<const body_descriptor> bodies);
    [[nodiscard]] std::vector<core::diagnostic> apply_commands(std::span<const physics_command> commands);
    [[nodiscard]] step_result advance(double elapsed_seconds);
    [[nodiscard]] raycast_hit raycast_2d(vector2 origin, vector2 direction, double distance) const;
    [[nodiscard]] raycast_hit raycast_3d(vector3 origin, vector3 direction, double distance) const;
    void clear();

    [[nodiscard]] std::size_t body_count() const noexcept;
    [[nodiscard]] std::uint64_t tick() const noexcept;

private:
    class implementation;
    std::unique_ptr<implementation> implementation_;
};
}
