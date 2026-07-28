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
    DPE_ABI_MINOR = 1,
    DPE_PHYSICS_ABI_MAJOR = 1,
    DPE_PHYSICS_ABI_MINOR = 1
};

typedef uint64_t dpe_runtime_handle;
typedef uint64_t dpe_world_handle;
typedef uint64_t dpe_entity_handle;
typedef uint64_t dpe_component_handle;
typedef uint64_t dpe_physics_world_handle;

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
    DPE_CAPABILITY_MANAGED_CALLBACK = UINT64_C(1) << 4,
    DPE_CAPABILITY_PHYSICS_V1 = UINT64_C(1) << 5
} dpe_capability_v1;

typedef enum dpe_physics_dimension_v1 {
    DPE_PHYSICS_DIMENSION_2D = 0,
    DPE_PHYSICS_DIMENSION_3D = 1
} dpe_physics_dimension_v1;

typedef enum dpe_physics_body_mode_v1 {
    DPE_PHYSICS_BODY_STATIC = 0,
    DPE_PHYSICS_BODY_KINEMATIC = 1,
    DPE_PHYSICS_BODY_DYNAMIC = 2
} dpe_physics_body_mode_v1;

typedef enum dpe_physics_shape_v1 {
    DPE_PHYSICS_SHAPE_BOX = 0,
    DPE_PHYSICS_SHAPE_CIRCLE_OR_SPHERE = 1,
    DPE_PHYSICS_SHAPE_POLYGON_2D = 2
} dpe_physics_shape_v1;

typedef enum dpe_physics_body_flags_v1 {
    DPE_PHYSICS_BODY_CONTINUOUS_COLLISION = UINT32_C(1) << 0
} dpe_physics_body_flags_v1;

typedef enum dpe_physics_command_kind_v1 {
    DPE_PHYSICS_COMMAND_FORCE = 0,
    DPE_PHYSICS_COMMAND_IMPULSE = 1,
    DPE_PHYSICS_COMMAND_SET_LINEAR_VELOCITY = 2,
    DPE_PHYSICS_COMMAND_SET_ANGULAR_VELOCITY = 3,
    DPE_PHYSICS_COMMAND_TELEPORT = 4
} dpe_physics_command_kind_v1;

typedef enum dpe_physics_contact_kind_v1 {
    DPE_PHYSICS_CONTACT_BEGIN = 0,
    DPE_PHYSICS_CONTACT_END = 1,
    DPE_PHYSICS_TRIGGER_BEGIN = 2,
    DPE_PHYSICS_TRIGGER_END = 3
} dpe_physics_contact_kind_v1;

typedef enum dpe_physics_step_flags_v1 {
    DPE_PHYSICS_STEP_DROPPED_TIME = UINT32_C(1) << 0
} dpe_physics_step_flags_v1;

typedef struct dpe_physics_world_settings_v1 {
    uint32_t struct_size;
    uint32_t maximum_catch_up_ticks;
    uint32_t box2d_solver_substeps;
    uint32_t jolt_collision_steps;
    double fixed_time_step_seconds;
    double gravity_2d[2];
    double gravity_3d[3];
} dpe_physics_world_settings_v1;

typedef struct dpe_physics_collider_v1 {
    uint32_t struct_size;
    uint32_t shape;
    uint32_t sensor;
    uint32_t reserved;
    double size[3];
    double offset[3];
    double density;
    double friction;
    double restitution;
    uint16_t layer;
    uint16_t mask;
    uint32_t reserved2;
} dpe_physics_collider_v1;

typedef struct dpe_physics_collider_v2 {
    uint32_t struct_size;
    uint32_t shape;
    uint32_t sensor;
    uint32_t reserved;
    double size[3];
    double offset[3];
    double density;
    double friction;
    double restitution;
    uint16_t layer;
    uint16_t mask;
    uint32_t reserved2;
    uint32_t vertex_count;
    uint32_t reserved3;
    double vertices[16];
} dpe_physics_collider_v2;

