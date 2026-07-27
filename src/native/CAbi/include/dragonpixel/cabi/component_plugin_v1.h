#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(DPE_COMPONENT_PLUGIN_BUILD)
#define DPE_COMPONENT_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DPE_COMPONENT_PLUGIN_EXPORT __declspec(dllimport)
#endif
#define DPE_COMPONENT_PLUGIN_CALL __cdecl
#else
#define DPE_COMPONENT_PLUGIN_EXPORT __attribute__((visibility("default")))
#define DPE_COMPONENT_PLUGIN_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* dpe_component_handle;
typedef void (DPE_COMPONENT_PLUGIN_CALL *dpe_component_diagnostic_fn)(
    void* user,
    int severity,
    const char* utf8_message,
    size_t length);

typedef struct dpe_component_update_v1
{
    double elapsed_seconds;
    double delta_seconds;
    const char* input_json;
    size_t input_json_length;
} dpe_component_update_v1;

typedef struct dpe_component_plugin_v1
{
    uint32_t abi_version;
    const char* component_type_id;
    dpe_component_handle (DPE_COMPONENT_PLUGIN_CALL *create)(
        const char* entity_id,
        dpe_component_diagnostic_fn diagnostic,
        void* user);
    void (DPE_COMPONENT_PLUGIN_CALL *destroy)(dpe_component_handle handle);
    int32_t (DPE_COMPONENT_PLUGIN_CALL *set_properties)(
        dpe_component_handle handle,
        const char* json,
        size_t length);
    int32_t (DPE_COMPONENT_PLUGIN_CALL *update)(
        dpe_component_handle handle,
        const dpe_component_update_v1* update);
    const char* (DPE_COMPONENT_PLUGIN_CALL *last_error)(dpe_component_handle handle);
} dpe_component_plugin_v1;

typedef enum dpe_component_lifecycle_phase_v2
{
    DPE_COMPONENT_LIFECYCLE_ENABLE_V2 = 1,
    DPE_COMPONENT_LIFECYCLE_FIXED_UPDATE_V2 = 2,
    DPE_COMPONENT_LIFECYCLE_VARIABLE_UPDATE_V2 = 3,
    DPE_COMPONENT_LIFECYCLE_LATE_UPDATE_V2 = 4,
    DPE_COMPONENT_LIFECYCLE_RENDER_SUBMISSION_V2 = 5,
    DPE_COMPONENT_LIFECYCLE_DISABLE_V2 = 6
} dpe_component_lifecycle_phase_v2;

typedef struct dpe_component_plugin_v2
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char* component_type_id;
    dpe_component_handle (DPE_COMPONENT_PLUGIN_CALL *create)(
        const char* entity_id,
        dpe_component_diagnostic_fn diagnostic,
        void* user);
    void (DPE_COMPONENT_PLUGIN_CALL *destroy)(dpe_component_handle handle);
    int32_t (DPE_COMPONENT_PLUGIN_CALL *set_properties)(
        dpe_component_handle handle,
        const char* json,
        size_t length);
    int32_t (DPE_COMPONENT_PLUGIN_CALL *dispatch_lifecycle)(
        dpe_component_handle handle,
        dpe_component_lifecycle_phase_v2 phase,
        const dpe_component_update_v1* update);
    const char* (DPE_COMPONENT_PLUGIN_CALL *last_error)(dpe_component_handle handle);
} dpe_component_plugin_v2;

DPE_COMPONENT_PLUGIN_EXPORT const dpe_component_plugin_v1* DPE_COMPONENT_PLUGIN_CALL
dpe_component_plugin_get_v1(void);

DPE_COMPONENT_PLUGIN_EXPORT const dpe_component_plugin_v2* DPE_COMPONENT_PLUGIN_CALL
dpe_component_plugin_get_v2(void);

#ifdef __cplusplus
}
#endif
