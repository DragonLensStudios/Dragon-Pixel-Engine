#include <dragonpixel/cabi/dpe_api_v1.h>
#include <dragonpixel/physics/world.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kSubsystemAbi = 1;
constexpr uint32_t kSubsystemPhysics = 2;
constexpr uint64_t kCapabilities =
    DPE_CAPABILITY_STRUCTURED_ERRORS |
    DPE_CAPABILITY_TRACKED_ALLOCATOR |
    DPE_CAPABILITY_LIVE_COUNTS |
    DPE_CAPABILITY_NATIVE_COUNTER_COMPONENT |
    DPE_CAPABILITY_MANAGED_CALLBACK |
    DPE_CAPABILITY_PHYSICS_V1;

enum class HandleKind : uint64_t {
    Runtime = 1,
    World = 2,
    Entity = 3,
    Component = 4,
    PhysicsWorld = 5
};

struct RuntimeState {
    std::unordered_set<dpe_world_handle> worlds;
    std::unordered_set<dpe_physics_world_handle> physics_worlds;
};

struct WorldState {
    dpe_runtime_handle runtime{};
    std::unordered_set<dpe_entity_handle> entities;
};

struct EntityState {
    dpe_world_handle world{};
    std::array<uint8_t, 16> uuid{};
    std::string name;
    std::unordered_set<dpe_component_handle> components;
};

struct ComponentState {
    dpe_entity_handle entity{};
    int64_t value{};
};

struct PhysicsWorldState {
    dpe_runtime_handle runtime{};
    dragonpixel::physics::world_settings settings;
    std::unique_ptr<dragonpixel::physics::physics_world> world;
    std::vector<dragonpixel::physics::body_transform> transforms;
    std::vector<dragonpixel::physics::contact_event> contacts;
};

struct ErrorState {
    dpe_error_info_v1 info{};
    std::string message;
};

std::mutex g_mutex;
uint64_t g_next_handle = 1;
std::unordered_map<dpe_runtime_handle, RuntimeState> g_runtimes;
std::unordered_map<dpe_world_handle, WorldState> g_worlds;
std::unordered_map<dpe_entity_handle, EntityState> g_entities;
std::unordered_map<dpe_component_handle, ComponentState> g_components;
std::unordered_map<dpe_physics_world_handle, PhysicsWorldState> g_physics_worlds;
std::unordered_set<void*> g_allocations;
thread_local ErrorState g_last_error;
thread_local uint64_t g_correlation_counter = 1;

void clear_error() noexcept {
    g_last_error = {};
}

dpe_status set_error(dpe_status status, std::string message, uint32_t subsystem = kSubsystemAbi) noexcept {
    g_last_error.info.code = static_cast<int32_t>(status);
    g_last_error.info.subsystem = subsystem;
    std::fill(std::begin(g_last_error.info.correlation_id), std::end(g_last_error.info.correlation_id), uint8_t{0});
    const uint64_t correlation = g_correlation_counter++;
    std::memcpy(g_last_error.info.correlation_id, &correlation, sizeof(correlation));
    g_last_error.message = std::move(message);
    return status;
}

template <typename Function>
dpe_status guarded(Function&& function) noexcept {
    try {
        clear_error();
        return function();
    } catch (const std::invalid_argument& exception) {
        return set_error(DPE_STATUS_INVALID_ARGUMENT, std::string("invalid native argument: ") + exception.what());
    } catch (const std::bad_alloc&) {
        return set_error(DPE_STATUS_OUT_OF_MEMORY, "native allocation failed");
    } catch (const std::exception& exception) {
        return set_error(DPE_STATUS_INTERNAL_ERROR, std::string("native exception: ") + exception.what());
    } catch (...) {
        return set_error(DPE_STATUS_INTERNAL_ERROR, "unknown native exception");
    }
}

uint64_t make_handle(HandleKind kind) {
    constexpr uint64_t kValueMask = (UINT64_C(1) << 56) - 1;
    if (g_next_handle > kValueMask) {
        throw std::overflow_error("handle space exhausted");
    }
    return (static_cast<uint64_t>(kind) << 56) | g_next_handle++;
}

