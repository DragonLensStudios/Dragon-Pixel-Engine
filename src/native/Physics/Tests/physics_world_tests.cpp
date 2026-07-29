#include <dragonpixel/physics/world.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace
{
using dragonpixel::physics::body_descriptor;
using dragonpixel::physics::body_dimension;
using dragonpixel::physics::body_mode;
using dragonpixel::physics::collider_descriptor;
using dragonpixel::physics::collider_shape;
using dragonpixel::physics::physics_world;
using dragonpixel::physics::physics_command;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "physics test failure: " << message << '\n';
        std::exit(1);
    }
}

body_descriptor ground_2d()
{
    body_descriptor body;
    body.entity_id = *dragonpixel::core::uuid::parse("10000000-0000-4000-8000-000000000001");
    body.dimension = body_dimension::two_d;
    body.mode = body_mode::static_body;
    body.position = {0.0, -1.0, 3.0};
    collider_descriptor collider;
    collider.shape = collider_shape::box;
    collider.size = {20.0, 1.0, 1.0};
    body.colliders.push_back(collider);
    return body;
}

body_descriptor falling_2d()
{
    body_descriptor body;
    body.entity_id = *dragonpixel::core::uuid::parse("10000000-0000-4000-8000-000000000002");
    body.dimension = body_dimension::two_d;
    body.mode = body_mode::dynamic;
    body.position = {0.0, 4.0, 7.0};
    collider_descriptor collider;
    collider.shape = collider_shape::circle_or_sphere;
    collider.size = {0.5, 0.5, 0.5};
    body.colliders.push_back(collider);
    return body;
}

body_descriptor ground_3d()
{
    body_descriptor body;
    body.entity_id = *dragonpixel::core::uuid::parse("10000000-0000-4000-8000-000000000003");
    body.dimension = body_dimension::three_d;
    body.mode = body_mode::static_body;
    body.position = {0.0, -1.0, 0.0};
    collider_descriptor collider;
    collider.shape = collider_shape::box;
    collider.size = {20.0, 1.0, 20.0};
    body.colliders.push_back(collider);
    return body;
}

body_descriptor falling_3d()
{
    body_descriptor body;
    body.entity_id = *dragonpixel::core::uuid::parse("10000000-0000-4000-8000-000000000004");
    body.dimension = body_dimension::three_d;
    body.mode = body_mode::dynamic;
    body.position = {0.0, 4.0, 0.0};
    collider_descriptor collider;
    collider.shape = collider_shape::circle_or_sphere;
    collider.size = {0.5, 0.5, 0.5};
    body.colliders.push_back(collider);
    return body;
}

dragonpixel::core::uuid test_id(std::uint8_t suffix)
{
    std::array<std::uint8_t, 16> bytes{
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, suffix,
    };
    return dragonpixel::core::uuid{bytes};
}

body_descriptor body(
    std::uint8_t suffix,
    body_dimension dimension,
    body_mode mode,
    dragonpixel::physics::vector3 position,
    collider_descriptor collider)
{
    body_descriptor value;
    value.entity_id = test_id(suffix);
    value.dimension = dimension;
    value.mode = mode;
    value.position = position;
    value.colliders.push_back(collider);
    return value;
}

const dragonpixel::physics::body_transform& transform_for(
    const dragonpixel::physics::step_result& result,
    const dragonpixel::core::uuid& entity_id)
{
    const auto found = std::find_if(result.transforms.begin(), result.transforms.end(), [&](const auto& transform) {
        return transform.entity_id == entity_id;
    });
    require(found != result.transforms.end(), "expected physics transform was missing");
    return *found;
}

bool contact_pair(
    const dragonpixel::physics::contact_event& event,
    const dragonpixel::core::uuid& first,
    const dragonpixel::core::uuid& second)
{
    return (event.entity_a == first && event.entity_b == second)
        || (event.entity_a == second && event.entity_b == first);
}
}

