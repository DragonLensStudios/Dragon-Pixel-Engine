#pragma once

#include <dragonpixel/tiles/tile_documents.h>

#include <array>
#include <span>
#include <vector>

namespace dragonpixel::tiles
{
struct projected_cell final
{
    double x{};
    double y{};
    double sort_key{};
    friend bool operator==(const projected_cell&, const projected_cell&) = default;
};

[[nodiscard]] projected_cell project_cell(
    const tile_grid_settings& grid,
    integer_point cell,
    int elevation = 0) noexcept;
[[nodiscard]] integer_point unproject_cell(
    const tile_grid_settings& grid,
    double_point position,
    int elevation = 0) noexcept;
[[nodiscard]] std::vector<integer_point> neighbor_offsets(grid_layout layout);
[[nodiscard]] std::vector<integer_point> grid_line(
    grid_layout layout,
    integer_point start,
    integer_point end);
[[nodiscard]] std::vector<double_point> grid_collision_polygon(
    const tile_grid_settings& grid,
    integer_point cell,
    int elevation = 0);
[[nodiscard]] std::vector<std::array<double_point, 3>> triangulate_polygon(
    std::span<const double_point> polygon);
}
