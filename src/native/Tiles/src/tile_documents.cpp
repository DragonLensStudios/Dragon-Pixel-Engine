#include <dragonpixel/tiles/tile_documents.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>

namespace dragonpixel::tiles
{
namespace
{
using json = nlohmann::ordered_json;

constexpr std::size_t maximum_dependencies = 4096;
constexpr std::size_t maximum_tiles = 1048576;
constexpr std::size_t maximum_layers = 4096;
constexpr std::size_t maximum_chunks = 1048576;
constexpr std::size_t maximum_cells = 1048576;

core::uuid required_uuid(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_string())
    {
        throw std::runtime_error{std::string{"Missing UUID field: "} + field};
    }
    const auto parsed = core::uuid::parse(value.at(field).get<std::string>());
    if (!parsed)
    {
        throw std::runtime_error{std::string{"Invalid UUID field: "} + field};
    }
    return *parsed;
}

core::uuid optional_uuid(const json& value, const char* field)
{
    if (!value.contains(field) || value.at(field).is_null()) return {};
    return required_uuid(value, field);
}

std::string required_string(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_string()
        || value.at(field).get<std::string>().empty())
    {
        throw std::runtime_error{std::string{"Missing string field: "} + field};
    }
    return value.at(field).get<std::string>();
}

integer_point read_point(const json& value, const char* field, bool positive)
{
    if (!value.contains(field) || !value.at(field).is_object())
    {
        throw std::runtime_error{std::string{"Missing point field: "} + field};
    }
    const auto& point = value.at(field);
    integer_point result{point.at("x").get<int>(), point.at("y").get<int>()};
    if ((positive && (result.x <= 0 || result.y <= 0))
        || (!positive && (result.x < 0 || result.y < 0)))
    {
        throw std::runtime_error{std::string{"Invalid point field: "} + field};
    }
    return result;
}

integer_point read_signed_point(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_object())
    {
        throw std::runtime_error{std::string{"Missing point field: "} + field};
    }
    const auto& point = value.at(field);
    return {point.at("x").get<int>(), point.at("y").get<int>()};
}

double required_finite(const json& value, const char* field)
{
    const auto result = value.at(field).get<double>();
    if (!std::isfinite(result))
    {
        throw std::runtime_error{std::string{"Non-finite number field: "} + field};
    }
    return result;
}

double_point read_double_point(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_object())
    {
        throw std::runtime_error{std::string{"Missing point field: "} + field};
    }
    const auto& point = value.at(field);
    return {required_finite(point, "x"), required_finite(point, "y")};
}

color_rgba read_color(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_object())
    {
        throw std::runtime_error{std::string{"Missing color field: "} + field};
    }
    const auto& color = value.at(field);
    return {required_finite(color, "r"), required_finite(color, "g"),
        required_finite(color, "b"), required_finite(color, "a")};
}

source_rectangle read_source(const json& value)
{
    source_rectangle result{value.at("x").get<int>(), value.at("y").get<int>(),
        value.at("width").get<int>(), value.at("height").get<int>()};
    if (result.x < 0 || result.y < 0 || result.width <= 0 || result.height <= 0)
    {
        throw std::runtime_error{"Source rectangles must be positive."};
    }
    return result;
}

json point_json(const integer_point& point)
{
    return {{"x", point.x}, {"y", point.y}};
}

json double_point_json(const double_point& point)
{
    return {{"x", point.x}, {"y", point.y}};
}

json color_json(const color_rgba& color)
{
    return {{"r", color.red}, {"g", color.green}, {"b", color.blue}, {"a", color.alpha}};
}

json source_json(const source_rectangle& source)
{
    return {{"x", source.x}, {"y", source.y}, {"width", source.width}, {"height", source.height}};
}

std::uint32_t require_envelope(const json& value, std::string_view format,
    std::initializer_list<std::uint32_t> versions)
{
    if (!value.is_object() || value.value("format", "") != format)
    {
        throw std::runtime_error{"Unsupported tile document format."};
    }
    const auto version = value.value("formatVersion", 0U);
    if (std::find(versions.begin(), versions.end(), version) == versions.end())
    {
        throw std::runtime_error{"Unsupported tile document version."};
    }
    return version;
}

std::string unknown_fields(const json& value, std::initializer_list<std::string_view> known)
{
    json result = json::object();
    for (const auto& [key, child] : value.items())
    {
        if (std::find(known.begin(), known.end(), key) == known.end()) result[key] = child;
    }
    return result.dump();
}

void apply_opaque(json& target, const std::string& encoded)
{
    if (encoded.empty()) return;
    const auto opaque = json::parse(encoded);
    if (!opaque.is_object()) throw std::runtime_error{"Opaque fields must encode a JSON object."};
    for (const auto& [key, value] : opaque.items())
    {
        if (!target.contains(key)) target[key] = value;
    }
}

