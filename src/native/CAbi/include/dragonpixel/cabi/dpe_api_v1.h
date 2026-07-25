#ifndef DRAGON_PIXEL_DPE_API_V1_H
#define DRAGON_PIXEL_DPE_API_V1_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define DPE_CALL __cdecl
#if defined(DPE_API_BUILD)
#define DPE_EXPORT __declspec(dllexport)
#else
#define DPE_EXPORT __declspec(dllimport)
#endif
#else
#define DPE_CALL
#define DPE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DPE_ABI_MAJOR = 1,
    DPE_ABI_MINOR = 0
};

typedef uint64_t dpe_runtime_handle;
typedef uint64_t dpe_world_handle;
typedef uint64_t dpe_entity_handle;
typedef uint64_t dpe_component_handle;

typedef enum dpe_status {
    DPE_STATUS_OK = 0,
    DPE_STATUS_INVALID_ARGUMENT = 1,
    DPE_STATUS_ABI_VERSION_UNSUPPORTED = 2,
    DPE_STATUS_INVALID_HANDLE = 3,
    DPE_STATUS_BUFFER_TOO_SMALL = 4,
    DPE_STATUS_OUT_OF_MEMORY = 5,
    DPE_STATUS_INTERNAL_ERROR = 6,
    DPE_STATUS_CALLBACK_FAILURE = 7,
    DPE_STATUS_INVALID_UTF8 = 8
} dpe_status;

typedef enum dpe_capability_v1 {
    DPE_CAPABILITY_STRUCTURED_ERRORS = UINT64_C(1) << 0,
    DPE_CAPABILITY_TRACKED_ALLOCATOR = UINT64_C(1) << 1,
    DPE_CAPABILITY_LIVE_COUNTS = UINT64_C(1) << 2,
    DPE_CAPABILITY_NATIVE_COUNTER_COMPONENT = UINT64_C(1) << 3,
    DPE_CAPABILITY_MANAGED_CALLBACK = UINT64_C(1) << 4
} dpe_capability_v1;

typedef struct dpe_utf8_view {
    const uint8_t* data;
    size_t length;
} dpe_utf8_view;

typedef struct dpe_error_info_v1 {
    int32_t code;
    uint32_t subsystem;
    uint8_t correlation_id[16];
} dpe_error_info_v1;

typedef struct dpe_live_counts_v1 {
    uint64_t runtimes;
    uint64_t worlds;
    uint64_t entities;
    uint64_t components;
    uint64_t allocations;
} dpe_live_counts_v1;

typedef dpe_status(DPE_CALL* dpe_managed_callback_v1)(void* context, int32_t* out_value);

typedef struct dpe_api_v1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
    uint64_t capabilities;

    dpe_status(DPE_CALL* create_runtime)(dpe_runtime_handle* out_runtime);
    dpe_status(DPE_CALL* destroy_runtime)(dpe_runtime_handle runtime);
    dpe_status(DPE_CALL* create_world)(dpe_runtime_handle runtime, dpe_world_handle* out_world);
    dpe_status(DPE_CALL* destroy_world)(dpe_world_handle world);
    dpe_status(DPE_CALL* create_entity)(dpe_world_handle world, const uint8_t uuid[16], dpe_utf8_view name, dpe_entity_handle* out_entity);
    dpe_status(DPE_CALL* destroy_entity)(dpe_entity_handle entity);
    dpe_status(DPE_CALL* get_entity_name)(dpe_entity_handle entity, uint8_t* buffer, size_t capacity, size_t* out_required);
    dpe_status(DPE_CALL* attach_counter_component)(dpe_entity_handle entity, int64_t initial_value, dpe_component_handle* out_component);
    dpe_status(DPE_CALL* destroy_component)(dpe_component_handle component);
    dpe_status(DPE_CALL* get_counter_value)(dpe_component_handle component, int64_t* out_value);
    dpe_status(DPE_CALL* set_counter_value)(dpe_component_handle component, int64_t value);
    dpe_status(DPE_CALL* allocate)(size_t size, void** out_memory);
    dpe_status(DPE_CALL* deallocate)(void* memory);
    dpe_status(DPE_CALL* get_live_counts)(dpe_live_counts_v1* out_counts);
    dpe_status(DPE_CALL* get_last_error)(dpe_error_info_v1* out_info, uint8_t* message_buffer, size_t capacity, size_t* out_required);
    dpe_status(DPE_CALL* force_native_exception)(void);
    dpe_status(DPE_CALL* invoke_managed_callback)(dpe_managed_callback_v1 callback, void* context, int32_t* out_value);
} dpe_api_v1;

DPE_EXPORT dpe_status DPE_CALL dpe_get_api_v1(
    uint32_t requested_major,
    uint32_t requested_minor,
    dpe_api_v1* out_api,
    size_t out_api_size);

#ifdef __cplusplus
}
#endif

#endif
