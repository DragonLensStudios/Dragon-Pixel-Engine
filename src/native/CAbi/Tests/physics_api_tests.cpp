#include <dragonpixel/cabi/dpe_api_v1.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "physics ABI test failure: " << message << '\n';
        std::exit(1);
    }
}

std::array<std::uint8_t, 16> id(std::uint8_t suffix)
{
    std::array<std::uint8_t, 16> value{
        0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, suffix,
    };
    return value;
}

dpe_physics_collider_v1 collider(bool sphere = false, bool sensor = false)
{
    dpe_physics_collider_v1 value{};
    value.struct_size = sizeof(value);
    value.shape = sphere ? DPE_PHYSICS_SHAPE_CIRCLE_OR_SPHERE : DPE_PHYSICS_SHAPE_BOX;
    value.sensor = sensor ? 1U : 0U;
    value.size[0] = sphere ? 0.5 : 1.0;
    value.size[1] = sphere ? 0.5 : 1.0;
    value.size[2] = sphere ? 0.5 : 1.0;
    value.density = 1.0;
    value.friction = 0.5;
    value.mask = 0xffff;
    return value;
}

dpe_physics_body_v1 body(
    std::uint8_t suffix,
    std::uint32_t dimension,
    std::uint32_t mode,
    double x,
    double y,
    double z,
    std::uint32_t collider_index)
{
    dpe_physics_body_v1 value{};
    value.struct_size = sizeof(value);
    value.dimension = dimension;
    value.mode = mode;
    const auto uuid = id(suffix);
    std::copy(uuid.begin(), uuid.end(), value.entity_uuid);
    value.position[0] = x;
    value.position[1] = y;
    value.position[2] = z;
    value.rotation[3] = 1.0;
    value.gravity_scale = 1.0;
    value.collider_start = collider_index;
    value.collider_count = 1;
    return value;
}

bool has_suffix(const std::uint8_t uuid[16], std::uint8_t suffix)
{
    return uuid[15] == suffix;
}

std::string last_error_message(const dpe_api_v1& api)
{
    dpe_error_info_v1 info{};
    size_t required{};
    const auto query = api.get_last_error(&info, nullptr, 0, &required);
    require(query == DPE_STATUS_BUFFER_TOO_SMALL && required > 0,
        "last-error message size query failed");
    std::string message(required, '\0');
    require(
        api.get_last_error(
            &info,
            reinterpret_cast<std::uint8_t*>(message.data()),
            message.size(),
            &required) == DPE_STATUS_OK,
        "last-error message copy failed");
    return message;
}

struct minor_zero_api
{
    std::array<std::byte, DPE_API_V1_MINOR_0_SIZE> bytes{};
    std::uint64_t sentinel{0x1badc0de12345678ULL};
};
}