std::string canonical_json(const json& value)
{
    return value.dump();
}

template <typename Enum, std::size_t Size>
Enum parse_enum(const json& value, const char* field,
    const std::array<std::pair<std::string_view, Enum>, Size>& values)
{
    const auto name = required_string(value, field);
    const auto found = std::find_if(values.begin(), values.end(), [&](const auto& item) {
        return item.first == name;
    });
    if (found == values.end())
    {
        throw std::runtime_error{std::string{"Unsupported value for "} + field + ": " + name};
    }
    return found->second;
}

template <typename Enum, std::size_t Size>
std::string_view enum_name(Enum value,
    const std::array<std::pair<std::string_view, Enum>, Size>& values)
{
    const auto found = std::find_if(values.begin(), values.end(), [&](const auto& item) {
        return item.second == value;
    });
    if (found == values.end()) throw std::runtime_error{"Unsupported tile enumeration value."};
    return found->first;
}

constexpr std::array layout_values{
    std::pair<std::string_view, grid_layout>{"rectangular", grid_layout::rectangular},
    std::pair<std::string_view, grid_layout>{"hex-point-top", grid_layout::hex_point_top},
    std::pair<std::string_view, grid_layout>{"hex-flat-top", grid_layout::hex_flat_top},
    std::pair<std::string_view, grid_layout>{"isometric", grid_layout::isometric},
    std::pair<std::string_view, grid_layout>{"isometric-z-as-y", grid_layout::isometric_z_as_y}};
constexpr std::array kind_values{
    std::pair<std::string_view, tile_kind>{"basic", tile_kind::basic},
    std::pair<std::string_view, tile_kind>{"animated", tile_kind::animated},
    std::pair<std::string_view, tile_kind>{"rule", tile_kind::rule},
    std::pair<std::string_view, tile_kind>{"rule-override", tile_kind::rule_override},
    std::pair<std::string_view, tile_kind>{"custom", tile_kind::custom}};
constexpr std::array collider_values{
    std::pair<std::string_view, tile_collider_mode>{"none", tile_collider_mode::none},
    std::pair<std::string_view, tile_collider_mode>{"grid", tile_collider_mode::grid},
    std::pair<std::string_view, tile_collider_mode>{"sprite-outline", tile_collider_mode::sprite_outline}};
constexpr std::array renderer_values{
    std::pair<std::string_view, tile_renderer_mode>{"chunk", tile_renderer_mode::chunk},
    std::pair<std::string_view, tile_renderer_mode>{"individual", tile_renderer_mode::individual}};
constexpr std::array slice_values{
    std::pair<std::string_view, slice_mode>{"cell-size", slice_mode::cell_size},
    std::pair<std::string_view, slice_mode>{"cell-count", slice_mode::cell_count},
    std::pair<std::string_view, slice_mode>{"automatic", slice_mode::automatic}};
constexpr std::array neighbor_values{
    std::pair<std::string_view, rule_neighbor_condition>{"any", rule_neighbor_condition::any},
    std::pair<std::string_view, rule_neighbor_condition>{"same-tile", rule_neighbor_condition::same_tile},
    std::pair<std::string_view, rule_neighbor_condition>{"not-same-tile", rule_neighbor_condition::not_same_tile},
    std::pair<std::string_view, rule_neighbor_condition>{"tile", rule_neighbor_condition::tile},
    std::pair<std::string_view, rule_neighbor_condition>{"not-tile", rule_neighbor_condition::not_tile},
    std::pair<std::string_view, rule_neighbor_condition>{"empty", rule_neighbor_condition::empty},
    std::pair<std::string_view, rule_neighbor_condition>{"not-empty", rule_neighbor_condition::not_empty}};
constexpr std::array match_values{
    std::pair<std::string_view, rule_match_transform>{"fixed", rule_match_transform::fixed},
    std::pair<std::string_view, rule_match_transform>{"rotated", rule_match_transform::rotated},
    std::pair<std::string_view, rule_match_transform>{"mirror-x", rule_match_transform::mirror_x},
    std::pair<std::string_view, rule_match_transform>{"mirror-y", rule_match_transform::mirror_y},
    std::pair<std::string_view, rule_match_transform>{"mirror-xy", rule_match_transform::mirror_xy}};
constexpr std::array output_values{
    std::pair<std::string_view, rule_output_kind>{"fixed", rule_output_kind::fixed},
    std::pair<std::string_view, rule_output_kind>{"random", rule_output_kind::random},
    std::pair<std::string_view, rule_output_kind>{"animated", rule_output_kind::animated}};

tile_reference read_tile_reference(const json& value)
{
    return {required_uuid(value, "tileSetId"), required_uuid(value, "tileId")};
}

