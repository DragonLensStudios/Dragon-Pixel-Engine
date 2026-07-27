#include <dragonpixel/cabi/dpe_api_v1.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "POC A native failure: " << message << '\n';
        std::exit(1);
    }
}

dpe_status DPE_CALL successful_callback(void*, int32_t* out_value) {
    *out_value = 42;
    return DPE_STATUS_OK;
}

dpe_status DPE_CALL failed_callback(void*, int32_t*) {
    return DPE_STATUS_CALLBACK_FAILURE;
}

} // namespace

int main() {
    dpe_api_v1 api{};
    require(
        dpe_get_api_v1(DPE_ABI_MAJOR + 1, 0, &api, sizeof(api)) == DPE_STATUS_ABI_VERSION_UNSUPPORTED,
        "major-version mismatch must fail");
    require(dpe_get_api_v1(DPE_ABI_MAJOR, DPE_ABI_MINOR, &api, sizeof(api)) == DPE_STATUS_OK, "API negotiation failed");
    require(api.struct_size == sizeof(dpe_api_v1), "API structure size mismatch");

    constexpr std::array<uint8_t, 16> uuid{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x01};
    const std::string name = "native-entity";

    dpe_runtime_handle runtime{};
    dpe_world_handle world{};
    dpe_entity_handle entity{};
    dpe_component_handle component{};
    require(api.create_runtime(&runtime) == DPE_STATUS_OK, "runtime creation failed");
    require(api.create_world(runtime, &world) == DPE_STATUS_OK, "world creation failed");
    require(
        api.create_entity(world, uuid.data(), {reinterpret_cast<const uint8_t*>(name.data()), name.size()}, &entity) == DPE_STATUS_OK,
        "entity creation failed");
    require(api.attach_counter_component(entity, 7, &component) == DPE_STATUS_OK, "component creation failed");

    int64_t counter{};
    require(api.get_counter_value(component, &counter) == DPE_STATUS_OK && counter == 7, "component read failed");
    require(api.set_counter_value(component, 9) == DPE_STATUS_OK, "component write failed");
    require(api.get_counter_value(component, &counter) == DPE_STATUS_OK && counter == 9, "component updated read failed");

    size_t required{};
    require(api.get_entity_name(entity, nullptr, 0, &required) == DPE_STATUS_BUFFER_TOO_SMALL, "name size query failed");
    std::vector<uint8_t> name_buffer(required);
    require(api.get_entity_name(entity, name_buffer.data(), name_buffer.size(), &required) == DPE_STATUS_OK, "name copy failed");
    require(std::string(name_buffer.begin(), name_buffer.end()) == name, "name round-trip mismatch");

    void* allocation{};
    require(api.allocate(64, &allocation) == DPE_STATUS_OK && allocation != nullptr, "tracked allocation failed");
    require(api.deallocate(allocation) == DPE_STATUS_OK, "tracked deallocation failed");
    require(api.deallocate(allocation) == DPE_STATUS_INVALID_ARGUMENT, "double-free detection failed");

    require(api.force_native_exception() == DPE_STATUS_INTERNAL_ERROR, "native exception did not become a status");
    dpe_error_info_v1 error{};
    require(api.get_last_error(&error, nullptr, 0, &required) == DPE_STATUS_BUFFER_TOO_SMALL, "error size query failed");
    std::vector<uint8_t> error_buffer(required);
    require(api.get_last_error(&error, error_buffer.data(), error_buffer.size(), &required) == DPE_STATUS_OK, "error copy failed");
    require(!error_buffer.empty() && error.code == DPE_STATUS_INTERNAL_ERROR, "structured error was not retained");

    int32_t callback_value{};
    require(api.invoke_managed_callback(&successful_callback, nullptr, &callback_value) == DPE_STATUS_OK, "successful callback failed");
    require(callback_value == 42, "successful callback value mismatch");
    require(api.invoke_managed_callback(&failed_callback, nullptr, &callback_value) == DPE_STATUS_CALLBACK_FAILURE, "callback failure escaped boundary");

    require(api.destroy_component(component) == DPE_STATUS_OK, "component destroy failed");
    require(api.get_counter_value(component, &counter) == DPE_STATUS_INVALID_HANDLE, "stale component handle remained valid");
    require(api.destroy_entity(entity) == DPE_STATUS_OK, "entity destroy failed");
    require(api.destroy_world(world) == DPE_STATUS_OK, "world destroy failed");
    require(api.destroy_runtime(runtime) == DPE_STATUS_OK, "runtime destroy failed");

    dpe_live_counts_v1 counts{};
    require(api.get_live_counts(&counts) == DPE_STATUS_OK, "live-count query failed");
    require(counts.runtimes == 0 && counts.worlds == 0 && counts.entities == 0 && counts.components == 0 && counts.allocations == 0, "native state leaked");

    std::cout << "POC A native ABI tests passed\n";
    return 0;
}