int main()
{
    static_assert(!noexcept(std::declval<physics_world&>().clear()),
        "clear must propagate allocation failures instead of terminating");
    physics_world world;
    const std::vector<body_descriptor> bodies{
        ground_2d(), falling_2d(), ground_3d(), falling_3d(),
    };
    const auto diagnostics = world.rebuild(bodies);
    require(diagnostics.empty(), "valid worlds should rebuild");
    require(world.body_count() == 4, "both backends should own their bodies");

    auto unsupported_bodies = bodies;
    auto second_3d_collider = unsupported_bodies[3].colliders.front();
    second_3d_collider.shape = collider_shape::box;
    unsupported_bodies[3].colliders.push_back(second_3d_collider);
    const auto unsupported_diagnostics = world.rebuild(unsupported_bodies);
    require(std::any_of(
        unsupported_diagnostics.begin(), unsupported_diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.code == "DPE.PHYSICS.UNSUPPORTED_3D_COLLIDER_COUNT";
        }), "multiple 3D colliders should return the SubShapeID semantics diagnostic");
    require(world.body_count() == 4,
        "rejected multiple-3D-collider rebuild must retain the supported world");
    require(world.advance(0.0).transforms.size() == 4,
        "rejected multiple-3D-collider rebuild must retain usable transforms");

    dragonpixel::physics::step_result latest;
    for (int index = 0; index < 240; ++index)
    {
        latest = world.advance(1.0 / 60.0);
        require(latest.ticks == 1, "fixed-step advancement should execute one tick");
    }
    require(world.tick() == 240, "world tick should be monotonic");

    double y2d = 100.0;
    double z2d = 0.0;
    double y3d = 100.0;
    for (const auto& transform : latest.transforms)
    {
        const auto id = transform.entity_id.to_string();
        if (id.ends_with("000000000002"))
        {
            y2d = transform.position.y;
            z2d = transform.position.z;
        }
        if (id.ends_with("000000000004"))
        {
            y3d = transform.position.y;
        }
    }
    require(y2d > -0.1 && y2d < 0.2, "Box2D body should drop and rest on the ground");
    require(std::abs(z2d - 7.0) < 0.0001, "2D transform mapping must preserve authoring Z");
    require(y3d > -0.1 && y3d < 0.2, "Jolt body should drop and rest on the ground");

    const auto hit2d = world.raycast_2d({0.0, 8.0}, {0.0, -1.0}, 20.0);
    const auto hit3d = world.raycast_3d({0.0, 8.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
    require(hit2d.hit, "2D raycast should hit a body");
    require(hit3d.hit, "3D raycast should hit a body");
    require(std::abs(hit3d.normal.y) > 0.5, "3D raycast should return a world-space surface normal");

    const std::array commands{
        physics_command{
            dragonpixel::physics::command_kind::teleport,
            falling_2d().entity_id,
            {0.0, 5.0, 9.0},
            {},
        },
        physics_command{
            dragonpixel::physics::command_kind::set_linear_velocity,
            falling_3d().entity_id,
            {1.0, 0.0, 0.0},
            {},
        },
    };
    require(world.apply_commands(commands).empty(), "valid command batch should apply");
    latest = world.advance(0.0);
    require(std::abs(transform_for(latest, falling_2d().entity_id).position.y - 5.0) < 0.001,
        "2D teleport should update Y");
    require(std::abs(transform_for(latest, falling_2d().entity_id).position.z - 9.0) < 0.001,
        "2D teleport should preserve the mapped authoring Z");
    latest = world.advance(1.0 / 60.0);
    require(transform_for(latest, falling_3d().entity_id).position.x > 0.01,
        "3D velocity command should affect the next step");

    const std::array invalid_commands{
        physics_command{
            dragonpixel::physics::command_kind::teleport,
            falling_2d().entity_id,
            {0.0, 100.0, 100.0},
            {},
        },
        physics_command{
            dragonpixel::physics::command_kind::force,
            test_id(0xff),
            {1.0, 0.0, 0.0},
            {},
        },
    };
    require(!world.apply_commands(invalid_commands).empty(), "invalid command batch should fail validation");
    latest = world.advance(0.0);
    require(transform_for(latest, falling_2d().entity_id).position.y < 10.0,
        "invalid command batch must not partially mutate the world");

    const auto catch_up = world.advance(1.0);
    require(catch_up.ticks == 4, "catch-up must be bounded");
    require(catch_up.dropped_seconds > 0.8, "excess accumulated time must be dropped");
    require(!catch_up.diagnostics.empty(), "catch-up drop must be diagnosed");

    world.clear();
    require(world.body_count() == 0, "clear must destroy both runtime worlds");

    physics_world polygon_world;
    collider_descriptor polygon_ground;
    polygon_ground.shape = collider_shape::polygon_2d;
    polygon_ground.vertices = {
        {-3.0, -0.5}, {3.0, -0.5}, {2.0, 0.5}, {-2.0, 0.5}};
    collider_descriptor polygon_faller;
    polygon_faller.shape = collider_shape::circle_or_sphere;
    polygon_faller.size = {0.5, 0.5, 0.5};
    const auto polygon_ground_id = test_id(40);
    const auto polygon_faller_id = test_id(41);
    const std::array polygon_bodies{
        body(40, body_dimension::two_d, body_mode::static_body,
            {0.0, 0.0, 0.0}, polygon_ground),
        body(41, body_dimension::two_d, body_mode::dynamic,
            {0.0, 4.0, 0.0}, polygon_faller),
    };
    require(polygon_world.rebuild(polygon_bodies).empty(),
        "valid convex 2D polygon worlds should rebuild");
    dragonpixel::physics::step_result polygon_step;
    bool polygon_contact{};
    for (int index = 0; index < 240; ++index)
    {
        polygon_step = polygon_world.advance(1.0 / 60.0);
        polygon_contact = polygon_contact || std::any_of(
            polygon_step.contacts.begin(), polygon_step.contacts.end(),
            [&](const auto& event) { return contact_pair(event, polygon_ground_id, polygon_faller_id); });
    }
    require(transform_for(polygon_step, polygon_faller_id).position.y > 0.8
            && transform_for(polygon_step, polygon_faller_id).position.y < 1.2,
        "Box2D body should rest on the neutral polygon collider");
    require(polygon_contact,
        "neutral polygon collider should emit Box2D contacts");
    auto invalid_polygon = polygon_ground;
    invalid_polygon.vertices = {{0.0, 0.0}, {1.0, 0.0}};
    const std::array invalid_polygon_bodies{body(42, body_dimension::two_d,
        body_mode::static_body, {}, invalid_polygon)};
    require(!polygon_world.rebuild(invalid_polygon_bodies).empty(),
        "polygon colliders with fewer than three vertices should be rejected");
    require(polygon_world.body_count() == 2,
        "rejected polygon rebuild must retain the last valid world");

    dragonpixel::physics::world_settings zero_gravity;
    zero_gravity.gravity_2d = {};
    zero_gravity.gravity_3d = {};
    physics_world filtered_world{zero_gravity};
    collider_descriptor sensor;
    sensor.shape = collider_shape::circle_or_sphere;
    sensor.size = {0.5, 0.5, 0.5};
    sensor.sensor = true;
    sensor.layer = 1;
    sensor.mask = static_cast<std::uint16_t>(1U << 2);
    collider_descriptor visitor = sensor;
    visitor.sensor = false;
    visitor.layer = 2;
    visitor.mask = static_cast<std::uint16_t>(1U << 1);
    collider_descriptor masked = visitor;
    masked.layer = 3;
    masked.mask = 0;
    collider_descriptor masked_other = visitor;
    masked_other.layer = 4;
    masked_other.mask = 0;
    const auto sensor_2d = test_id(10);
    const auto visitor_2d = test_id(11);
    const auto masked_2d_a = test_id(12);
    const auto masked_2d_b = test_id(13);
    const auto sensor_3d = test_id(14);
    const auto visitor_3d = test_id(15);
    const auto masked_3d_a = test_id(16);
    const auto masked_3d_b = test_id(17);
    auto kinematic_2d = body(30, body_dimension::two_d, body_mode::kinematic, {50.0, 0.0, 0.0}, visitor);
    kinematic_2d.linear_velocity = {1.0, 0.0, 0.0};
    auto kinematic_3d = body(31, body_dimension::three_d, body_mode::kinematic, {60.0, 0.0, 0.0}, visitor);
    kinematic_3d.linear_velocity = {1.0, 0.0, 0.0};
    const std::vector<body_descriptor> filtered_bodies{
        body(10, body_dimension::two_d, body_mode::static_body, {10.0, 0.0, 0.0}, sensor),
        body(11, body_dimension::two_d, body_mode::dynamic, {10.0, 0.0, 0.0}, visitor),
        body(12, body_dimension::two_d, body_mode::dynamic, {20.0, 0.0, 0.0}, masked),
        body(13, body_dimension::two_d, body_mode::dynamic, {20.0, 0.0, 0.0}, masked_other),
        body(14, body_dimension::three_d, body_mode::static_body, {30.0, 0.0, 0.0}, sensor),
        body(15, body_dimension::three_d, body_mode::dynamic, {30.0, 0.0, 0.0}, visitor),
        body(16, body_dimension::three_d, body_mode::dynamic, {40.0, 0.0, 0.0}, masked),
        body(17, body_dimension::three_d, body_mode::dynamic, {40.0, 0.0, 0.0}, masked_other),
        kinematic_2d,
        kinematic_3d,
    };
    require(filtered_world.rebuild(filtered_bodies).empty(), "filtered sensor worlds should rebuild");
    const auto filtered_step = filtered_world.advance(1.0 / 60.0);
    require(std::any_of(filtered_step.contacts.begin(), filtered_step.contacts.end(), [&](const auto& event) {
        return event.kind == dragonpixel::physics::contact_kind::begin_trigger
            && contact_pair(event, sensor_2d, visitor_2d);
    }), "Box2D sensor should emit a begin trigger");
    require(std::any_of(filtered_step.contacts.begin(), filtered_step.contacts.end(), [&](const auto& event) {
        return event.kind == dragonpixel::physics::contact_kind::begin_trigger
            && contact_pair(event, sensor_3d, visitor_3d);
    }), "Jolt sensor should emit a begin trigger");
    require(std::none_of(filtered_step.contacts.begin(), filtered_step.contacts.end(), [&](const auto& event) {
        return contact_pair(event, masked_2d_a, masked_2d_b) || contact_pair(event, masked_3d_a, masked_3d_b);
    }), "collision masks should suppress contacts in both backends");
    require(transform_for(filtered_step, test_id(30)).position.x > 50.0,
        "Box2D kinematic body should advance without gravity");
    require(transform_for(filtered_step, test_id(31)).position.x > 60.0,
        "Jolt kinematic body should advance without gravity");

    physics_world continuous_world{zero_gravity};
    collider_descriptor wall;
    wall.size = {0.1, 10.0, 10.0};
    collider_descriptor bullet;
    bullet.shape = collider_shape::circle_or_sphere;
    bullet.size = {0.25, 0.25, 0.25};
    auto bullet_2d = body(20, body_dimension::two_d, body_mode::dynamic, {0.0, 0.0, 0.0}, bullet);
    bullet_2d.linear_velocity = {300.0, 0.0, 0.0};
    bullet_2d.continuous_collision = true;
    auto bullet_3d = body(22, body_dimension::three_d, body_mode::dynamic, {0.0, 0.0, 0.0}, bullet);
    bullet_3d.linear_velocity = {300.0, 0.0, 0.0};
    bullet_3d.continuous_collision = true;
    const std::vector<body_descriptor> continuous_bodies{
        body(19, body_dimension::two_d, body_mode::static_body, {3.0, 0.0, 0.0}, wall),
        bullet_2d,
        body(21, body_dimension::three_d, body_mode::static_body, {3.0, 0.0, 0.0}, wall),
        bullet_3d,
    };
    require(continuous_world.rebuild(continuous_bodies).empty(), "continuous worlds should rebuild");
    const auto continuous_step = continuous_world.advance(1.0 / 60.0);
    require(transform_for(continuous_step, test_id(20)).position.x < 3.0,
        "Box2D continuous body should not tunnel through a thin wall");
    require(transform_for(continuous_step, test_id(22)).position.x < 3.0,
        "Jolt continuous body should not tunnel through a thin wall");

    physics_world replay_a;
    physics_world replay_b;
    require(replay_a.rebuild(bodies).empty() && replay_b.rebuild(bodies).empty(), "replay worlds should rebuild");
    dragonpixel::physics::step_result replay_result_a;
    dragonpixel::physics::step_result replay_result_b;
    for (int index = 0; index < 120; ++index)
    {
        replay_result_a = replay_a.advance(1.0 / 60.0);
        replay_result_b = replay_b.advance(1.0 / 60.0);
    }
    require(replay_result_a.transforms.size() == replay_result_b.transforms.size(), "replay transform counts differ");
    for (std::size_t index = 0; index < replay_result_a.transforms.size(); ++index)
    {
        const auto& first = replay_result_a.transforms[index];
        const auto& second = replay_result_b.transforms[index];
        require(first.entity_id == second.entity_id
            && std::abs(first.position.x - second.position.x) < 1e-9
            && std::abs(first.position.y - second.position.y) < 1e-9
            && std::abs(first.position.z - second.position.z) < 1e-9,
            "same-binary replay diverged");
    }

    std::cout << "Dragon Pixel physics POC G baseline passed\n";
    return 0;
}