json tile_reference_json(const tile_reference& value)
{
    return {{"tileSetId", value.tile_set_id.to_string()}, {"tileId", value.tile_id.to_string()}};
}

sprite_reference read_sprite_reference(const json& value)
{
    sprite_reference result;
    result.texture_asset_id = required_uuid(value, "textureAssetId");
    result.source = read_source(value.at("source"));
    result.pivot = value.contains("pivot") ? read_double_point(value, "pivot") : double_point{0.5, 0.5};
    return result;
}

json sprite_reference_json(const sprite_reference& value)
{
    return {{"textureAssetId", value.texture_asset_id.to_string()},
        {"source", source_json(value.source)}, {"pivot", double_point_json(value.pivot)}};
}

animation_frame read_animation_frame(const json& value)
{
    animation_frame result{read_sprite_reference(value.at("sprite")), required_finite(value, "durationSeconds")};
    if (result.duration_seconds <= 0.0) throw std::runtime_error{"Animation frame duration must be positive."};
    return result;
}

json animation_frame_json(const animation_frame& value)
{
    return {{"sprite", sprite_reference_json(value.sprite)}, {"durationSeconds", value.duration_seconds}};
}

std::vector<core::uuid> read_dependencies(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_array()
        || value.at(field).size() > maximum_dependencies)
    {
        throw std::runtime_error{std::string{"Invalid dependency list: "} + field};
    }
    std::vector<core::uuid> result;
    std::set<core::uuid> seen;
    for (const auto& dependency : value.at(field))
    {
        if (!dependency.is_string()) throw std::runtime_error{"Dependency IDs must be strings."};
        const auto parsed = core::uuid::parse(dependency.get<std::string>());
        if (!parsed || !seen.insert(*parsed).second)
        {
            throw std::runtime_error{"Dependency IDs must be valid and unique."};
        }
        result.push_back(*parsed);
    }
    return result;
}

json dependency_json(std::vector<core::uuid> values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    json result = json::array();
    for (const auto& id : values) result.push_back(id.to_string());
    return result;
}

void require_dependency(const std::vector<core::uuid>& dependencies, const core::uuid& id,
    const char* message)
{
    if (id.is_nil() || std::find(dependencies.begin(), dependencies.end(), id) == dependencies.end())
    {
        throw std::runtime_error{message};
    }
}

