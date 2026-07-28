#include <dragonpixel/tiles/tile_grid.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace dragonpixel::tiles
{
namespace
{
struct cube_coordinate final
{
    double x{};
    double y{};
    double z{};
};

double width(const tile_grid_settings& grid) noexcept
{
    return grid.cell_size.x + grid.cell_gap.x;
}

double height(const tile_grid_settings& grid) noexcept
{
    return grid.cell_size.y + grid.cell_gap.y;
}

integer_point cube_round(cube_coordinate value) noexcept
{
    auto x = std::round(value.x);
    auto y = std::round(value.y);
    auto z = std::round(value.z);
    const auto x_difference = std::abs(x - value.x);
    const auto y_difference = std::abs(y - value.y);
    const auto z_difference = std::abs(z - value.z);
    if (x_difference > y_difference && x_difference > z_difference)
    {
        x = -y - z;
    }
    else if (y_difference > z_difference)
    {
        y = -x - z;
    }
    else
    {
        z = -x - y;
    }
    return {static_cast<int>(x), static_cast<int>(z)};
}

std::vector<integer_point> square_line(integer_point start, integer_point end)
{
    std::vector<integer_point> result;
    auto x = start.x;
    auto y = start.y;
    const auto delta_x = std::abs(end.x - start.x);
    const auto step_x = start.x < end.x ? 1 : -1;
    const auto delta_y = -std::abs(end.y - start.y);
    const auto step_y = start.y < end.y ? 1 : -1;
    auto error = delta_x + delta_y;
    for (;;)
    {
        result.push_back({x, y});
        if (x == end.x && y == end.y) break;
        const auto twice_error = 2 * error;
        if (twice_error >= delta_y)
        {
            error += delta_y;
            x += step_x;
        }
        if (twice_error <= delta_x)
        {
            error += delta_x;
            y += step_y;
        }
    }
    return result;
}

std::vector<integer_point> hex_line(integer_point start, integer_point end)
{
    const cube_coordinate begin{static_cast<double>(start.x),
        static_cast<double>(-start.x - start.y), static_cast<double>(start.y)};
    const cube_coordinate finish{static_cast<double>(end.x),
        static_cast<double>(-end.x - end.y), static_cast<double>(end.y)};
    const auto distance = static_cast<int>((std::abs(begin.x - finish.x)
        + std::abs(begin.y - finish.y) + std::abs(begin.z - finish.z)) / 2.0);
    std::vector<integer_point> result;
    result.reserve(static_cast<std::size_t>(distance) + 1);
    for (auto index = 0; index <= distance; ++index)
    {
        const auto amount = distance == 0 ? 0.0 : static_cast<double>(index) / distance;
        const auto point = cube_round({begin.x + (finish.x - begin.x) * amount,
            begin.y + (finish.y - begin.y) * amount,
            begin.z + (finish.z - begin.z) * amount});
        if (result.empty() || result.back() != point) result.push_back(point);
    }
    return result;
}

double cross(double_point first, double_point second, double_point third) noexcept
{
    return (second.x - first.x) * (third.y - first.y)
        - (second.y - first.y) * (third.x - first.x);
}

bool point_on_segment(double_point point, double_point first, double_point second) noexcept
{
    constexpr double epsilon = 1e-10;
    return std::abs(cross(first, second, point)) <= epsilon
        && point.x >= std::min(first.x, second.x) - epsilon
        && point.x <= std::max(first.x, second.x) + epsilon
        && point.y >= std::min(first.y, second.y) - epsilon
        && point.y <= std::max(first.y, second.y) + epsilon;
}

bool segments_intersect(
    double_point first_a,
    double_point first_b,
    double_point second_a,
    double_point second_b) noexcept
{
    const auto first_side_a = cross(first_a, first_b, second_a);
    const auto first_side_b = cross(first_a, first_b, second_b);
    const auto second_side_a = cross(second_a, second_b, first_a);
    const auto second_side_b = cross(second_a, second_b, first_b);
    if (((first_side_a > 0.0) != (first_side_b > 0.0))
        && ((second_side_a > 0.0) != (second_side_b > 0.0)))
    {
        return true;
    }
    return point_on_segment(second_a, first_a, first_b)
        || point_on_segment(second_b, first_a, first_b)
        || point_on_segment(first_a, second_a, second_b)
        || point_on_segment(first_b, second_a, second_b);
}

bool point_in_triangle(
    double_point point,
    double_point first,
    double_point second,
    double_point third,
    bool counter_clockwise) noexcept
{
    constexpr double epsilon = 1e-10;
    const auto first_side = cross(first, second, point);
    const auto second_side = cross(second, third, point);
    const auto third_side = cross(third, first, point);
    return counter_clockwise
        ? first_side >= -epsilon && second_side >= -epsilon && third_side >= -epsilon
        : first_side <= epsilon && second_side <= epsilon && third_side <= epsilon;
}
}

projected_cell project_cell(const tile_grid_settings& grid, integer_point cell, int elevation) noexcept
{
    const auto cell_width = width(grid);
    const auto cell_height = height(grid);
    switch (grid.layout)
    {
        case grid_layout::rectangular:
            return {cell.x * cell_width, cell.y * cell_height,
                cell.y * cell_height + static_cast<double>(elevation) * cell_height};
        case grid_layout::hex_point_top:
        {
            const auto x = cell_width * (cell.x + 0.5 * cell.y);
            const auto y = cell_height * 0.75 * cell.y;
            return {x, y, y + static_cast<double>(elevation) * cell_height};
        }
        case grid_layout::hex_flat_top:
        {
            const auto x = cell_width * 0.75 * cell.x;
            const auto y = cell_height * (cell.y + 0.5 * cell.x);
            return {x, y, y + static_cast<double>(elevation) * cell_height};
        }
        case grid_layout::isometric:
        {
            const auto x = (cell.x - cell.y) * cell_width * 0.5;
            const auto y = (cell.x + cell.y) * cell_height * 0.5;
            return {x, y, y + static_cast<double>(elevation) * cell_height};
        }
        case grid_layout::isometric_z_as_y:
        {
            const auto x = (cell.x - cell.y) * cell_width * 0.5;
            const auto base_y = (cell.x + cell.y) * cell_height * 0.5;
            const auto y = base_y + static_cast<double>(elevation) * cell_height * 0.5;
            return {x, y, base_y + static_cast<double>(elevation) * cell_height};
        }
    }
    return {};
}

integer_point unproject_cell(const tile_grid_settings& grid, double_point position, int elevation) noexcept
{
    const auto cell_width = width(grid);
    const auto cell_height = height(grid);
    switch (grid.layout)
    {
        case grid_layout::rectangular:
            return {static_cast<int>(std::round(position.x / cell_width)),
                static_cast<int>(std::round(position.y / cell_height))};
        case grid_layout::hex_point_top:
        {
            const auto r = position.y / (cell_height * 0.75);
            const auto q = position.x / cell_width - 0.5 * r;
            return cube_round({q, -q - r, r});
        }
        case grid_layout::hex_flat_top:
        {
            const auto q = position.x / (cell_width * 0.75);
            const auto r = position.y / cell_height - 0.5 * q;
            return cube_round({q, -q - r, r});
        }
        case grid_layout::isometric:
        case grid_layout::isometric_z_as_y:
        {
            auto y = position.y;
            if (grid.layout == grid_layout::isometric_z_as_y)
            {
                y -= static_cast<double>(elevation) * cell_height * 0.5;
            }
            const auto horizontal = position.x / (cell_width * 0.5);
            const auto vertical = y / (cell_height * 0.5);
            return {static_cast<int>(std::round((vertical + horizontal) * 0.5)),
                static_cast<int>(std::round((vertical - horizontal) * 0.5))};
        }
    }
    return {};
}

std::vector<integer_point> neighbor_offsets(grid_layout layout)
{
    if (layout == grid_layout::hex_point_top || layout == grid_layout::hex_flat_top)
    {
        return {{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};
    }
    return {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
}

std::vector<integer_point> grid_line(grid_layout layout, integer_point start, integer_point end)
{
    return layout == grid_layout::hex_point_top || layout == grid_layout::hex_flat_top
        ? hex_line(start, end) : square_line(start, end);
}

std::vector<double_point> grid_collision_polygon(
    const tile_grid_settings& grid, integer_point cell, int elevation)
{
    const auto center = project_cell(grid, cell, elevation);
    const auto half_width = grid.cell_size.x * 0.5;
    const auto half_height = grid.cell_size.y * 0.5;
    switch (grid.layout)
    {
        case grid_layout::hex_point_top:
            return {{center.x, center.y + half_height},
                {center.x + half_width, center.y + half_height * 0.5},
                {center.x + half_width, center.y - half_height * 0.5},
                {center.x, center.y - half_height},
                {center.x - half_width, center.y - half_height * 0.5},
                {center.x - half_width, center.y + half_height * 0.5}};
        case grid_layout::hex_flat_top:
            return {{center.x + half_width, center.y},
                {center.x + half_width * 0.5, center.y + half_height},
                {center.x - half_width * 0.5, center.y + half_height},
                {center.x - half_width, center.y},
                {center.x - half_width * 0.5, center.y - half_height},
                {center.x + half_width * 0.5, center.y - half_height}};
        case grid_layout::isometric:
        case grid_layout::isometric_z_as_y:
            return {{center.x, center.y + half_height}, {center.x + half_width, center.y},
                {center.x, center.y - half_height}, {center.x - half_width, center.y}};
        case grid_layout::rectangular:
            return {{center.x - half_width, center.y - half_height},
                {center.x + half_width, center.y - half_height},
                {center.x + half_width, center.y + half_height},
                {center.x - half_width, center.y + half_height}};
    }
    return {};
}

std::vector<std::array<double_point, 3>> triangulate_polygon(
    std::span<const double_point> polygon)
{
    constexpr double epsilon = 1e-10;
    constexpr std::size_t maximum_vertices = 256;
    if (polygon.size() < 3 || polygon.size() > maximum_vertices
        || std::any_of(polygon.begin(), polygon.end(), [](const auto& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y);
        }))
    {
        return {};
    }
    for (std::size_t first = 0; first < polygon.size(); ++first)
    {
        const auto first_next = (first + 1) % polygon.size();
        if (std::abs(polygon[first].x - polygon[first_next].x) <= epsilon
            && std::abs(polygon[first].y - polygon[first_next].y) <= epsilon)
        {
            return {};
        }
        for (std::size_t second = first + 1; second < polygon.size(); ++second)
        {
            const auto second_next = (second + 1) % polygon.size();
            if (first == second || first_next == second || second_next == first) continue;
            if (segments_intersect(
                polygon[first], polygon[first_next], polygon[second], polygon[second_next]))
            {
                return {};
            }
        }
    }

    double signed_area{};
    for (std::size_t index = 0; index < polygon.size(); ++index)
    {
        const auto& current = polygon[index];
        const auto& next = polygon[(index + 1) % polygon.size()];
        signed_area += current.x * next.y - next.x * current.y;
    }
    if (std::abs(signed_area) <= epsilon) return {};
    const auto counter_clockwise = signed_area > 0.0;

    std::vector<std::size_t> remaining(polygon.size());
    std::iota(remaining.begin(), remaining.end(), std::size_t{});
    std::vector<std::array<double_point, 3>> result;
    result.reserve(polygon.size() - 2);
    while (remaining.size() > 3)
    {
        bool clipped{};
        for (std::size_t candidate = 0; candidate < remaining.size(); ++candidate)
        {
            const auto previous_index = remaining[(candidate + remaining.size() - 1) % remaining.size()];
            const auto current_index = remaining[candidate];
            const auto next_index = remaining[(candidate + 1) % remaining.size()];
            const auto turn = cross(
                polygon[previous_index], polygon[current_index], polygon[next_index]);
            if (std::abs(turn) <= epsilon || ((turn > 0.0) != counter_clockwise)) continue;
            const auto contains_vertex = std::any_of(
                remaining.begin(), remaining.end(), [&](const auto index) {
                    return index != previous_index && index != current_index && index != next_index
                        && point_in_triangle(polygon[index], polygon[previous_index],
                            polygon[current_index], polygon[next_index], counter_clockwise);
                });
            if (contains_vertex) continue;
            result.push_back({
                polygon[previous_index], polygon[current_index], polygon[next_index]});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(candidate));
            clipped = true;
            break;
        }
        if (!clipped) return {};
    }
    result.push_back({
        polygon[remaining[0]], polygon[remaining[1]], polygon[remaining[2]]});
    return result;
}
}
