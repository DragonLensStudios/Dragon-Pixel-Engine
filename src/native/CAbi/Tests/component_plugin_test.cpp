#include <dragonpixel/cabi/component_plugin_v1.h>
#include <dragonpixel/cabi/tile_extension_plugin_v1.h>

#include <cstring>
#include <new>
#include <string>

namespace
{
constexpr auto type_id = "ce9c24d8-278e-457d-b733-3671c85f6a45";
constexpr auto tile_extension_id = "example.weather-tile";

dpe_tile_extension_string_v1 extension_string(std::string value)
{
    auto* bytes = new (std::nothrow) char[value.size()];
    if (bytes == nullptr) return {};
    std::memcpy(bytes, value.data(), value.size());
    return {bytes, value.size()};
}

int32_t DPE_TILE_EXTENSION_PLUGIN_CALL evaluate_tiles(
    const dpe_tile_extension_context_v1* contexts,
    size_t context_count,
    dpe_tile_extension_result_v1* results,
    size_t result_count)
{
    if (contexts == nullptr || results == nullptr || context_count == 0
        || context_count > DPE_TILE_EXTENSION_MAX_BATCH_V1 || result_count != context_count)
    {
        return -1;
    }
    for (size_t index = 0; index < context_count; ++index)
    {
        if (contexts[index].struct_size < sizeof(dpe_tile_extension_context_v1)) return -2;
        const std::string payload{contexts[index].payload_json.data,
            contexts[index].payload_json.length};
        auto json = payload.find("\"malformed\":true") != std::string::npos
            ? std::string{"{"}
            : std::string{"{\"tint\":{\"r\":0.25,\"g\":0.5,\"b\":0.75,\"a\":1.0}}"};
        results[index] = {static_cast<uint32_t>(sizeof(dpe_tile_extension_result_v1)), 0,
            extension_string(std::move(json)), {}, {}};
    }
    return 0;
}

int32_t DPE_TILE_EXTENSION_PLUGIN_CALL propose_brush(
    const dpe_tile_extension_context_v1* context,
    dpe_tile_extension_string_v1 request,
    dpe_tile_extension_result_v1* result)
{
    if (context == nullptr || result == nullptr
        || context->struct_size < sizeof(dpe_tile_extension_context_v1)) return -1;
    const std::string request_json{request.data, request.length};
    auto json = request_json.find("\"malformed\":true") != std::string::npos
        ? std::string{"[]"}
        : std::string{"{\"commands\":[{\"kind\":\"paint\",\"x\":"}
            + std::to_string(context->cell_x) + ",\"y\":"
            + std::to_string(context->cell_y) + ",\"tileSetId\":\""
            + std::string{context->tile_set_id.data, context->tile_set_id.length}
            + "\",\"tileId\":\""
            + std::string{context->tile_id.data, context->tile_id.length}
            + "\"}]}";
    *result = {static_cast<uint32_t>(sizeof(dpe_tile_extension_result_v1)), 0,
        extension_string(std::move(json)), {}, {}};
    return 0;
}

void DPE_TILE_EXTENSION_PLUGIN_CALL release_extension_string(dpe_tile_extension_string_v1 value)
{
    delete[] value.data;
}

struct test_component final
{
    std::string properties;
    std::string error;
    dpe_component_diagnostic_fn diagnostic{};
    void* diagnostic_user{};
    bool property_initialization_failed{};
    bool fail_late_update{};
};

dpe_component_handle DPE_COMPONENT_PLUGIN_CALL create(
    const char*,
    dpe_component_diagnostic_fn diagnostic,
    void* user)
{
    auto* value = new (std::nothrow) test_component;
    if (value != nullptr && diagnostic != nullptr)
    {
        value->diagnostic = diagnostic;
        value->diagnostic_user = user;
        constexpr char message[] = "Native runtime test component initialized.";
        diagnostic(user, 0, message, sizeof(message) - 1);
    }
    return value;
}

void DPE_COMPONENT_PLUGIN_CALL destroy(dpe_component_handle handle)
{
    auto* value = static_cast<test_component*>(handle);
    if (value != nullptr && value->diagnostic != nullptr)
    {
        if (value->property_initialization_failed)
        {
            constexpr char message[] =
                "Native runtime test component destroyed after set-properties failure.";
            value->diagnostic(value->diagnostic_user, 0, message, sizeof(message) - 1);
        }
        else
        {
            constexpr char message[] = "Native runtime test component destroyed.";
            value->diagnostic(value->diagnostic_user, 0, message, sizeof(message) - 1);
        }
    }
    delete value;
}

int32_t DPE_COMPONENT_PLUGIN_CALL set_properties(
    dpe_component_handle handle,
    const char* json,
    size_t length)
{
    if (handle == nullptr || json == nullptr) return -1;
    auto* value = static_cast<test_component*>(handle);
    value->properties.assign(json, length);
    if (value->properties.find("\"failSetProperties\":true") != std::string::npos)
    {
        value->property_initialization_failed = true;
        value->error = "injected native set-properties failure";
        return -1;
    }
    value->fail_late_update =
        value->properties.find("\"failLateUpdate\":true") != std::string::npos;
    return 0;
}

int32_t DPE_COMPONENT_PLUGIN_CALL update(
    dpe_component_handle handle,
    const dpe_component_update_v1* value)
{
    return handle == nullptr || value == nullptr ? -1 : 0;
}

void report(test_component* value, const char* message, size_t length)
{
    if (value != nullptr && value->diagnostic != nullptr)
    {
        value->diagnostic(value->diagnostic_user, 0, message, length);
    }
}

int32_t DPE_COMPONENT_PLUGIN_CALL dispatch_lifecycle(
    dpe_component_handle handle,
    dpe_component_lifecycle_phase_v2 phase,
    const dpe_component_update_v1* update_value)
{
    auto* value = static_cast<test_component*>(handle);
    if (value == nullptr) return -1;
    switch (phase)
    {
        case DPE_COMPONENT_LIFECYCLE_ENABLE_V2:
        {
            constexpr char message[] = "Native runtime test component enabled.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_FIXED_UPDATE_V2:
        {
            if (update_value == nullptr) return -1;
            constexpr char message[] = "Native runtime test component fixed update.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_VARIABLE_UPDATE_V2:
        {
            if (update_value == nullptr) return -1;
            constexpr char message[] = "Native runtime test component variable update.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_LATE_UPDATE_V2:
        {
            if (update_value == nullptr) return -1;
            if (value->fail_late_update)
            {
                value->error = "injected native late-update failure";
                return -1;
            }
            constexpr char message[] = "Native runtime test component late update.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_RENDER_SUBMISSION_V2:
        {
            if (update_value == nullptr) return -1;
            constexpr char message[] = "Native runtime test component render submission.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_DISABLE_V2:
        {
            constexpr char message[] = "Native runtime test component disabled.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
    }
    value->error = "invalid lifecycle phase";
    return -1;
}

const char* DPE_COMPONENT_PLUGIN_CALL last_error(dpe_component_handle handle)
{
    if (handle == nullptr) return "null test component";
    return static_cast<test_component*>(handle)->error.c_str();
}

const dpe_component_plugin_v1 api{1U, type_id, &create, &destroy, &set_properties, &update, &last_error};
const dpe_component_plugin_v2 api_v2{
    2U,
    static_cast<uint32_t>(sizeof(dpe_component_plugin_v2)),
    type_id,
    &create,
    &destroy,
    &set_properties,
    &dispatch_lifecycle,
    &last_error};
const dpe_tile_extension_plugin_v1 tile_extension_api{
    DPE_TILE_EXTENSION_ABI_V1,
    static_cast<uint32_t>(sizeof(dpe_tile_extension_plugin_v1)),
    DPE_TILE_EXTENSION_CAPABILITY_EVALUATE_TILES_V1
        | DPE_TILE_EXTENSION_CAPABILITY_PROPOSE_BRUSH_V1,
    0,
    {tile_extension_id, sizeof("example.weather-tile") - 1},
    &evaluate_tiles,
    &propose_brush,
    &release_extension_string};
}

extern "C" DPE_COMPONENT_PLUGIN_EXPORT const dpe_component_plugin_v1* DPE_COMPONENT_PLUGIN_CALL
dpe_component_plugin_get_v1(void)
{
    return &api;
}

extern "C" DPE_COMPONENT_PLUGIN_EXPORT const dpe_component_plugin_v2* DPE_COMPONENT_PLUGIN_CALL
dpe_component_plugin_get_v2(void)
{
    return &api_v2;
}

extern "C" DPE_TILE_EXTENSION_PLUGIN_EXPORT const dpe_tile_extension_plugin_v1*
DPE_TILE_EXTENSION_PLUGIN_CALL dpe_tile_extension_plugin_get_v1(void)
{
    return &tile_extension_api;
}