tile_definition read_tile_definition(const json& item, const tile_set_document& set, std::uint32_t version)
{
    tile_definition tile;
    tile.tile_id = required_uuid(item, "tileId");
    tile.name = required_string(item, "name");
    tile.source = read_source(item.at("source"));
    tile.texture_asset_id = version == 1 ? set.texture_asset_id : required_uuid(item, "textureAssetId");
    tile.pivot = item.contains("pivot") ? read_double_point(item, "pivot") : double_point{0.5, 0.5};
    if (item.contains("collision") && !item.at("collision").is_null())
    {
        const auto& collision = item.at("collision");
        tile.collision = collision_rectangle{required_finite(collision, "offsetX"),
            required_finite(collision, "offsetY"), required_finite(collision, "width"),
            required_finite(collision, "height")};
        if (tile.collision->width <= 0.0 || tile.collision->height <= 0.0)
        {
            throw std::runtime_error{"Collision rectangles must be positive."};
        }
        tile.collider_mode = tile_collider_mode::grid;
    }
    if (version == 2)
    {
        tile.kind = parse_enum(item, "kind", kind_values);
        const auto& collider = item.at("collider");
        tile.collider_mode = parse_enum(collider, "mode", collider_values);
        if (collider.contains("outline"))
        {
            if (!collider.at("outline").is_array() || collider.at("outline").size() > 4096)
            {
                throw std::runtime_error{"Invalid collision outline."};
            }
            for (const auto& point : collider.at("outline"))
            {
                tile.collision_outline.push_back(
                    {required_finite(point, "x"), required_finite(point, "y")});
            }
            if (tile.collider_mode == tile_collider_mode::sprite_outline
                && tile.collision_outline.size() < 3)
            {
                throw std::runtime_error{"Sprite collision outlines require at least three points."};
            }
        }
        if (item.contains("animation"))
        {
            const auto& animation = item.at("animation");
            tile.minimum_speed = required_finite(animation, "minimumSpeed");
            tile.maximum_speed = required_finite(animation, "maximumSpeed");
            tile.animation_start_time = required_finite(animation, "startTime");
            tile.animation_start_frame = animation.value("startFrame", 0U);
            tile.loop_once = animation.value("loopOnce", false);
            tile.pause_animation = animation.value("paused", false);
            tile.update_physics = animation.value("updatePhysics", false);
            if (tile.minimum_speed <= 0.0 || tile.maximum_speed < tile.minimum_speed
                || !animation.at("frames").is_array() || animation.at("frames").size() > maximum_tiles)
            {
                throw std::runtime_error{"Invalid tile animation."};
            }
            for (const auto& frame : animation.at("frames"))
            {
                tile.animation_frames.push_back(read_animation_frame(frame));
            }
        }
        if (item.contains("rules"))
        {
            if (!item.at("rules").is_array() || item.at("rules").size() > maximum_tiles)
            {
                throw std::runtime_error{"Invalid rule list."};
            }
            for (const auto& rule_value : item.at("rules"))
            {
                tile_rule rule;
                rule.topology = parse_enum(rule_value, "topology", layout_values);
                rule.match_transform = parse_enum(rule_value, "matchTransform", match_values);
                rule.output_kind = parse_enum(rule_value, "outputKind", output_values);
                for (const auto& neighbor_value : rule_value.at("neighbors"))
                {
                    rule_neighbor neighbor;
                    neighbor.offset = read_signed_point(neighbor_value, "offset");
                    neighbor.condition = parse_enum(neighbor_value, "condition", neighbor_values);
                    if (neighbor_value.contains("tile") && !neighbor_value.at("tile").is_null())
                    {
                        neighbor.tile = read_tile_reference(neighbor_value.at("tile"));
                    }
                    rule.neighbors.push_back(neighbor);
                }
                for (const auto& output : rule_value.at("outputs"))
                {
                    const auto weight = required_finite(output, "weight");
                    if (weight <= 0.0) throw std::runtime_error{"Rule output weight must be positive."};
                    rule.outputs.push_back({read_tile_reference(output.at("tile")), weight});
                }
                if (rule_value.contains("animation"))
                {
                    for (const auto& frame : rule_value.at("animation"))
                    {
                        rule.animation.push_back(read_animation_frame(frame));
                    }
                }
                tile.rules.push_back(std::move(rule));
            }
        }
        if (item.contains("overrideSource") && !item.at("overrideSource").is_null())
        {
            tile.override_source = read_tile_reference(item.at("overrideSource"));
        }
        if (item.contains("overrides"))
        {
            for (const auto& override_value : item.at("overrides"))
            {
                tile.overrides.push_back({required_uuid(override_value, "sourceTileId"),
                    read_tile_reference(override_value.at("replacement"))});
            }
        }
        if (item.contains("custom"))
        {
            const auto& custom = item.at("custom");
            tile.custom_type_id = required_string(custom, "typeId");
            tile.custom_type_version = custom.at("typeVersion").get<std::uint32_t>();
            if (tile.custom_type_version == 0) throw std::runtime_error{"Custom type version must be positive."};
            tile.opaque_payload_json = canonical_json(custom.at("payload"));
        }
    }
    tile.opaque_fields_json = unknown_fields(item,
        {"tileId", "name", "source", "collision", "kind", "textureAssetId", "pivot",
            "collider", "animation", "rules", "overrideSource", "overrides", "custom"});
    return tile;
}

