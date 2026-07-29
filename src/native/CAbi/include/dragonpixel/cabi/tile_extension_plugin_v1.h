#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(DPE_TILE_EXTENSION_PLUGIN_BUILD)
#define DPE_TILE_EXTENSION_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DPE_TILE_EXTENSION_PLUGIN_EXPORT __declspec(dllimport)
#endif
#define DPE_TILE_EXTENSION_PLUGIN_CALL __cdecl
#else
#define DPE_TILE_EXTENSION_PLUGIN_EXPORT __attribute__((visibility("default")))
#define DPE_TILE_EXTENSION_PLUGIN_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    DPE_TILE_EXTENSION_ABI_V1 = 1,
    DPE_TILE_EXTENSION_CAPABILITY_EVALUATE_TILES_V1 = 1u << 0,
    DPE_TILE_EXTENSION_CAPABILITY_PROPOSE_BRUSH_V1 = 1u << 1,
    DPE_TILE_EXTENSION_MAX_BATCH_V1 = 65536,
    DPE_TILE_EXTENSION_MAX_COMMANDS_V1 = 1048576
};

typedef struct dpe_tile_extension_string_v1
{
    const char* data;
    size_t length;
} dpe_tile_extension_string_v1;

typedef struct dpe_tile_extension_context_v1
{
    uint32_t struct_size;
    int32_t cell_x;
    int32_t cell_y;
    int32_t elevation;
    uint32_t layout;
    uint64_t deterministic_seed;
    double elapsed_seconds;
    dpe_tile_extension_string_v1 map_id;
    dpe_tile_extension_string_v1 layer_id;
    dpe_tile_extension_string_v1 tile_set_id;
    dpe_tile_extension_string_v1 tile_id;
    dpe_tile_extension_string_v1 payload_json;
    dpe_tile_extension_string_v1 neighborhood_json;
} dpe_tile_extension_context_v1;

typedef struct dpe_tile_extension_result_v1
{
    uint32_t struct_size;
    int32_t status;
    dpe_tile_extension_string_v1 result_json;
    dpe_tile_extension_string_v1 error_code;
    dpe_tile_extension_string_v1 error_message;
} dpe_tile_extension_result_v1;

typedef int32_t (DPE_TILE_EXTENSION_PLUGIN_CALL *dpe_tile_extension_evaluate_fn_v1)(
    const dpe_tile_extension_context_v1* contexts,
    size_t context_count,
    dpe_tile_extension_result_v1* results,
    size_t result_count);

typedef int32_t (DPE_TILE_EXTENSION_PLUGIN_CALL *dpe_tile_extension_propose_brush_fn_v1)(
    const dpe_tile_extension_context_v1* context,
    dpe_tile_extension_string_v1 brush_request_json,
    dpe_tile_extension_result_v1* result);

typedef void (DPE_TILE_EXTENSION_PLUGIN_CALL *dpe_tile_extension_release_fn_v1)(
    dpe_tile_extension_string_v1 value);

typedef struct dpe_tile_extension_plugin_v1
{
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t capabilities;
    uint32_t reserved;
    dpe_tile_extension_string_v1 plugin_id;
    dpe_tile_extension_evaluate_fn_v1 evaluate_tiles;
    dpe_tile_extension_propose_brush_fn_v1 propose_brush;
    dpe_tile_extension_release_fn_v1 release_string;
} dpe_tile_extension_plugin_v1;

DPE_TILE_EXTENSION_PLUGIN_EXPORT const dpe_tile_extension_plugin_v1*
DPE_TILE_EXTENSION_PLUGIN_CALL dpe_tile_extension_plugin_get_v1(void);

#ifdef __cplusplus
}
#endif
