#include <dragonpixel/cabi/dpe_api_v1.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
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
constexpr uint64_t kCapabilities =
    DPE_CAPABILITY_STRUCTURED_ERRORS |
    DPE_CAPABILITY_TRACKED_ALLOCATOR |
    DPE_CAPABILITY_LIVE_COUNTS |
    DPE_CAPABILITY_NATIVE_COUNTER_COMPONENT |
    DPE_CAPABILITY_MANAGED_CALLBACK;

enum class HandleKind : uint64_t {
    Runtime = 1,
    World = 2,
    Entity = 3,
    Component = 4
};

struct RuntimeState {
    std::unordered_set<dpe_world_handle> worlds;
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
std::unordered_set<void*> g_allocations;
thread_local ErrorState g_last_error;
thread_local uint64_t g_correlation_counter = 1;

void clear_error() noexcept {
    g_last_error = {};
}

dpe_status set_error(dpe_status status, std::string message) noexcept {
    g_last_error.info.code = static_cast<int32_t>(status);
    g_last_error.info.subsystem = kSubsystemAbi;
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
    &invoke_managed_callback_impl};

} // namespace

extern "C" DPE_EXPORT dpe_status DPE_CALL dpe_get_api_v1(
    uint32_t requested_major,
    uint32_t requested_minor,
    dpe_api_v1* out_api,
    size_t out_api_size) {
    return guarded([&]() {
        if (out_api == nullptr || out_api_size < sizeof(dpe_api_v1)) {
            return set_error(DPE_STATUS_INVALID_ARGUMENT, "API output is null or smaller than dpe_api_v1");
        }
        if (requested_major != DPE_ABI_MAJOR || requested_minor > DPE_ABI_MINOR) {
            return set_error(DPE_STATUS_ABI_VERSION_UNSUPPORTED, "requested ABI version is unsupported");
        }
        *out_api = kApi;
        return DPE_STATUS_OK;
    });
}