json tile_definition_json(const tile_definition& tile, const tile_set_document& set)
{
    const auto texture = tile.texture_asset_id.is_nil() ? set.texture_asset_id : tile.texture_asset_id;
    auto collider_mode = tile.collider_mode;
    if (collider_mode == tile_collider_mode::none && tile.collision) collider_mode = tile_collider_mode::grid;
    json collision = nullptr;
    if (tile.collision)
    {
        collision = {{"offsetX", tile.collision->offset_x}, {"offsetY", tile.collision->offset_y},
            {"width", tile.collision->width}, {"height", tile.collision->height}};
    }
    json outline = json::array();
    for (const auto& point : tile.collision_outline) outline.push_back(double_point_json(point));
    json value{{"tileId", tile.tile_id.to_string()}, {"name", tile.name},
        {"kind", enum_name(tile.kind, kind_values)}, {"textureAssetId", texture.to_string()},
        {"source", source_json(tile.source)}, {"pivot", double_point_json(tile.pivot)},
        {"collision", collision},
        {"collider", {{"mode", enum_name(collider_mode, collider_values)}, {"outline", outline}}}};
    if (tile.kind == tile_kind::animated || !tile.animation_frames.empty())
    {
        json frames = json::array();
        for (const auto& frame : tile.animation_frames) frames.push_back(animation_frame_json(frame));
        value["animation"] = {{"minimumSpeed", tile.minimum_speed}, {"maximumSpeed", tile.maximum_speed},
            {"startTime", tile.animation_start_time}, {"startFrame", tile.animation_start_frame},
            {"loopOnce", tile.loop_once}, {"paused", tile.pause_animation},
            {"updatePhysics", tile.update_physics}, {"frames", frames}};
    }
    if (tile.kind == tile_kind::rule || !tile.rules.empty())
    {
        json rules = json::array();
        for (const auto& rule : tile.rules)
        {
            json neighbors = json::array();
            for (const auto& neighbor : rule.neighbors)
            {
                json neighbor_value{{"offset", point_json(neighbor.offset)},
                    {"condition", enum_name(neighbor.condition, neighbor_values)}};
                neighbor_value["tile"] = neighbor.tile ? tile_reference_json(*neighbor.tile) : json(nullptr);
                neighbors.push_back(std::move(neighbor_value));
            }
            json outputs = json::array();
            for (const auto& output : rule.outputs)
            {
                outputs.push_back({{"tile", tile_reference_json(output.tile)}, {"weight", output.weight}});
            }
            json animation = json::array();
            for (const auto& frame : rule.animation) animation.push_back(animation_frame_json(frame));
            rules.push_back({{"topology", enum_name(rule.topology, layout_values)},
                {"matchTransform", enum_name(rule.match_transform, match_values)},
                {"outputKind", enum_name(rule.output_kind, output_values)},
                {"neighbors", neighbors}, {"outputs", outputs}, {"animation", animation}});
        }
        value["rules"] = std::move(rules);
    }
    value["overrideSource"] = tile.override_source
        ? tile_reference_json(*tile.override_source) : json(nullptr);
    if (!tile.overrides.empty())
    {
        json overrides = json::array();
        for (const auto& entry : tile.overrides)
        {
            overrides.push_back({{"sourceTileId", entry.source_tile_id.to_string()},
                {"replacement", tile_reference_json(entry.replacement)}});
        }
        value["overrides"] = std::move(overrides);
    }
    if (tile.kind == tile_kind::custom || !tile.custom_type_id.empty())
    {
        value["custom"] = {{"typeId", tile.custom_type_id},
            {"typeVersion", tile.custom_type_version},
            {"payload", json::parse(tile.opaque_payload_json)}};
    }
    apply_opaque(value, tile.opaque_fields_json);
    return value;
}

tile_grid_settings read_grid(const json& value)
{
    tile_grid_settings result;
    result.layout = parse_enum(value, "layout", layout_values);
    result.cell_size = read_double_point(value, "cellSize");
    result.cell_gap = read_double_point(value, "cellGap");
    result.tile_anchor = read_double_point(value, "tileAnchor");
    if (result.cell_size.x <= 0.0 || result.cell_size.y <= 0.0
        || result.cell_size.x + result.cell_gap.x <= 0.0
        || result.cell_size.y + result.cell_gap.y <= 0.0)
    {
        throw std::runtime_error{"Tilemap grid cell size plus gap must be positive."};
    }
    return result;
}

json grid_json(const tile_grid_settings& value)
{
    return {{"layout", enum_name(value.layout, layout_values)},
        {"cellSize", double_point_json(value.cell_size)},
        {"cellGap", double_point_json(value.cell_gap)},
        {"tileAnchor", double_point_json(value.tile_anchor)}};
}
}

document_result<tile_set_document> read_tile_set(std::string_view encoded)
{
    try
    {
        const auto value = json::parse(encoded);
        const auto version = require_envelope(value, "dpe.tileset", {1U, 2U});
        tile_set_document result;
        result.source_format_version = version;
        result.asset_id = required_uuid(value, "assetId");
        result.name = required_string(value, "name");
        result.texture_asset_id = required_uuid(value, "textureAssetId");
        result.texture_asset_ids = version == 1
            ? std::vector<core::uuid>{result.texture_asset_id}
            : read_dependencies(value, "textureAssetIds");
        if (std::find(result.texture_asset_ids.begin(), result.texture_asset_ids.end(), result.texture_asset_id)
            == result.texture_asset_ids.end())
        {
            throw std::runtime_error{"Primary TileSet texture must be a texture dependency."};
        }
        result.cell_size = read_point(value, "cellSize", true);
        result.margin = read_point(value, "margin", false);
        result.spacing = read_point(value, "spacing", false);
        result.pixels_per_unit = required_finite(value, "pixelsPerUnit");
        if (result.pixels_per_unit <= 0.0) throw std::runtime_error{"pixelsPerUnit must be positive."};
        if (version == 2)
        {
            const auto& slicing = value.at("slicing");
            result.slicing.mode = parse_enum(slicing, "mode", slice_values);
            result.slicing.cell_count = read_point(slicing, "cellCount", true);
            result.slicing.keep_empty_rects = slicing.value("keepEmptyRects", false);
            result.slicing.pivot = read_double_point(slicing, "pivot");
        }
        if (!value.contains("tiles") || !value.at("tiles").is_array()
            || value.at("tiles").size() > maximum_tiles)
        {
            throw std::runtime_error{"Invalid TileSet tile list."};
        }
        std::set<core::uuid> ids;
        for (const auto& item : value.at("tiles"))
        {
            auto tile = read_tile_definition(item, result, version);
            if (!ids.insert(tile.tile_id).second) throw std::runtime_error{"Tile IDs must be unique."};
            require_dependency(result.texture_asset_ids, tile.texture_asset_id,
                "Tile texture must be a TileSet texture dependency.");
            result.tiles.push_back(std::move(tile));
        }
        result.opaque_fields_json = unknown_fields(value,
            {"$schema", "format", "formatVersion", "engineVersion", "assetId", "name",
                "textureAssetId", "textureAssetIds", "cellSize", "margin", "spacing",
                "pixelsPerUnit", "slicing", "tiles"});
        return {std::move(result), {}};
    }
    catch (const std::exception& exception)
    {
        return {std::nullopt, exception.what()};
    }
}