bool is_valid_utf8(const uint8_t* data, size_t length) noexcept {
    if (length == 0) {
        return true;
    }
    if (data == nullptr) {
        return false;
    }

    size_t index = 0;
    while (index < length) {
        const uint8_t first = data[index++];
        if (first <= 0x7F) {
            continue;
        }

        uint32_t code_point = 0;
        size_t continuation_count = 0;
        if ((first & 0xE0) == 0xC0) {
            code_point = first & 0x1F;
            continuation_count = 1;
            if (code_point == 0) {
                return false;
            }
        } else if ((first & 0xF0) == 0xE0) {
            code_point = first & 0x0F;
            continuation_count = 2;
        } else if ((first & 0xF8) == 0xF0) {
            code_point = first & 0x07;
            continuation_count = 3;
        } else {
            return false;
        }

        if (index + continuation_count > length) {
            return false;
        }
        for (size_t offset = 0; offset < continuation_count; ++offset) {
            const uint8_t continuation = data[index++];
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }

        if ((continuation_count == 1 && code_point < 0x80) ||
            (continuation_count == 2 && code_point < 0x800) ||
            (continuation_count == 3 && code_point < 0x10000) ||
            code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
    }
    return true;
}

dragonpixel::core::uuid physics_uuid(const uint8_t bytes[16]) {
    std::array<uint8_t, 16> value{};
    std::copy_n(bytes, value.size(), value.begin());
    return dragonpixel::core::uuid{value};
}

void copy_physics_uuid(const dragonpixel::core::uuid& source, uint8_t destination[16]) {
    std::copy(source.bytes().begin(), source.bytes().end(), destination);
}

dpe_status physics_diagnostics_status(const std::vector<dragonpixel::core::diagnostic>& diagnostics) {
    if (diagnostics.empty()) {
        return DPE_STATUS_OK;
    }
    const auto& diagnostic = diagnostics.front();
    auto message = diagnostic.code + ": " + diagnostic.message;
    if (!diagnostic.context.empty()) {
        message += " [" + diagnostic.context + "]";
    }
    return set_error(DPE_STATUS_INVALID_ARGUMENT, std::move(message), kSubsystemPhysics);
}

dragonpixel::physics::vector3 physics_vector3(const double value[3]) {
    return {value[0], value[1], value[2]};
}

dragonpixel::physics::quaternion physics_quaternion(const double value[4]) {
    return {value[0], value[1], value[2], value[3]};
}

void copy_vector3(const dragonpixel::physics::vector3& source, double destination[3]) {
    destination[0] = source.x;
    destination[1] = source.y;
    destination[2] = source.z;
}

void copy_quaternion(const dragonpixel::physics::quaternion& source, double destination[4]) {
    destination[0] = source.x;
    destination[1] = source.y;
    destination[2] = source.z;
    destination[3] = source.w;
}

void destroy_component_locked(dpe_component_handle component) {
    const auto component_it = g_components.find(component);
    if (component_it == g_components.end()) {
        return;
    }
    const auto entity_it = g_entities.find(component_it->second.entity);
    if (entity_it != g_entities.end()) {
        entity_it->second.components.erase(component);
    }
    g_components.erase(component_it);
}

void destroy_entity_locked(dpe_entity_handle entity) {
    const auto entity_it = g_entities.find(entity);
    if (entity_it == g_entities.end()) {
        return;
    }
    const std::vector<dpe_component_handle> components(entity_it->second.components.begin(), entity_it->second.components.end());
    for (const auto component : components) {
        destroy_component_locked(component);
    }
    const auto world_it = g_worlds.find(entity_it->second.world);
    if (world_it != g_worlds.end()) {
        world_it->second.entities.erase(entity);
    }
    g_entities.erase(entity_it);
}

void destroy_world_locked(dpe_world_handle world) {
    const auto world_it = g_worlds.find(world);
    if (world_it == g_worlds.end()) {
        return;
    }
    const std::vector<dpe_entity_handle> entities(world_it->second.entities.begin(), world_it->second.entities.end());
    for (const auto entity : entities) {
        destroy_entity_locked(entity);
    }
    const auto runtime_it = g_runtimes.find(world_it->second.runtime);
    if (runtime_it != g_runtimes.end()) {
        runtime_it->second.worlds.erase(world);
    }
    g_worlds.erase(world_it);
}

void destroy_physics_world_locked(dpe_physics_world_handle world) {
    const auto world_it = g_physics_worlds.find(world);
    if (world_it == g_physics_worlds.end()) {
        return;
    }
    const auto runtime_it = g_runtimes.find(world_it->second.runtime);
    if (runtime_it != g_runtimes.end()) {
        runtime_it->second.physics_worlds.erase(world);
    }
    g_physics_worlds.erase(world_it);
}

dpe_status DPE_CALL create_runtime_impl(dpe_runtime_handle* out_runtime) {
    return guarded([&]() {
        if (out_runtime == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "out_runtime is null");
        }
        std::scoped_lock lock(g_mutex);
        const auto handle = make_handle(HandleKind::Runtime);
        g_runtimes.emplace(handle, RuntimeState{});
        *out_runtime = handle;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL destroy_runtime_impl(dpe_runtime_handle runtime) {
    return guarded([&]() {
        std::scoped_lock lock(g_mutex);
        const auto runtime_it = g_runtimes.find(runtime);
        if (runtime_it == g_runtimes.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "runtime handle is invalid or stale");
        }
        const std::vector<dpe_world_handle> worlds(runtime_it->second.worlds.begin(), runtime_it->second.worlds.end());
        for (const auto world : worlds) {
            destroy_world_locked(world);
        }
        const std::vector<dpe_physics_world_handle> physics_worlds(
            runtime_it->second.physics_worlds.begin(),
            runtime_it->second.physics_worlds.end());
        for (const auto world : physics_worlds) {
            destroy_physics_world_locked(world);
        }
        g_runtimes.erase(runtime);
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL create_world_impl(dpe_runtime_handle runtime, dpe_world_handle* out_world) {
    return guarded([&]() {
        if (out_world == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "out_world is null");
        }
        std::scoped_lock lock(g_mutex);
        const auto runtime_it = g_runtimes.find(runtime);
        if (runtime_it == g_runtimes.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "runtime handle is invalid or stale");
        }
        const auto handle = make_handle(HandleKind::World);
        g_worlds.emplace(handle, WorldState{runtime, {}});
        try {
            runtime_it->second.worlds.insert(handle);
        } catch (...) {
            g_worlds.erase(handle);
            throw;
        }
        *out_world = handle;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL destroy_world_impl(dpe_world_handle world) {
    return guarded([&]() {
        std::scoped_lock lock(g_mutex);
        if (!g_worlds.contains(world)) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "world handle is invalid or stale");
        }
        destroy_world_locked(world);
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL create_entity_impl(
    dpe_world_handle world,
    const uint8_t uuid[16],
    dpe_utf8_view name,
    dpe_entity_handle* out_entity) {
    return guarded([&]() {
        if (uuid == nullptr || out_entity == nullptr || (name.length > 0 && name.data == nullptr)) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "entity input or output pointer is null");
        }
        if (!is_valid_utf8(name.data, name.length)) {
            return set_error(DPE_STATUS_INVALID_UTF8, "entity name is not valid UTF-8");
        }
        std::scoped_lock lock(g_mutex);
        const auto world_it = g_worlds.find(world);
        if (world_it == g_worlds.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "world handle is invalid or stale");
        }
        EntityState state;
        state.world = world;
        std::copy_n(uuid, state.uuid.size(), state.uuid.begin());
        state.name.assign(reinterpret_cast<const char*>(name.data), name.length);
        const auto handle = make_handle(HandleKind::Entity);
        g_entities.emplace(handle, std::move(state));
        try {
            world_it->second.entities.insert(handle);
        } catch (...) {
            g_entities.erase(handle);
            throw;
        }
        *out_entity = handle;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL destroy_entity_impl(dpe_entity_handle entity) {
    return guarded([&]() {
        std::scoped_lock lock(g_mutex);
        if (!g_entities.contains(entity)) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "entity handle is invalid or stale");
        }
        destroy_entity_locked(entity);
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL get_entity_name_impl(dpe_entity_handle entity, uint8_t* buffer, size_t capacity, size_t* out_required) {
    return guarded([&]() {
        if (out_required == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "out_required is null");
        }
        std::scoped_lock lock(g_mutex);
        const auto entity_it = g_entities.find(entity);
        if (entity_it == g_entities.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "entity handle is invalid or stale");
        }
        *out_required = entity_it->second.name.size();
        if (capacity < entity_it->second.name.size() || (capacity > 0 && buffer == nullptr)) {
            return DPE_STATUS_BUFFER_TOO_SMALL;
        }
        if (!entity_it->second.name.empty()) {
            std::memcpy(buffer, entity_it->second.name.data(), entity_it->second.name.size());
        }
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL attach_counter_component_impl(
    dpe_entity_handle entity,
    int64_t initial_value,
    dpe_component_handle* out_component) {
    return guarded([&]() {
        if (out_component == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "out_component is null");
        }
        std::scoped_lock lock(g_mutex);
        const auto entity_it = g_entities.find(entity);
        if (entity_it == g_entities.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "entity handle is invalid or stale");
        }
        const auto handle = make_handle(HandleKind::Component);
        g_components.emplace(handle, ComponentState{entity, initial_value});
        try {
            entity_it->second.components.insert(handle);
        } catch (...) {
            g_components.erase(handle);
            throw;
        }
        *out_component = handle;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL destroy_component_impl(dpe_component_handle component) {
    return guarded([&]() {
        std::scoped_lock lock(g_mutex);
        if (!g_components.contains(component)) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "component handle is invalid or stale");
        }
        destroy_component_locked(component);
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL get_counter_value_impl(dpe_component_handle component, int64_t* out_value) {
    return guarded([&]() {
        if (out_value == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "out_value is null");
        }
        std::scoped_lock lock(g_mutex);
        const auto component_it = g_components.find(component);
        if (component_it == g_components.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "component handle is invalid or stale");
        }
        *out_value = component_it->second.value;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL set_counter_value_impl(dpe_component_handle component, int64_t value) {
    return guarded([&]() {
        std::scoped_lock lock(g_mutex);
        const auto component_it = g_components.find(component);
        if (component_it == g_components.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "component handle is invalid or stale");
        }
        component_it->second.value = value;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL allocate_impl(size_t size, void** out_memory) {
    return guarded([&]() {
        if (size == 0 || out_memory == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "allocation size is zero or output is null");
        }
        void* memory = ::operator new(size);
        try {
            std::scoped_lock lock(g_mutex);
            g_allocations.insert(memory);
        } catch (...) {
            ::operator delete(memory);
            throw;
        }
        *out_memory = memory;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL deallocate_impl(void* memory) {
    return guarded([&]() {
        if (memory == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "allocation pointer is null");
        }
        {
            std::scoped_lock lock(g_mutex);
            if (g_allocations.erase(memory) == 0) {
                return set_error(DPE_STATUS_INVALID_ARGUMENT, "allocation pointer is foreign or already freed");
            }
        }
        ::operator delete(memory);
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL get_live_counts_impl(dpe_live_counts_v1* out_counts) {
    return guarded([&]() {
        if (out_counts == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "out_counts is null");
        }
        std::scoped_lock lock(g_mutex);
        *out_counts = {
            static_cast<uint64_t>(g_runtimes.size()),
            static_cast<uint64_t>(g_worlds.size()),
            static_cast<uint64_t>(g_entities.size()),
            static_cast<uint64_t>(g_components.size()),
            static_cast<uint64_t>(g_allocations.size())};
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL get_last_error_impl(
    dpe_error_info_v1* out_info,
    uint8_t* message_buffer,
    size_t capacity,
    size_t* out_required) {
    if (out_info == nullptr || out_required == nullptr) {
        return DPE_STATUS_INVALID_ARGUMENT;
    }
    *out_info = g_last_error.info;
    *out_required = g_last_error.message.size();
    if (capacity < g_last_error.message.size() || (capacity > 0 && message_buffer == nullptr)) {
        return DPE_STATUS_BUFFER_TOO_SMALL;
    }
    if (!g_last_error.message.empty()) {
        std::memcpy(message_buffer, g_last_error.message.data(), g_last_error.message.size());
    }
    return DPE_STATUS_OK;
}

dpe_status DPE_CALL force_native_exception_impl() {
    return guarded([]() -> dpe_status {
        throw std::runtime_error("forced failure for ABI verification");
    });
}

dpe_status DPE_CALL invoke_managed_callback_impl(
    dpe_managed_callback_v1 callback,
    void* context,
    int32_t* out_value) {
    return guarded([&]() {
        if (callback == nullptr || out_value == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "callback or out_value is null");
        }
        const dpe_status status = callback(context, out_value);
        if (status != DPE_STATUS_OK) {
            return set_error(DPE_STATUS_CALLBACK_FAILURE, "managed callback reported a contained failure");
        }
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL create_physics_world_impl(
    dpe_runtime_handle runtime,
    const dpe_physics_world_settings_v1* settings,
    dpe_physics_world_handle* out_world) {
    return guarded([&]() {
        if (out_world == nullptr || (settings != nullptr && settings->struct_size < sizeof(*settings))) {
            return set_error(
                DPE_STATUS_INVALID_ARGUMENT,
                "physics settings are undersized or out_world is null",
                kSubsystemPhysics);
        }

        dragonpixel::physics::world_settings native_settings;
        if (settings != nullptr) {
            native_settings.maximum_catch_up_ticks = settings->maximum_catch_up_ticks;
            native_settings.box2d_solver_substeps = settings->box2d_solver_substeps;
            native_settings.jolt_collision_steps = settings->jolt_collision_steps;
            native_settings.fixed_time_step_seconds = settings->fixed_time_step_seconds;
            native_settings.gravity_2d = {settings->gravity_2d[0], settings->gravity_2d[1]};
            native_settings.gravity_3d = physics_vector3(settings->gravity_3d);
        }

        auto native_world = std::make_unique<dragonpixel::physics::physics_world>(native_settings);
        std::scoped_lock lock(g_mutex);
        const auto runtime_it = g_runtimes.find(runtime);
        if (runtime_it == g_runtimes.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "runtime handle is invalid or stale", kSubsystemPhysics);
        }
        const auto handle = make_handle(HandleKind::PhysicsWorld);
        PhysicsWorldState state;
        state.runtime = runtime;
        state.settings = native_settings;
        state.world = std::move(native_world);
        g_physics_worlds.emplace(handle, std::move(state));
        try {
            runtime_it->second.physics_worlds.insert(handle);
        } catch (...) {
            g_physics_worlds.erase(handle);
            throw;
        }
        *out_world = handle;
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL destroy_physics_world_impl(dpe_physics_world_handle world) {
    return guarded([&]() {
        std::scoped_lock lock(g_mutex);
        if (!g_physics_worlds.contains(world)) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "physics world handle is invalid or stale", kSubsystemPhysics);
        }
        destroy_physics_world_locked(world);
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL rebuild_physics_world_impl(
    dpe_physics_world_handle world,
    const dpe_physics_body_v1* bodies,
    size_t body_count,
    const dpe_physics_collider_v1* colliders,
    size_t collider_count) {
    return guarded([&]() {
        if ((body_count > 0 && bodies == nullptr) || (collider_count > 0 && colliders == nullptr)) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics snapshot arrays are null", kSubsystemPhysics);
        }

        std::vector<dragonpixel::physics::body_descriptor> native_bodies;
        native_bodies.reserve(body_count);
        for (size_t body_index = 0; body_index < body_count; ++body_index) {
            const auto& input = bodies[body_index];
            if (input.struct_size < sizeof(input)
                || input.dimension > DPE_PHYSICS_DIMENSION_3D
                || input.mode > DPE_PHYSICS_BODY_DYNAMIC
                || (input.flags & ~static_cast<uint32_t>(DPE_PHYSICS_BODY_CONTINUOUS_COLLISION)) != 0) {
                return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics body record is incompatible", kSubsystemPhysics);
            }
            const size_t collider_start = input.collider_start;
            const size_t body_collider_count = input.collider_count;
            if (collider_start > collider_count || body_collider_count > collider_count - collider_start) {
                return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics collider range is outside the supplied array", kSubsystemPhysics);
            }

            dragonpixel::physics::body_descriptor body;
            body.entity_id = physics_uuid(input.entity_uuid);
            body.dimension = input.dimension == DPE_PHYSICS_DIMENSION_2D
                ? dragonpixel::physics::body_dimension::two_d
                : dragonpixel::physics::body_dimension::three_d;
            body.mode = static_cast<dragonpixel::physics::body_mode>(input.mode);
            body.position = physics_vector3(input.position);
            body.rotation = physics_quaternion(input.rotation);
            body.linear_velocity = physics_vector3(input.linear_velocity);
            body.angular_velocity = physics_vector3(input.angular_velocity);
            body.linear_damping = input.linear_damping;
            body.angular_damping = input.angular_damping;
            body.gravity_scale = input.gravity_scale;
            body.continuous_collision = (input.flags & DPE_PHYSICS_BODY_CONTINUOUS_COLLISION) != 0;
            body.colliders.reserve(body_collider_count);
            for (size_t collider_index = collider_start;
                 collider_index < collider_start + body_collider_count;
                 ++collider_index) {
                const auto& collider_input = colliders[collider_index];
                if (collider_input.struct_size < sizeof(collider_input)
                    || collider_input.shape > DPE_PHYSICS_SHAPE_CIRCLE_OR_SPHERE) {
                    return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics collider record is incompatible", kSubsystemPhysics);
                }
                dragonpixel::physics::collider_descriptor collider;
                collider.shape = collider_input.shape == DPE_PHYSICS_SHAPE_BOX
                    ? dragonpixel::physics::collider_shape::box
                    : dragonpixel::physics::collider_shape::circle_or_sphere;
                collider.size = physics_vector3(collider_input.size);
                collider.offset = physics_vector3(collider_input.offset);
                collider.sensor = collider_input.sensor != 0;
                collider.density = collider_input.density;
                collider.friction = collider_input.friction;
                collider.restitution = collider_input.restitution;
                collider.layer = collider_input.layer;
                collider.mask = collider_input.mask;
                body.colliders.push_back(collider);
            }
            native_bodies.push_back(std::move(body));
        }

        std::scoped_lock lock(g_mutex);
        const auto world_it = g_physics_worlds.find(world);
        if (world_it == g_physics_worlds.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "physics world handle is invalid or stale", kSubsystemPhysics);
        }
        const auto diagnostics = world_it->second.world->rebuild(native_bodies);
        const auto status = physics_diagnostics_status(diagnostics);
        if (status != DPE_STATUS_OK) {
            return status;
        }
        auto state = world_it->second.world->advance(0.0);
        world_it->second.transforms = std::move(state.transforms);
        world_it->second.contacts.clear();
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL apply_physics_commands_impl(
    dpe_physics_world_handle world,
    const dpe_physics_command_v1* commands,
    size_t command_count) {
    return guarded([&]() {
        if (command_count > 0 && commands == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics command array is null", kSubsystemPhysics);
        }
        std::vector<dragonpixel::physics::physics_command> native_commands;
        native_commands.reserve(command_count);
        for (size_t index = 0; index < command_count; ++index) {
            const auto& input = commands[index];
            if (input.struct_size < sizeof(input) || input.kind > DPE_PHYSICS_COMMAND_TELEPORT) {
                return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics command record is incompatible", kSubsystemPhysics);
            }
            dragonpixel::physics::physics_command command;
            command.kind = static_cast<dragonpixel::physics::command_kind>(input.kind);
            command.entity_id = physics_uuid(input.entity_uuid);
            command.value = physics_vector3(input.value);
            command.rotation = physics_quaternion(input.rotation);
            native_commands.push_back(command);
        }

        std::scoped_lock lock(g_mutex);
        const auto world_it = g_physics_worlds.find(world);
        if (world_it == g_physics_worlds.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "physics world handle is invalid or stale", kSubsystemPhysics);
        }
        return physics_diagnostics_status(world_it->second.world->apply_commands(native_commands));
    });
}

dpe_status DPE_CALL step_physics_world_impl(
    dpe_physics_world_handle world,
    double elapsed_seconds,
    dpe_physics_step_result_v1* out_result) {
    return guarded([&]() {
        if (out_result == nullptr || out_result->struct_size < sizeof(*out_result)) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics step result is null or undersized", kSubsystemPhysics);
        }
        std::scoped_lock lock(g_mutex);
        const auto world_it = g_physics_worlds.find(world);
        if (world_it == g_physics_worlds.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "physics world handle is invalid or stale", kSubsystemPhysics);
        }
        auto result = world_it->second.world->advance(elapsed_seconds);
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.severity == dragonpixel::core::diagnostic_severity::error) {
                return physics_diagnostics_status(result.diagnostics);
            }
        }
        world_it->second.transforms = std::move(result.transforms);
        world_it->second.contacts.insert(
            world_it->second.contacts.end(),
            result.contacts.begin(),
            result.contacts.end());
        *out_result = {
            sizeof(*out_result),
            result.ticks,
            result.dropped_seconds > 0.0 ? static_cast<uint32_t>(DPE_PHYSICS_STEP_DROPPED_TIME) : 0U,
            0,
            world_it->second.world->tick(),
            result.dropped_seconds,
            world_it->second.transforms.size(),
            world_it->second.contacts.size(),
        };
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL copy_physics_transforms_impl(
    dpe_physics_world_handle world,
    dpe_physics_transform_v1* buffer,
    size_t capacity,
    size_t* out_required) {
    return guarded([&]() {
        if (out_required == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics transform count output is null", kSubsystemPhysics);
        }
        std::scoped_lock lock(g_mutex);
        const auto world_it = g_physics_worlds.find(world);
        if (world_it == g_physics_worlds.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "physics world handle is invalid or stale", kSubsystemPhysics);
        }
        *out_required = world_it->second.transforms.size();
        if (capacity < *out_required || (*out_required > 0 && buffer == nullptr)) {
            return DPE_STATUS_BUFFER_TOO_SMALL;
        }
        for (size_t index = 0; index < *out_required; ++index) {
            const auto& source = world_it->second.transforms[index];
            auto& destination = buffer[index];
            destination = {};
            destination.struct_size = sizeof(destination);
            copy_physics_uuid(source.entity_id, destination.entity_uuid);
            copy_vector3(source.position, destination.position);
            copy_quaternion(source.rotation, destination.rotation);
            copy_vector3(source.linear_velocity, destination.linear_velocity);
        }
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL drain_physics_contacts_impl(
    dpe_physics_world_handle world,
    dpe_physics_contact_v1* buffer,
    size_t capacity,
    size_t* out_required) {
    return guarded([&]() {
        if (out_required == nullptr) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics contact count output is null", kSubsystemPhysics);
        }
        std::scoped_lock lock(g_mutex);
        const auto world_it = g_physics_worlds.find(world);
        if (world_it == g_physics_worlds.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "physics world handle is invalid or stale", kSubsystemPhysics);
        }
        *out_required = world_it->second.contacts.size();
        if (capacity < *out_required || (*out_required > 0 && buffer == nullptr)) {
            return DPE_STATUS_BUFFER_TOO_SMALL;
        }
        for (size_t index = 0; index < *out_required; ++index) {
            const auto& source = world_it->second.contacts[index];
            auto& destination = buffer[index];
            destination = {};
            destination.struct_size = sizeof(destination);
            destination.kind = static_cast<uint32_t>(source.kind);
            destination.tick = source.tick;
            copy_physics_uuid(source.entity_a, destination.entity_a_uuid);
            copy_physics_uuid(source.entity_b, destination.entity_b_uuid);
            copy_vector3(source.point, destination.point);
            copy_vector3(source.normal, destination.normal);
        }
        world_it->second.contacts.clear();
        return DPE_STATUS_OK;
    });
}

dpe_status DPE_CALL raycast_physics_world_impl(
    dpe_physics_world_handle world,
    const dpe_physics_raycast_v1* ray,
    dpe_physics_raycast_hit_v1* out_hit) {
    return guarded([&]() {
        if (ray == nullptr || out_hit == nullptr
            || ray->struct_size < sizeof(*ray) || out_hit->struct_size < sizeof(*out_hit)
            || ray->dimension > DPE_PHYSICS_DIMENSION_3D) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics raycast record is incompatible", kSubsystemPhysics);
        }
        std::scoped_lock lock(g_mutex);
        const auto world_it = g_physics_worlds.find(world);
        if (world_it == g_physics_worlds.end()) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "physics world handle is invalid or stale", kSubsystemPhysics);
        }
        dragonpixel::physics::raycast_hit hit;
        if (ray->dimension == DPE_PHYSICS_DIMENSION_2D) {
            hit = world_it->second.world->raycast_2d(
                {ray->origin[0], ray->origin[1]},
                {ray->direction[0], ray->direction[1]},
                ray->distance);
        } else {
            hit = world_it->second.world->raycast_3d(
                physics_vector3(ray->origin),
                physics_vector3(ray->direction),
                ray->distance);
        }
        *out_hit = {};
        out_hit->struct_size = sizeof(*out_hit);
        out_hit->hit = hit.hit ? 1U : 0U;
        if (hit.hit) {
            copy_physics_uuid(hit.entity_id, out_hit->entity_uuid);
            out_hit->fraction = hit.fraction;
            copy_vector3(hit.point, out_hit->point);
            copy_vector3(hit.normal, out_hit->normal);
        }
        return DPE_STATUS_OK;
    });
}

const dpe_physics_api_v1 kPhysicsApi = {
    sizeof(dpe_physics_api_v1),
    DPE_PHYSICS_ABI_MAJOR,
    DPE_PHYSICS_ABI_MINOR,
    0,
    &create_physics_world_impl,
    &destroy_physics_world_impl,
    &rebuild_physics_world_impl,
    &apply_physics_commands_impl,
    &step_physics_world_impl,
    &copy_physics_transforms_impl,
    &drain_physics_contacts_impl,
    &raycast_physics_world_impl,
};

dpe_status DPE_CALL acquire_physics_api_impl(
    dpe_runtime_handle runtime,
    uint32_t requested_major,
    uint32_t requested_minor,
    dpe_physics_api_v1* out_api,
    size_t out_api_size) {
    return guarded([&]() {
        if (out_api == nullptr || out_api_size < sizeof(dpe_physics_api_v1)) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "physics API output is null or undersized", kSubsystemPhysics);
        }
        if (requested_major != DPE_PHYSICS_ABI_MAJOR || requested_minor > DPE_PHYSICS_ABI_MINOR) {
            return set_error(DPE_STATUS_ABI_VERSION_UNSUPPORTED, "requested physics ABI is unsupported", kSubsystemPhysics);
        }
        std::scoped_lock lock(g_mutex);
        if (!g_runtimes.contains(runtime)) {
            return set_error(DPE_STATUS_INVALID_HANDLE, "runtime handle is invalid or stale", kSubsystemPhysics);
        }
        *out_api = kPhysicsApi;
        return DPE_STATUS_OK;
    });
}

const dpe_api_v1 kApi = {
    sizeof(dpe_api_v1),
    DPE_ABI_MAJOR,
    DPE_ABI_MINOR,
    0,
    kCapabilities,
    &create_runtime_impl,
    &destroy_runtime_impl,
    &create_world_impl,
    &destroy_world_impl,
    &create_entity_impl,
    &destroy_entity_impl,
    &get_entity_name_impl,
    &attach_counter_component_impl,
    &destroy_component_impl,
    &get_counter_value_impl,
    &set_counter_value_impl,
    &allocate_impl,
    &deallocate_impl,
    &get_live_counts_impl,
    &get_last_error_impl,
    &force_native_exception_impl,
    &invoke_managed_callback_impl,
    &acquire_physics_api_impl};

} // namespace

extern "C" DPE_EXPORT dpe_status DPE_CALL dpe_get_api_v1(
    uint32_t requested_major,
    uint32_t requested_minor,
    dpe_api_v1* out_api,
    size_t out_api_size) {
    return guarded([&]() {
        if (requested_major != DPE_ABI_MAJOR || requested_minor > DPE_ABI_MINOR) {
            return set_error(DPE_STATUS_ABI_VERSION_UNSUPPORTED, "requested ABI version is unsupported");
        }
        const size_t required_size = requested_minor == 0 ? DPE_API_V1_MINOR_0_SIZE : sizeof(dpe_api_v1);
        if (out_api == nullptr || out_api_size < required_size) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "API output is null or smaller than the requested minor version");
        }
        auto negotiated = kApi;
        negotiated.struct_size = static_cast<uint32_t>(required_size);
        negotiated.abi_minor = requested_minor;
        if (requested_minor == 0) {
            negotiated.capabilities &= ~static_cast<uint64_t>(DPE_CAPABILITY_PHYSICS_V1);
        }
        std::memcpy(out_api, &negotiated, required_size);
        return DPE_STATUS_OK;
    });
}