typedef struct dpe_physics_body_v1 {
    uint32_t struct_size;
    uint32_t dimension;
    uint32_t mode;
    uint32_t flags;
    uint8_t entity_uuid[16];
    double position[3];
    double rotation[4];
    double linear_velocity[3];
    double angular_velocity[3];
    double linear_damping;
    double angular_damping;
    double gravity_scale;
    uint32_t collider_start;
    uint32_t collider_count;
} dpe_physics_body_v1;

typedef struct dpe_physics_command_v1 {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t reserved[2];
    uint8_t entity_uuid[16];
    double value[3];
    double rotation[4];
} dpe_physics_command_v1;

typedef struct dpe_physics_transform_v1 {
    uint32_t struct_size;
    uint32_t reserved;
    uint8_t entity_uuid[16];
    double position[3];
    double rotation[4];
    double linear_velocity[3];
} dpe_physics_transform_v1;

typedef struct dpe_physics_contact_v1 {
    uint32_t struct_size;
    uint32_t kind;
    uint64_t tick;
    uint8_t entity_a_uuid[16];
    uint8_t entity_b_uuid[16];
    double point[3];
    double normal[3];
} dpe_physics_contact_v1;

typedef struct dpe_physics_step_result_v1 {
    uint32_t struct_size;
    uint32_t ticks;
    uint32_t flags;
    uint32_t reserved;
    uint64_t world_tick;
    double dropped_seconds;
    size_t transform_count;
    size_t pending_contact_count;
} dpe_physics_step_result_v1;

typedef struct dpe_physics_raycast_v1 {
    uint32_t struct_size;
    uint32_t dimension;
    double origin[3];
    double direction[3];
    double distance;
} dpe_physics_raycast_v1;

typedef struct dpe_physics_raycast_hit_v1 {
    uint32_t struct_size;
    uint32_t hit;
    uint8_t entity_uuid[16];
    double fraction;
    double point[3];
    double normal[3];
} dpe_physics_raycast_hit_v1;

typedef struct dpe_physics_api_v1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;

    dpe_status(DPE_CALL* create_world)(dpe_runtime_handle runtime, const dpe_physics_world_settings_v1* settings, dpe_physics_world_handle* out_world);
    dpe_status(DPE_CALL* destroy_world)(dpe_physics_world_handle world);
    dpe_status(DPE_CALL* rebuild)(dpe_physics_world_handle world, const dpe_physics_body_v1* bodies, size_t body_count, const dpe_physics_collider_v1* colliders, size_t collider_count);
    dpe_status(DPE_CALL* apply_commands)(dpe_physics_world_handle world, const dpe_physics_command_v1* commands, size_t command_count);
    dpe_status(DPE_CALL* step)(dpe_physics_world_handle world, double elapsed_seconds, dpe_physics_step_result_v1* out_result);
    dpe_status(DPE_CALL* copy_transforms)(dpe_physics_world_handle world, dpe_physics_transform_v1* buffer, size_t capacity, size_t* out_required);
    dpe_status(DPE_CALL* drain_contacts)(dpe_physics_world_handle world, dpe_physics_contact_v1* buffer, size_t capacity, size_t* out_required);
    dpe_status(DPE_CALL* raycast)(dpe_physics_world_handle world, const dpe_physics_raycast_v1* ray, dpe_physics_raycast_hit_v1* out_hit);
    dpe_status(DPE_CALL* rebuild_v2)(dpe_physics_world_handle world, const dpe_physics_body_v1* bodies, size_t body_count, const dpe_physics_collider_v2* colliders, size_t collider_count);
} dpe_physics_api_v1;

#define DPE_PHYSICS_API_V1_MINOR_0_SIZE offsetof(dpe_physics_api_v1, rebuild_v2)

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
    dpe_status(DPE_CALL* acquire_physics_api)(dpe_runtime_handle runtime, uint32_t requested_major, uint32_t requested_minor, dpe_physics_api_v1* out_api, size_t out_api_size);
} dpe_api_v1;

#define DPE_API_V1_MINOR_0_SIZE offsetof(dpe_api_v1, acquire_physics_api)

DPE_EXPORT dpe_status DPE_CALL dpe_get_api_v1(
    uint32_t requested_major,
    uint32_t requested_minor,
    dpe_api_v1* out_api,
    size_t out_api_size);

#ifdef __cplusplus
}
#endif

#endif