document_result<tilemap_document> read_tilemap(std::string_view encoded)
{
    try
    {
        const auto value = json::parse(encoded);
        const auto version = require_envelope(value, "dpe.tilemap", {1U, 2U});
        if (value.value("chunkSize", 0) != 32) throw std::runtime_error{"Tilemap chunkSize must be 32."};
        tilemap_document result;
        result.source_format_version = version;
        result.asset_id = required_uuid(value, "assetId");
        result.name = required_string(value, "name");
        result.tile_set_dependencies = read_dependencies(value, "tileSetDependencies");
        if (result.tile_set_dependencies.empty()) throw std::runtime_error{"Tilemap requires a TileSet dependency."};
        result.grid = version == 1 ? tile_grid_settings{} : read_grid(value.at("grid"));
        if (!value.contains("layers") || !value.at("layers").is_array()
            || value.at("layers").empty() || value.at("layers").size() > maximum_layers)
        {
            throw std::runtime_error{"Tilemap requires a bounded non-empty layer list."};
        }
        std::set<core::uuid> layer_ids;
        std::set<unsigned> orders;
        std::size_t chunk_count{};
        std::size_t cell_count{};
        for (const auto& item : value.at("layers"))
        {
            tile_layer layer;
            layer.layer_id = required_uuid(item, "layerId");
            layer.name = required_string(item, "name");
            layer.visible = item.at("visible").get<bool>();
            layer.order = item.at("order").get<unsigned>();
            if (!layer_ids.insert(layer.layer_id).second || !orders.insert(layer.order).second)
            {
                throw std::runtime_error{"Layer IDs and order values must be unique."};
            }
            if (version == 2)
            {
                const auto& renderer = item.at("renderer");
                layer.tint = read_color(renderer, "tint");
                layer.material_asset_id = optional_uuid(renderer, "materialAssetId");
                layer.sort_order = renderer.at("sortOrder").get<int>();
                layer.renderer_mode = parse_enum(renderer, "mode", renderer_values);
                layer.animation_rate = required_finite(renderer, "animationRate");
                layer.culling_padding = read_double_point(renderer, "cullingPadding");
                if (layer.animation_rate <= 0.0) throw std::runtime_error{"Layer animation rate must be positive."};
            }
            std::set<std::pair<int, int>> chunk_positions;
            for (const auto& chunk_value : item.at("chunks"))
            {
                if (++chunk_count > maximum_chunks) throw std::runtime_error{"Tilemap has too many chunks."};
                tile_chunk chunk{chunk_value.at("x").get<int>(), chunk_value.at("y").get<int>(), {}};
                if (!chunk_positions.emplace(chunk.x, chunk.y).second)
                {
                    throw std::runtime_error{"Chunk coordinates must be unique per layer."};
                }
                std::set<unsigned> cell_indices;
                for (const auto& cell_value : chunk_value.at("cells"))
                {
                    if (++cell_count > maximum_cells) throw std::runtime_error{"Tilemap has too many cells."};
                    tile_cell cell;
                    cell.index = cell_value.at("index").get<unsigned>();
                    cell.tile_id = required_uuid(cell_value, "tileId");
                    cell.flip_x = cell_value.value("flipX", false);
                    cell.flip_y = cell_value.value("flipY", false);
                    cell.rotation_quarter_turns = cell_value.value("rotationQuarterTurns", 0U);
                    cell.tile_set_id = version == 1
                        ? result.tile_set_dependencies.front() : required_uuid(cell_value, "tileSetId");
                    if (version == 2)
                    {
                        cell.tint = read_color(cell_value, "tint");
                        cell.offset = read_double_point(cell_value, "offset");
                        cell.rotation_degrees = required_finite(cell_value, "rotationDegrees");
                        cell.scale = read_double_point(cell_value, "scale");
                        cell.elevation = cell_value.value("elevation", 0);
                        cell.lock_color = cell_value.value("lockColor", false);
                        cell.lock_transform = cell_value.value("lockTransform", false);
                        if (cell.scale.x == 0.0 || cell.scale.y == 0.0)
                        {
                            throw std::runtime_error{"Tile cell scale cannot be zero."};
                        }
                    }
                    require_dependency(result.tile_set_dependencies, cell.tile_set_id,
                        "Tile cell references an undeclared TileSet.");
                    if (cell.index >= 1024 || cell.rotation_quarter_turns > 3
                        || !cell_indices.insert(cell.index).second)
                    {
                        throw std::runtime_error{"Cells require unique indexes below 1024 and rotations from zero to three."};
                    }
                    cell.opaque_fields_json = unknown_fields(cell_value,
                        {"index", "tileSetId", "tileId", "flipX", "flipY", "rotationQuarterTurns",
                            "tint", "offset", "rotationDegrees", "scale", "elevation",
                            "lockColor", "lockTransform"});
                    chunk.cells.push_back(std::move(cell));
                }
                layer.chunks.push_back(std::move(chunk));
            }
            layer.opaque_fields_json = unknown_fields(item,
                {"layerId", "name", "visible", "order", "renderer", "chunks"});
            result.layers.push_back(std::move(layer));
        }
        result.opaque_fields_json = unknown_fields(value,
            {"$schema", "format", "formatVersion", "engineVersion", "assetId", "name",
                "chunkSize", "tileSetDependencies", "grid", "layers"});
        return {std::move(result), {}};
    }
    catch (const std::exception& exception)
    {
        return {std::nullopt, exception.what()};
    }
}

