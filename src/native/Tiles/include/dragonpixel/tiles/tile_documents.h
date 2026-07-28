#pragma once

#include <dragonpixel/core/uuid.h>

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dragonpixel::tiles
{
struct integer_point final
{
    int x{};
    int y{};
    friend bool operator==(const integer_point&, const integer_point&) = default;
};

struct source_rectangle final
{
    int x{};
    int y{};
    int width{1};
    int height{1};
    friend bool operator==(const source_rectangle&, const source_rectangle&) = default;
};

struct double_point final
{
    double x{};
    double y{};
    friend bool operator==(const double_point&, const double_point&) = default;
};

struct color_rgba final
{
    double red{1.0};
    double green{1.0};
    double blue{1.0};
    double alpha{1.0};
    friend bool operator==(const color_rgba&, const color_rgba&) = default;
};

enum class grid_layout : std::uint8_t
{
    rectangular,
    hex_point_top,
    hex_flat_top,
    isometric,
    isometric_z_as_y
};

enum class tile_kind : std::uint8_t
{
    basic,
    animated,
    rule,
    rule_override,
    custom
};

enum class tile_collider_mode : std::uint8_t
{
    none,
    grid,
    sprite_outline
};

enum class tile_renderer_mode : std::uint8_t
{
    chunk,
    individual
};

enum class slice_mode : std::uint8_t
{
    cell_size,
    cell_count,
    automatic
};

enum class rule_neighbor_condition : std::uint8_t
{
    any,
    same_tile,
    not_same_tile,
    tile,
    not_tile,
    empty,
    not_empty
};

enum class rule_match_transform : std::uint8_t
{
    fixed,
    rotated,
    mirror_x,
    mirror_y,
    mirror_xy
};

enum class rule_output_kind : std::uint8_t
{
    fixed,
    random,
    animated
};

struct tile_reference final
{
    core::uuid tile_set_id;
    core::uuid tile_id;
    friend bool operator==(const tile_reference&, const tile_reference&) = default;
};

struct sprite_reference final
{
    core::uuid texture_asset_id;
    source_rectangle source;
    double_point pivot{0.5, 0.5};
    friend bool operator==(const sprite_reference&, const sprite_reference&) = default;
};

struct animation_frame final
{
    sprite_reference sprite;
    double duration_seconds{1.0 / 12.0};
    friend bool operator==(const animation_frame&, const animation_frame&) = default;
};

struct weighted_tile_reference final
{
    tile_reference tile;
    double weight{1.0};
    friend bool operator==(const weighted_tile_reference&, const weighted_tile_reference&) = default;
};

struct rule_neighbor final
{
    integer_point offset;
    rule_neighbor_condition condition{rule_neighbor_condition::any};
    std::optional<tile_reference> tile;
    friend bool operator==(const rule_neighbor&, const rule_neighbor&) = default;
};

struct tile_rule final
{
    grid_layout topology{grid_layout::rectangular};
    rule_match_transform match_transform{rule_match_transform::fixed};
    rule_output_kind output_kind{rule_output_kind::fixed};
    std::vector<rule_neighbor> neighbors;
    std::vector<weighted_tile_reference> outputs;
    std::vector<animation_frame> animation;
    friend bool operator==(const tile_rule&, const tile_rule&) = default;
};

struct rule_override_entry final
{
    core::uuid source_tile_id;
    tile_reference replacement;
    friend bool operator==(const rule_override_entry&, const rule_override_entry&) = default;
};

struct slicing_settings final
{
    slice_mode mode{slice_mode::cell_size};
    integer_point cell_count{1, 1};
    bool keep_empty_rects{};
    double_point pivot{0.5, 0.5};
    friend bool operator==(const slicing_settings&, const slicing_settings&) = default;
};

struct collision_rectangle final
{
    double offset_x{};
    double offset_y{};
    double width{1.0};
    double height{1.0};
    friend bool operator==(const collision_rectangle&, const collision_rectangle&) = default;
};

struct tile_definition final
{
    core::uuid tile_id;
    std::string name;
    source_rectangle source;
    std::optional<collision_rectangle> collision;
    tile_kind kind{tile_kind::basic};
    core::uuid texture_asset_id;
    double_point pivot{0.5, 0.5};
    tile_collider_mode collider_mode{tile_collider_mode::none};
    std::vector<double_point> collision_outline;
    std::vector<animation_frame> animation_frames;
    double minimum_speed{1.0};
    double maximum_speed{1.0};
    double animation_start_time{};
    unsigned animation_start_frame{};
    bool loop_once{};
    bool pause_animation{};
    bool update_physics{};
    std::vector<tile_rule> rules;
    std::optional<tile_reference> override_source;
    std::vector<rule_override_entry> overrides;
    std::string custom_type_id;
    std::uint32_t custom_type_version{1};
    std::string opaque_payload_json{"{}"};
    std::string opaque_fields_json{"{}"};
    friend bool operator==(const tile_definition&, const tile_definition&) = default;
};

struct tile_set_document final
{
    core::uuid asset_id;
    std::string name;
    core::uuid texture_asset_id;
    integer_point cell_size{32, 32};
    integer_point margin{};
    integer_point spacing{};
    double pixels_per_unit{32.0};
    std::vector<tile_definition> tiles;
    std::vector<core::uuid> texture_asset_ids;
    slicing_settings slicing;
    std::string opaque_fields_json{"{}"};
    std::uint32_t source_format_version{2};
    friend bool operator==(const tile_set_document&, const tile_set_document&) = default;
};

struct tile_cell final
{
    unsigned index{};
    core::uuid tile_id;
    bool flip_x{};
    bool flip_y{};
    unsigned rotation_quarter_turns{};
    core::uuid tile_set_id;
    color_rgba tint;
    double_point offset;
    double rotation_degrees{};
    double_point scale{1.0, 1.0};
    int elevation{};
    bool lock_color{};
    bool lock_transform{};
    std::string opaque_fields_json{"{}"};
    friend bool operator==(const tile_cell&, const tile_cell&) = default;
};

struct tile_chunk final
{
    int x{};
    int y{};
    std::vector<tile_cell> cells;
    friend bool operator==(const tile_chunk&, const tile_chunk&) = default;
};

struct tile_layer final
{
    core::uuid layer_id;
    std::string name;
    bool visible{true};
    unsigned order{};
    std::vector<tile_chunk> chunks;
    color_rgba tint;
    core::uuid material_asset_id;
    int sort_order{};
    tile_renderer_mode renderer_mode{tile_renderer_mode::chunk};
    double animation_rate{1.0};
    double_point culling_padding;
    std::string opaque_fields_json{"{}"};
    friend bool operator==(const tile_layer&, const tile_layer&) = default;
};

struct tile_grid_settings final
{
    grid_layout layout{grid_layout::rectangular};
    double_point cell_size{1.0, 1.0};
    double_point cell_gap;
    double_point tile_anchor{0.5, 0.5};
    friend bool operator==(const tile_grid_settings&, const tile_grid_settings&) = default;
};

struct tilemap_document final
{
    core::uuid asset_id;
    std::string name;
    std::vector<core::uuid> tile_set_dependencies;
    std::vector<tile_layer> layers;
    tile_grid_settings grid;
    std::string opaque_fields_json{"{}"};
    std::uint32_t source_format_version{2};
    friend bool operator==(const tilemap_document&, const tilemap_document&) = default;
};

struct tile_palette_cell final
{
    int u{};
    int v{};
    tile_reference tile;
    bool flip_x{};
    bool flip_y{};
    unsigned rotation_quarter_turns{};
    color_rgba tint;
    friend bool operator==(const tile_palette_cell&, const tile_palette_cell&) = default;
};

struct tile_palette_document final
{
    core::uuid asset_id;
    std::string name;
    std::vector<core::uuid> tile_set_dependencies;
    std::vector<tile_palette_cell> cells;
    std::string opaque_fields_json{"{}"};
    friend bool operator==(const tile_palette_document&, const tile_palette_document&) = default;
};

template <typename Document>
struct document_result final
{
    std::optional<Document> document;
    std::string error;
    [[nodiscard]] bool succeeded() const noexcept { return document.has_value(); }
};

[[nodiscard]] document_result<tile_set_document> read_tile_set(std::string_view json);
[[nodiscard]] document_result<tilemap_document> read_tilemap(std::string_view json);
[[nodiscard]] document_result<tile_palette_document> read_tile_palette(std::string_view json);
[[nodiscard]] std::string write_tile_set(const tile_set_document& document);
[[nodiscard]] std::string write_tilemap(const tilemap_document& document);
[[nodiscard]] std::string write_tile_palette(const tile_palette_document& document);
}
