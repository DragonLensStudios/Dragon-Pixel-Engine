#pragma once

#include <dragonpixel/tiles/tile_grid.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace dragonpixel::tiles
{
using tile_lookup = std::function<std::optional<tile_reference>(integer_point)>;
using definition_lookup = std::function<const tile_definition*(tile_reference)>;

struct tile_evaluation final
{
    tile_reference output;
    std::optional<sprite_reference> sprite;
    unsigned animation_frame{};
    unsigned rotation_quarter_turns{};
    unsigned topology_rotation_steps{};
    bool flip_x{};
    bool flip_y{};
    bool matched_rule{};
    bool placeholder{};
    bool refresh_physics{};
    friend bool operator==(const tile_evaluation&, const tile_evaluation&) = default;
};

struct brush_cell final
{
    integer_point cell;
    tile_reference tile;
    friend bool operator==(const brush_cell&, const brush_cell&) = default;
};

[[nodiscard]] std::uint64_t stable_tile_seed(
    core::uuid map_id,
    core::uuid layer_id,
    integer_point cell,
    std::string_view type_seed) noexcept;

[[nodiscard]] tile_evaluation evaluate_tile(
    const tile_definition& definition,
    tile_reference self,
    grid_layout layout,
    integer_point cell,
    double time_seconds,
    std::uint64_t seed,
    const tile_lookup& neighbors,
    const definition_lookup& definitions = {},
    bool custom_behavior_available = false);

[[nodiscard]] tile_reference random_brush_tile(
    const std::vector<weighted_tile_reference>& candidates,
    core::uuid map_id,
    core::uuid layer_id,
    integer_point cell,
    std::string_view type_seed = "random-brush");

[[nodiscard]] std::vector<brush_cell> line_brush(
    grid_layout layout,
    integer_point start,
    integer_point end,
    const std::vector<tile_reference>& pattern);

[[nodiscard]] std::vector<tile_palette_cell> group_pick(
    const std::vector<brush_cell>& occupied,
    integer_point minimum,
    integer_point maximum,
    int gap,
    std::size_t limit);
}