document_result<tile_palette_document> read_tile_palette(std::string_view encoded)
{
    try
    {
        const auto value = json::parse(encoded);
        require_envelope(value, "dpe.tilepalette", {1U});
        tile_palette_document result;
        result.asset_id = required_uuid(value, "assetId");
        result.name = required_string(value, "name");
        result.tile_set_dependencies = read_dependencies(value, "tileSetDependencies");
        if (result.tile_set_dependencies.empty()) throw std::runtime_error{"TilePalette requires a TileSet dependency."};
        if (!value.contains("cells") || !value.at("cells").is_array()
            || value.at("cells").size() > maximum_cells)
        {
            throw std::runtime_error{"Invalid TilePalette cell list."};
        }
        std::set<std::pair<int, int>> positions;
        for (const auto& item : value.at("cells"))
        {
            tile_palette_cell cell;
            cell.u = item.at("u").get<int>();
            cell.v = item.at("v").get<int>();
            if (!positions.emplace(cell.u, cell.v).second)
            {
                throw std::runtime_error{"TilePalette cell coordinates must be unique."};
            }
            cell.tile = read_tile_reference(item.at("tile"));
            require_dependency(result.tile_set_dependencies, cell.tile.tile_set_id,
                "TilePalette cell references an undeclared TileSet.");
            cell.flip_x = item.value("flipX", false);
            cell.flip_y = item.value("flipY", false);
            cell.rotation_quarter_turns = item.value("rotationQuarterTurns", 0U);
            if (cell.rotation_quarter_turns > 3) throw std::runtime_error{"TilePalette rotation is invalid."};
            cell.tint = read_color(item, "tint");
            result.cells.push_back(cell);
        }
        result.opaque_fields_json = unknown_fields(value,
            {"$schema", "format", "formatVersion", "engineVersion", "assetId", "name",
                "tileSetDependencies", "cells"});
        return {std::move(result), {}};
    }
    catch (const std::exception& exception)
    {
        return {std::nullopt, exception.what()};
    }
}

std::string write_tile_set(const tile_set_document& document)
{
    auto textures = document.texture_asset_ids;
    if (std::find(textures.begin(), textures.end(), document.texture_asset_id) == textures.end())
    {
        textures.push_back(document.texture_asset_id);
    }
    auto tiles = document.tiles;
    std::sort(tiles.begin(), tiles.end(), [](const auto& left, const auto& right) {
        return left.tile_id < right.tile_id;
    });
    json values = json::array();
    for (const auto& tile : tiles) values.push_back(tile_definition_json(tile, document));
    json result{{"$schema", "https://dragonpixel.dev/schemas/v2/tileset.schema.json"},
        {"format", "dpe.tileset"}, {"formatVersion", 2}, {"engineVersion", "0.4.0-tilemap"},
        {"assetId", document.asset_id.to_string()}, {"name", document.name},
        {"textureAssetId", document.texture_asset_id.to_string()},
        {"textureAssetIds", dependency_json(std::move(textures))},
        {"cellSize", point_json(document.cell_size)}, {"margin", point_json(document.margin)},
        {"spacing", point_json(document.spacing)}, {"pixelsPerUnit", document.pixels_per_unit},
        {"slicing", {{"mode", enum_name(document.slicing.mode, slice_values)},
            {"cellCount", point_json(document.slicing.cell_count)},
            {"keepEmptyRects", document.slicing.keep_empty_rects},
            {"pivot", double_point_json(document.slicing.pivot)}}},
        {"tiles", values}};
    apply_opaque(result, document.opaque_fields_json);
    return result.dump(2) + "\n";
}

