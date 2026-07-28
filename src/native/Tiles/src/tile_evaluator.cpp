#include <dragonpixel/tiles/tile_evaluator.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dragonpixel::tiles
{
namespace
{
void mix(std::uint64_t& value, std::uint8_t byte) noexcept
{
    value ^= byte;
    value *= 1099511628211ULL;
}

void mix_integer(std::uint64_t& value, int integer) noexcept
{
    const auto bits = static_cast<std::uint32_t>(integer);
    for (unsigned shift = 0; shift < 32U; shift += 8U)
        mix(value, static_cast<std::uint8_t>((bits >> shift) & 0xffU));
}

double unit_random(std::uint64_t seed) noexcept
{
    seed ^= seed >> 12U;
    seed ^= seed << 25U;
    seed ^= seed >> 27U;
    return static_cast<double>((seed * 2685821657736338717ULL) >> 11U)
        / static_cast<double>(1ULL << 53U);
}

integer_point transform_offset(integer_point offset, grid_layout layout,
    unsigned rotation, bool mirror_x, bool mirror_y)
{
    if (mirror_x) offset.x = -offset.x;
    if (mirror_y) offset.y = -offset.y;
    const auto hex = layout == grid_layout::hex_point_top || layout == grid_layout::hex_flat_top;
    for (unsigned index = 0; index < rotation % (hex ? 6U : 4U); ++index)
        offset = hex ? integer_point{-offset.y, offset.x + offset.y}
                     : integer_point{-offset.y, offset.x};
    return offset;
}

bool condition_matches(const rule_neighbor& expected,
    const std::optional<tile_reference>& actual, tile_reference self)
{
    switch (expected.condition)
    {
        case rule_neighbor_condition::any: return true;
        case rule_neighbor_condition::same_tile: return actual && *actual == self;
        case rule_neighbor_condition::not_same_tile: return !actual || *actual != self;
        case rule_neighbor_condition::tile: return actual && expected.tile && *actual == *expected.tile;
        case rule_neighbor_condition::not_tile: return !actual || !expected.tile || *actual != *expected.tile;
        case rule_neighbor_condition::empty: return !actual;
        case rule_neighbor_condition::not_empty: return actual.has_value();
    }
    return false;
}

struct match_variant final
{
    unsigned rotation{};
    bool flip_x{};
    bool flip_y{};
};

std::vector<match_variant> variants(rule_match_transform transform, grid_layout layout)
{
    switch (transform)
    {
        case rule_match_transform::fixed: return {{}};
        case rule_match_transform::rotated:
            return layout == grid_layout::hex_point_top || layout == grid_layout::hex_flat_top
                ? std::vector<match_variant>{{0}, {1}, {2}, {3}, {4}, {5}}
                : std::vector<match_variant>{{0}, {1}, {2}, {3}};
        case rule_match_transform::mirror_x: return {{}, {0, true, false}};
        case rule_match_transform::mirror_y: return {{}, {0, false, true}};
        case rule_match_transform::mirror_xy:
            return {{}, {0, true, false}, {0, false, true}, {0, true, true}};
    }
    return {{}};
}

tile_reference weighted_output(const std::vector<weighted_tile_reference>& outputs, std::uint64_t seed)
{
    if (outputs.empty()) return {};
    double total{};
    for (const auto& output : outputs)
        if (std::isfinite(output.weight) && output.weight > 0.0) total += output.weight;
    if (!(total > 0.0)) return outputs.front().tile;
    auto target = unit_random(seed) * total;
    for (const auto& output : outputs)
    {
        if (!std::isfinite(output.weight) || output.weight <= 0.0) continue;
        if (target < output.weight) return output.tile;
        target -= output.weight;
    }
    return outputs.back().tile;
}

std::optional<std::pair<unsigned, sprite_reference>> animation_sprite(
    const std::vector<animation_frame>& frames,
    double time_seconds,
    double speed,
    double start_time,
    unsigned start_frame,
    bool loop_once,
    bool paused)
{
    if (frames.empty()) return std::nullopt;
    if (paused) return std::pair{start_frame % static_cast<unsigned>(frames.size()),
        frames[start_frame % frames.size()].sprite};
    double duration{};
    for (const auto& frame : frames) duration += std::max(frame.duration_seconds, 0.000001);
    auto elapsed = std::max(0.0, time_seconds - start_time) * std::max(speed, 0.0);
    if (loop_once) elapsed = std::min(elapsed, std::nextafter(duration, 0.0));
    else elapsed = std::fmod(elapsed, duration);
    auto index = start_frame % static_cast<unsigned>(frames.size());
    for (std::size_t visited = 0; visited < frames.size(); ++visited)
    {
        const auto frame_duration = std::max(frames[index].duration_seconds, 0.000001);
        if (elapsed < frame_duration) return std::pair{index, frames[index].sprite};
        elapsed -= frame_duration;
        index = (index + 1U) % static_cast<unsigned>(frames.size());
    }
    return std::pair{index, frames[index].sprite};
}
}

std::uint64_t stable_tile_seed(core::uuid map_id, core::uuid layer_id,
    integer_point cell, std::string_view type_seed) noexcept
{
    std::uint64_t result = 1469598103934665603ULL;
    for (const auto byte : map_id.bytes()) mix(result, byte);
    for (const auto byte : layer_id.bytes()) mix(result, byte);
    mix_integer(result, cell.x);
    mix_integer(result, cell.y);
    for (const auto byte : type_seed) mix(result, static_cast<std::uint8_t>(byte));
    return result;
}

tile_evaluation evaluate_tile(const tile_definition& definition, tile_reference self,
    grid_layout layout, integer_point cell, double time_seconds, std::uint64_t seed,
    const tile_lookup& neighbors, const definition_lookup& definitions,
    bool custom_behavior_available)
{
    tile_evaluation result;
    result.output = self;
    if (definition.kind == tile_kind::custom)
    {
        result.placeholder = !custom_behavior_available;
        return result;
    }
    if (definition.kind == tile_kind::animated)
    {
        const auto speed = definition.minimum_speed
            + (definition.maximum_speed - definition.minimum_speed) * unit_random(seed);
        if (const auto frame = animation_sprite(definition.animation_frames, time_seconds, speed,
                definition.animation_start_time, definition.animation_start_frame,
                definition.loop_once, definition.pause_animation))
        {
            result.animation_frame = frame->first;
            result.sprite = frame->second;
            result.refresh_physics = definition.update_physics;
        }
        return result;
    }
    const tile_definition* rules = &definition;
    if (definition.kind == tile_kind::rule_override && definition.override_source && definitions)
    {
        if (const auto* source = definitions(*definition.override_source)) rules = source;
        else
        {
            result.placeholder = true;
            return result;
        }
    }
    if (rules->kind != tile_kind::rule) return result;
    for (std::size_t rule_index = 0; rule_index < rules->rules.size(); ++rule_index)
    {
        const auto& rule = rules->rules[rule_index];
        if (rule.topology != layout) continue;
        for (const auto variant : variants(rule.match_transform, layout))
        {
            const auto matched = std::all_of(rule.neighbors.begin(), rule.neighbors.end(), [&](const auto& expected) {
                const auto offset = transform_offset(expected.offset, layout,
                    variant.rotation, variant.flip_x, variant.flip_y);
                return condition_matches(expected,
                    neighbors({cell.x + offset.x, cell.y + offset.y}), self);
            });
            if (!matched) continue;
            result.matched_rule = true;
            result.rotation_quarter_turns = variant.rotation % 4U;
            result.topology_rotation_steps = variant.rotation;
            result.flip_x = variant.flip_x;
            result.flip_y = variant.flip_y;
            if (rule.output_kind == rule_output_kind::animated)
            {
                if (const auto frame = animation_sprite(rule.animation, time_seconds, 1.0, 0.0, 0, false, false))
                {
                    result.animation_frame = frame->first;
                    result.sprite = frame->second;
                }
            }
            else
            {
                result.output = rule.output_kind == rule_output_kind::random
                    ? weighted_output(rule.outputs, seed ^ rule_index)
                    : rule.outputs.empty() ? self : rule.outputs.front().tile;
            }
            if (definition.kind == tile_kind::rule_override)
            {
                const auto replacement = std::find_if(definition.overrides.begin(), definition.overrides.end(),
                    [&](const auto& entry) { return entry.source_tile_id == result.output.tile_id; });
                if (replacement != definition.overrides.end()) result.output = replacement->replacement;
            }
            return result;
        }
    }
    return result;
}

tile_reference random_brush_tile(const std::vector<weighted_tile_reference>& candidates,
    core::uuid map_id, core::uuid layer_id, integer_point cell, std::string_view type_seed)
{
    return weighted_output(candidates, stable_tile_seed(map_id, layer_id, cell, type_seed));
}

std::vector<brush_cell> line_brush(grid_layout layout, integer_point start,
    integer_point end, const std::vector<tile_reference>& pattern)
{
    std::vector<brush_cell> result;
    if (pattern.empty()) return result;
    const auto line = grid_line(layout, start, end);
    result.reserve(line.size());
    for (std::size_t index = 0; index < line.size(); ++index)
        result.push_back({line[index], pattern[index % pattern.size()]});
    return result;
}

std::vector<tile_palette_cell> group_pick(const std::vector<brush_cell>& occupied,
    integer_point minimum, integer_point maximum, int gap, std::size_t limit)
{
    std::vector<tile_palette_cell> result;
    if (gap < 0 || limit == 0) return result;
    const auto min_x = std::min(minimum.x, maximum.x);
    const auto min_y = std::min(minimum.y, maximum.y);
    const auto max_x = std::max(minimum.x, maximum.x);
    const auto max_y = std::max(minimum.y, maximum.y);
    for (const auto& item : occupied)
    {
        if (item.cell.x < min_x || item.cell.x > max_x
            || item.cell.y < min_y || item.cell.y > max_y) continue;
        result.push_back({(item.cell.x - min_x) * (gap + 1),
            (item.cell.y - min_y) * (gap + 1), item.tile});
        if (result.size() == limit) break;
    }
    return result;
}
}