int main()
{
    minor_zero_api old_storage;
    require(
        dpe_get_api_v1(1, 0, reinterpret_cast<dpe_api_v1*>(old_storage.bytes.data()), old_storage.bytes.size())
            == DPE_STATUS_OK,
        "minor-0 original-size negotiation failed");
    const auto* old_api = reinterpret_cast<const dpe_api_v1*>(old_storage.bytes.data());
    require(old_api->struct_size == DPE_API_V1_MINOR_0_SIZE, "minor-0 table reported the wrong size");
    require(old_api->abi_minor == 0, "minor-0 negotiation did not stay at minor 0");
    require((old_api->capabilities & DPE_CAPABILITY_PHYSICS_V1) == 0, "minor-0 table leaked the extension capability");
    require(old_storage.sentinel == 0x1badc0de12345678ULL, "minor-0 negotiation overran caller storage");

    dpe_api_v1 api{};
    require(dpe_get_api_v1(1, 1, &api, sizeof(api)) == DPE_STATUS_OK, "minor-1 API negotiation failed");
    require(api.struct_size == sizeof(api) && api.abi_minor == 1, "minor-1 table was incomplete");
    require((api.capabilities & DPE_CAPABILITY_PHYSICS_V1) != 0, "physics capability was missing");

    dpe_runtime_handle runtime{};
    require(api.create_runtime(&runtime) == DPE_STATUS_OK, "runtime creation failed");
    dpe_physics_api_v1 physics{};
    require(
        api.acquire_physics_api(runtime, 1, 1, &physics, sizeof(physics)) == DPE_STATUS_ABI_VERSION_UNSUPPORTED,
        "unsupported physics minor was accepted");
    require(api.acquire_physics_api(runtime, 1, 0, &physics, sizeof(physics)) == DPE_STATUS_OK, "physics API acquisition failed");
    require(physics.struct_size == sizeof(physics), "physics API table size mismatch");

    dpe_physics_world_settings_v1 settings{};
    settings.struct_size = sizeof(settings);
    settings.maximum_catch_up_ticks = 4;
    settings.box2d_solver_substeps = 4;
    settings.jolt_collision_steps = 1;
    settings.fixed_time_step_seconds = 1.0 / 60.0;
    settings.gravity_2d[1] = -9.81;
    settings.gravity_3d[1] = -9.81;

    dpe_physics_world_handle world{};
    require(physics.create_world(runtime, &settings, &world) == DPE_STATUS_OK, "physics world creation failed");

    std::vector<dpe_physics_collider_v1> colliders{
        collider(), collider(true), collider(), collider(true), collider(true, true), collider(true), collider(), collider(),
    };
    colliders[0].size[0] = 20.0;
    colliders[0].size[2] = 20.0;
    colliders[2].size[0] = 20.0;
    colliders[2].size[2] = 20.0;
    colliders[4].layer = 1;
    colliders[4].mask = static_cast<std::uint16_t>(1U << 2);
    colliders[5].layer = 2;
    colliders[5].mask = static_cast<std::uint16_t>(1U << 1);
    colliders[6].layer = 3;
    colliders[6].mask = 0;
    colliders[7].layer = 4;
    colliders[7].mask = 0;

    std::vector<dpe_physics_body_v1> bodies{
        body(1, DPE_PHYSICS_DIMENSION_2D, DPE_PHYSICS_BODY_STATIC, 0.0, -1.0, 3.0, 0),
        body(2, DPE_PHYSICS_DIMENSION_2D, DPE_PHYSICS_BODY_DYNAMIC, 0.0, 4.0, 7.0, 1),
        body(3, DPE_PHYSICS_DIMENSION_3D, DPE_PHYSICS_BODY_STATIC, 0.0, -1.0, 0.0, 2),
        body(4, DPE_PHYSICS_DIMENSION_3D, DPE_PHYSICS_BODY_DYNAMIC, 0.0, 4.0, 0.0, 3),
        body(5, DPE_PHYSICS_DIMENSION_2D, DPE_PHYSICS_BODY_STATIC, 25.0, 0.0, 0.0, 4),
        body(6, DPE_PHYSICS_DIMENSION_2D, DPE_PHYSICS_BODY_DYNAMIC, 25.0, 0.0, 0.0, 5),
        body(7, DPE_PHYSICS_DIMENSION_2D, DPE_PHYSICS_BODY_DYNAMIC, 35.0, 0.0, 0.0, 6),
        body(8, DPE_PHYSICS_DIMENSION_2D, DPE_PHYSICS_BODY_DYNAMIC, 35.0, 0.0, 0.0, 7),
    };
    bodies[1].flags = DPE_PHYSICS_BODY_CONTINUOUS_COLLISION;
    require(
        physics.rebuild(world, bodies.data(), bodies.size(), colliders.data(), colliders.size()) == DPE_STATUS_OK,
        "physics snapshot rebuild failed");

    auto unsupported_colliders = colliders;
    unsupported_colliders.insert(unsupported_colliders.begin() + 4, collider());
    auto unsupported_bodies = bodies;
    unsupported_bodies[3].collider_count = 2;
    for (std::size_t index = 4; index < unsupported_bodies.size(); ++index)
    {
        ++unsupported_bodies[index].collider_start;
    }
    require(
        physics.rebuild(
            world,
            unsupported_bodies.data(),
            unsupported_bodies.size(),
            unsupported_colliders.data(),
            unsupported_colliders.size()) == DPE_STATUS_INVALID_ARGUMENT,
        "multiple 3D colliders should be rejected by the C ABI");
    require(
        last_error_message(api).find("DPE.PHYSICS.UNSUPPORTED_3D_COLLIDER_COUNT") != std::string::npos,
        "multiple 3D colliders did not surface a structured diagnostic code");
    size_t retained_transform_count{};
    require(
        physics.copy_transforms(world, nullptr, 0, &retained_transform_count)
                == DPE_STATUS_BUFFER_TOO_SMALL
            && retained_transform_count == bodies.size(),
        "rejected C ABI rebuild did not retain the prior supported world");

    dpe_physics_step_result_v1 step{};
    step.struct_size = sizeof(step);
    for (int index = 0; index < 240; ++index)
    {
        step.struct_size = sizeof(step);
        require(physics.step(world, 1.0 / 60.0, &step) == DPE_STATUS_OK, "fixed physics step failed");
        require(step.ticks == 1, "fixed physics step did not execute exactly once");
    }

    size_t required{};
    require(
        physics.copy_transforms(world, nullptr, 0, &required) == DPE_STATUS_BUFFER_TOO_SMALL && required == bodies.size(),
        "transform size query failed");
    std::vector<dpe_physics_transform_v1> transforms(required);
    require(
        physics.copy_transforms(world, transforms.data(), transforms.size(), &required) == DPE_STATUS_OK,
        "transform copy failed");
    const auto body_2d = std::find_if(transforms.begin(), transforms.end(), [](const auto& value) {
        return has_suffix(value.entity_uuid, 2);
    });
    const auto body_3d = std::find_if(transforms.begin(), transforms.end(), [](const auto& value) {
        return has_suffix(value.entity_uuid, 4);
    });
    require(body_2d != transforms.end() && body_2d->position[1] > -0.1 && body_2d->position[1] < 0.2,
        "Box2D body did not drop and rest");
    require(std::abs(body_2d->position[2] - 7.0) < 0.0001, "2D-to-3D transform mapping lost authoring Z");
    require(body_3d != transforms.end() && body_3d->position[1] > -0.1 && body_3d->position[1] < 0.2,
        "Jolt Y-up body did not drop and rest");

    size_t contact_count{};
    auto contact_status = physics.drain_contacts(world, nullptr, 0, &contact_count);
    require(contact_status == DPE_STATUS_BUFFER_TOO_SMALL && contact_count > 0, "contact size query failed");
    std::vector<dpe_physics_contact_v1> contacts(contact_count);
    require(physics.drain_contacts(world, contacts.data(), contacts.size(), &contact_count) == DPE_STATUS_OK,
        "contact drain failed");
    require(std::any_of(contacts.begin(), contacts.end(), [](const auto& value) {
        return value.kind == DPE_PHYSICS_TRIGGER_BEGIN
            && ((has_suffix(value.entity_a_uuid, 5) && has_suffix(value.entity_b_uuid, 6))
                || (has_suffix(value.entity_a_uuid, 6) && has_suffix(value.entity_b_uuid, 5)));
    }), "sensor overlap did not emit a trigger");
    require(std::none_of(contacts.begin(), contacts.end(), [](const auto& value) {
        return (has_suffix(value.entity_a_uuid, 7) && has_suffix(value.entity_b_uuid, 8))
            || (has_suffix(value.entity_a_uuid, 8) && has_suffix(value.entity_b_uuid, 7));
    }), "masked bodies emitted a contact");

    dpe_physics_raycast_v1 ray{};
    ray.struct_size = sizeof(ray);
    ray.dimension = DPE_PHYSICS_DIMENSION_2D;
    ray.origin[1] = 8.0;
    ray.direction[1] = -2.0;
    ray.distance = 20.0;
    dpe_physics_raycast_hit_v1 hit{};
    hit.struct_size = sizeof(hit);
    require(physics.raycast(world, &ray, &hit) == DPE_STATUS_OK && hit.hit != 0, "2D raycast failed");
    ray.dimension = DPE_PHYSICS_DIMENSION_3D;
    hit.struct_size = sizeof(hit);
    require(physics.raycast(world, &ray, &hit) == DPE_STATUS_OK && hit.hit != 0, "3D raycast failed");
    require(std::abs(hit.normal[1]) > 0.5, "3D raycast did not return a surface normal");

    dpe_physics_command_v1 teleport{};
    teleport.struct_size = sizeof(teleport);
    teleport.kind = DPE_PHYSICS_COMMAND_TELEPORT;
    const auto dynamic_2d_id = id(2);
    std::copy(dynamic_2d_id.begin(), dynamic_2d_id.end(), teleport.entity_uuid);
    teleport.value[1] = 5.0;
    teleport.value[2] = 9.0;
    teleport.rotation[3] = 1.0;
    require(physics.apply_commands(world, &teleport, 1) == DPE_STATUS_OK, "teleport command failed");
    step.struct_size = sizeof(step);
    require(physics.step(world, 0.0, &step) == DPE_STATUS_OK, "zero-time transform refresh failed");
    transforms.resize(step.transform_count);
    require(physics.copy_transforms(world, transforms.data(), transforms.size(), &required) == DPE_STATUS_OK,
        "post-command transform copy failed");
    const auto teleported = std::find_if(transforms.begin(), transforms.end(), [](const auto& value) {
        return has_suffix(value.entity_uuid, 2);
    });
    require(teleported != transforms.end() && std::abs(teleported->position[1] - 5.0) < 0.001
        && std::abs(teleported->position[2] - 9.0) < 0.001, "teleport mapping failed");

    step.struct_size = sizeof(step);
    require(physics.step(world, 1.0, &step) == DPE_STATUS_OK, "bounded catch-up step failed");
    require(step.ticks == 4 && (step.flags & DPE_PHYSICS_STEP_DROPPED_TIME) != 0 && step.dropped_seconds > 0.8,
        "catch-up diagnostics were not returned");

    require(physics.destroy_world(world) == DPE_STATUS_OK, "physics world destroy failed");
    step.struct_size = sizeof(step);
    require(physics.step(world, 0.0, &step) == DPE_STATUS_INVALID_HANDLE, "stale physics handle remained valid");

    dpe_physics_world_handle cascade_world{};
    require(physics.create_world(runtime, &settings, &cascade_world) == DPE_STATUS_OK, "cascade world creation failed");
    require(api.destroy_runtime(runtime) == DPE_STATUS_OK, "runtime cleanup failed");
    require(physics.destroy_world(cascade_world) == DPE_STATUS_INVALID_HANDLE, "runtime did not clean up physics worlds");
    require(api.acquire_physics_api(runtime, 1, 0, &physics, sizeof(physics)) == DPE_STATUS_INVALID_HANDLE,
        "stale runtime acquired physics API");

    std::cout << "Dragon Pixel physics C ABI POC G passed\n";
    return 0;
}