std::string write_tilemap(const tilemap_document& document)
{
    auto layers = document.layers;
    std::sort(layers.begin(), layers.end(), [](const auto& left, const auto& right) {
        return left.order < right.order;
    });
    json layer_values = json::array();
    for (auto& layer : layers)
    {
        std::sort(layer.chunks.begin(), layer.chunks.end(), [](const auto& left, const auto& right) {
            return left.y != right.y ? left.y < right.y : left.x < right.x;
        });
        json chunks = json::array();
        for (auto& chunk : layer.chunks)
        {
            std::sort(chunk.cells.begin(), chunk.cells.end(), [](const auto& left, const auto& right) {
                return left.index < right.index;
            });
            json cells = json::array();
            for (const auto& cell : chunk.cells)
            {
                auto tile_set_id = cell.tile_set_id;
                if (tile_set_id.is_nil() && !document.tile_set_dependencies.empty())
                {
                    tile_set_id = document.tile_set_dependencies.front();
                }
                json cell_value{{"index", cell.index}, {"tileSetId", tile_set_id.to_string()},
                    {"tileId", cell.tile_id.to_string()}, {"flipX", cell.flip_x},
                    {"flipY", cell.flip_y}, {"rotationQuarterTurns", cell.rotation_quarter_turns},
                    {"tint", color_json(cell.tint)}, {"offset", double_point_json(cell.offset)},
                    {"rotationDegrees", cell.rotation_degrees}, {"scale", double_point_json(cell.scale)},
                    {"elevation", cell.elevation}, {"lockColor", cell.lock_color},
                    {"lockTransform", cell.lock_transform}};
                apply_opaque(cell_value, cell.opaque_fields_json);
                cells.push_back(std::move(cell_value));
            }
            chunks.push_back({{"x", chunk.x}, {"y", chunk.y}, {"cells", cells}});
        }
        json renderer{{"tint", color_json(layer.tint)},
            {"materialAssetId", layer.material_asset_id.is_nil()
                    ? json(nullptr) : json(layer.material_asset_id.to_string())},
            {"sortOrder", layer.sort_order}, {"mode", enum_name(layer.renderer_mode, renderer_values)},
            {"animationRate", layer.animation_rate},
            {"cullingPadding", double_point_json(layer.culling_padding)}};
        json layer_value{{"layerId", layer.layer_id.to_string()}, {"name", layer.name},
            {"visible", layer.visible}, {"order", layer.order}, {"renderer", renderer}, {"chunks", chunks}};
        apply_opaque(layer_value, layer.opaque_fields_json);
        layer_values.push_back(std::move(layer_value));
    }
    json result{{"$schema", "https://dragonpixel.dev/schemas/v2/tilemap.schema.json"},
        {"format", "dpe.tilemap"}, {"formatVersion", 2}, {"engineVersion", "0.4.0-tilemap"},
        {"assetId", document.asset_id.to_string()}, {"name", document.name}, {"chunkSize", 32},
        {"tileSetDependencies", dependency_json(document.tile_set_dependencies)},
        {"grid", grid_json(document.grid)}, {"layers", layer_values}};
    apply_opaque(result, document.opaque_fields_json);
    return result.dump(2) + "\n";
}

std::string write_tile_palette(const tile_palette_document& document)
{
    auto cells = document.cells;
    std::sort(cells.begin(), cells.end(), [](const auto& left, const auto& right) {
        return left.v != right.v ? left.v < right.v : left.u < right.u;
    });
    json cell_values = json::array();
    for (const auto& cell : cells)
    {
        cell_values.push_back({{"u", cell.u}, {"v", cell.v},
            {"tile", tile_reference_json(cell.tile)}, {"flipX", cell.flip_x}, {"flipY", cell.flip_y},
            {"rotationQuarterTurns", cell.rotation_quarter_turns}, {"tint", color_json(cell.tint)}});
    }
    json result{{"$schema", "https://dragonpixel.dev/schemas/v1/tilepalette.schema.json"},
        {"format", "dpe.tilepalette"}, {"formatVersion", 1}, {"engineVersion", "0.4.0-tilemap"},
        {"assetId", document.asset_id.to_string()}, {"name", document.name},
        {"tileSetDependencies", dependency_json(document.tile_set_dependencies)}, {"cells", cell_values}};
    apply_opaque(result, document.opaque_fields_json);
    return result.dump(2) + "\n";
}
}
