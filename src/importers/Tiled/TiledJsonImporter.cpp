#include "TiledJsonImporter.h"

#include <dragonpixel/tiles/tile_documents.h>

#include <QByteArray>
#include <QCryptographicHash>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace dragonpixel::importers::tiled
{
namespace
{
using json = nlohmann::ordered_json;

constexpr std::uint32_t flip_horizontal = 0x80000000U;
constexpr std::uint32_t flip_vertical = 0x40000000U;
constexpr std::uint32_t flip_diagonal = 0x20000000U;
constexpr std::uint32_t reserved_high_bit = 0x10000000U;
constexpr std::uint32_t gid_mask = 0x0fffffffU;
constexpr std::uintmax_t max_map_bytes = 16U * 1024U * 1024U;
constexpr std::uintmax_t max_tileset_bytes = 4U * 1024U * 1024U;
constexpr std::uintmax_t max_texture_bytes = 64U * 1024U * 1024U;
constexpr std::size_t max_tiles = 65536U;
constexpr std::size_t max_layers = 256U;
constexpr std::size_t max_chunks = 65536U;
constexpr std::size_t max_cells = 4U * 1024U * 1024U;
constexpr int max_coordinate = 1'000'000;

class import_error final : public std::runtime_error
{
public:
    import_error(std::string code, std::string message, std::filesystem::path path = {})
        : std::runtime_error(std::move(message)), code_(std::move(code)), path_(std::move(path))
    {
    }

    [[nodiscard]] const std::string& code() const noexcept { return code_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::string code_;
    std::filesystem::path path_;
};

std::string read_file(
    const std::filesystem::path& path,
    std::uintmax_t maximum,
    std::string_view role)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
    {
        throw import_error{"DPE-TILED-SOURCE", std::string{role} + " must be a regular non-link file.", path};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum)
    {
        throw import_error{"DPE-TILED-SOURCE-SIZE", std::string{role} + " exceeds the supported size limit.", path};
    }
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
    {
        throw import_error{"DPE-TILED-SOURCE-READ", std::string{"Could not read "} + std::string{role} + '.', path};
    }
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

json parse_json_file(
    const std::filesystem::path& path,
    std::uintmax_t maximum,
    std::string_view role)
{
    try
    {
        return json::parse(read_file(path, maximum, role));
    }
    catch (const import_error&)
    {
        throw;
    }
    catch (const std::exception& exception)
    {
        throw import_error{"DPE-TILED-JSON", std::string{role} + " is not valid JSON: " + exception.what(), path};
    }
}

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    const auto normalized_root = root.lexically_normal();
    const auto normalized_candidate = candidate.lexically_normal();
    auto root_part = normalized_root.begin();
    auto candidate_part = normalized_candidate.begin();
    for (; root_part != normalized_root.end(); ++root_part, ++candidate_part)
    {
        if (candidate_part == normalized_candidate.end() || *root_part != *candidate_part)
        {
            return false;
        }
    }
    return true;
}

std::filesystem::path contained_dependency(
    const std::filesystem::path& source_root,
    const std::filesystem::path& relative_to,
    const std::string& encoded,
    std::string_view role)
{
    const std::filesystem::path relative{encoded};
    if (encoded.empty() || relative.is_absolute())
    {
        throw import_error{"DPE-TILED-DEPENDENCY-PATH", std::string{role} + " path must be relative and non-empty."};
    }
    std::error_code error;
    const auto candidate = std::filesystem::weakly_canonical(relative_to / relative, error);
    if (error || !path_is_within(source_root, candidate))
    {
        throw import_error{"DPE-TILED-DEPENDENCY-ESCAPE", std::string{role} + " escapes the selected map directory.", candidate};
    }
    return candidate;
}

core::uuid stable_uuid(const core::uuid& namespace_id, std::string_view discriminator)
{
    QByteArray input{reinterpret_cast<const char*>(namespace_id.bytes().data()), 16};
    input.append(':');
    input.append(discriminator.data(), static_cast<qsizetype>(discriminator.size()));
    const auto digest = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
    std::array<std::uint8_t, 16> bytes{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()), bytes.size(), bytes.begin());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x50U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return core::uuid{bytes};
}

int required_positive_int(const json& value, const char* field, const std::filesystem::path& path)
{
    if (!value.contains(field) || !value.at(field).is_number_integer())
    {
        throw import_error{"DPE-TILED-FIELD", std::string{"Missing integer field: "} + field, path};
    }
    const auto result = value.at(field).get<long long>();
    if (result <= 0 || result > max_coordinate)
    {
        throw import_error{"DPE-TILED-LIMIT", std::string{"Field is outside the supported range: "} + field, path};
    }
    return static_cast<int>(result);
}

int optional_int(const json& value, const char* field, int fallback, const std::filesystem::path& path)
{
    if (!value.contains(field)) return fallback;
    if (!value.at(field).is_number_integer())
    {
        throw import_error{"DPE-TILED-FIELD", std::string{"Field must be an integer: "} + field, path};
    }
    const auto result = value.at(field).get<long long>();
    if (result < -max_coordinate || result > max_coordinate)
    {
        throw import_error{"DPE-TILED-LIMIT", std::string{"Field is outside the supported range: "} + field, path};
    }
    return static_cast<int>(result);
}

std::string optional_name(const json& value, std::string fallback)
{
    if (!value.contains("name") || !value.at("name").is_string()) return fallback;
    auto result = value.at("name").get<std::string>();
    if (result.empty()) return fallback;
    if (result.size() > 256U)
    {
        throw import_error{"DPE-TILED-LIMIT", "A Tiled name exceeds 256 UTF-8 bytes."};
    }
    return result;
}

int floor_div_32(int value)
{
    return value >= 0 ? value / 32 : -(((-value) + 31) / 32);
}

struct transform_flags final
{
    bool flip_x{};
    bool flip_y{};
    unsigned rotation{};
    double rotation_degrees{};
};

transform_flags convert_flags(std::uint32_t gid, tiles::grid_layout layout)
{
    const auto horizontal = (gid & flip_horizontal) != 0;
    const auto vertical = (gid & flip_vertical) != 0;
    const auto diagonal = (gid & flip_diagonal) != 0;
    if (layout == tiles::grid_layout::hex_point_top
        || layout == tiles::grid_layout::hex_flat_top)
    {
        return {horizontal, vertical, 0U,
            (diagonal ? 60.0 : 0.0) + ((gid & reserved_high_bit) != 0 ? 120.0 : 0.0)};
    }
    if (!diagonal)
    {
        return {horizontal, vertical, 0U};
    }
    if (horizontal && vertical) return {true, false, 1U};
    if (horizontal) return {false, false, 1U};
    if (vertical) return {false, false, 3U};
    return {true, false, 3U};
}

struct imported_tile_set final
{
    std::uint32_t first_gid{};
    tiles::tile_set_document document;
    std::filesystem::path source_texture;
    std::string texture_bytes;
};

struct coordinate_conversion final
{
    tiles::grid_layout layout{tiles::grid_layout::rectangular};
    std::string orientation{"orthogonal"};
    char stagger_axis{'y'};
    bool stagger_odd{true};
};

tiles::integer_point convert_coordinate(
    int x, int y, const coordinate_conversion& conversion)
{
    if (conversion.orientation == "staggered")
    {
        const auto axis_value = conversion.stagger_axis == 'y' ? y : x;
        const auto shifted = ((axis_value & 1) != 0) == conversion.stagger_odd;
        const auto origin_shift = conversion.stagger_odd ? 0 : 1;
        const auto horizontal = conversion.stagger_axis == 'y'
            ? 2 * x + (shifted ? 1 : 0) - origin_shift : x;
        const auto vertical = conversion.stagger_axis == 'y'
            ? y : 2 * y + (shifted ? 1 : 0) - origin_shift;
        return {(horizontal - vertical) / 2, (-horizontal - vertical) / 2};
    }
    if (conversion.layout == tiles::grid_layout::hex_point_top)
    {
        const auto shifted = ((y & 1) != 0) == conversion.stagger_odd;
        const auto q = x - ((y + (shifted ? 1 : 0)) / 2);
        return {q, -y};
    }
    if (conversion.layout == tiles::grid_layout::hex_flat_top)
    {
        const auto shifted = ((x & 1) != 0) == conversion.stagger_odd;
        const auto r = y - ((x + (shifted ? 1 : 0)) / 2);
        return {x, -r};
    }
    return {x, -y};
}

void add_cell(
    tiles::tile_layer& layer,
    std::set<std::pair<int, int>>& occupied,
    int tiled_x,
    int tiled_y,
    std::uint64_t encoded_gid,
    const coordinate_conversion& conversion,
    const std::vector<imported_tile_set>& tile_sets,
    std::size_t& total_cells)
{
    if (encoded_gid > std::numeric_limits<std::uint32_t>::max())
    {
        throw import_error{"DPE-TILED-GID", "A Tiled global tile ID exceeds 32 bits."};
    }
    const auto gid = static_cast<std::uint32_t>(encoded_gid);
    const auto plain_gid = gid & gid_mask;
    if (plain_gid == 0U)
    {
        if ((gid & ~gid_mask) != 0U)
            throw import_error{"DPE-TILED-GID", "An empty Tiled cell contains transformation flags."};
        return;
    }
    const auto owner = std::upper_bound(tile_sets.cbegin(), tile_sets.cend(), plain_gid,
        [](std::uint32_t value, const auto& candidate) { return value < candidate.first_gid; });
    if (owner == tile_sets.cbegin())
        throw import_error{"DPE-TILED-GID", "A Tiled global tile ID precedes every imported TileSet."};
    const auto& tile_set = *std::prev(owner);
    const auto local_id = static_cast<std::size_t>(plain_gid - tile_set.first_gid);
    if (local_id >= tile_set.document.tiles.size())
    {
        throw import_error{"DPE-TILED-GID", "A Tiled global tile ID does not resolve to the imported TileSet."};
    }
    if (tiled_x < -max_coordinate || tiled_x > max_coordinate
        || tiled_y < -max_coordinate || tiled_y > max_coordinate)
    {
        throw import_error{"DPE-TILED-LIMIT", "A Tiled cell coordinate exceeds the supported range."};
    }
    const auto converted = convert_coordinate(tiled_x, tiled_y, conversion);
    const auto x = converted.x;
    const auto y = converted.y;
    if (!occupied.emplace(x, y).second)
    {
        throw import_error{"DPE-TILED-CELL-DUPLICATE", "Tiled chunks contain the same cell coordinate more than once."};
    }
    const auto chunk_x = floor_div_32(x);
    const auto chunk_y = floor_div_32(y);
    const auto local_x = x - (chunk_x * 32);
    const auto local_y = y - (chunk_y * 32);
    const auto index = static_cast<unsigned>((local_y * 32) + local_x);
    auto chunk = std::find_if(layer.chunks.begin(), layer.chunks.end(), [&](const auto& value) {
        return value.x == chunk_x && value.y == chunk_y;
    });
    if (chunk == layer.chunks.end())
    {
        if (layer.chunks.size() >= max_chunks)
            throw import_error{"DPE-TILED-LIMIT", "The imported map exceeds the chunk limit."};
        layer.chunks.push_back({chunk_x, chunk_y, {}});
        chunk = std::prev(layer.chunks.end());
    }
    const auto flags = convert_flags(gid, conversion.layout);
    tiles::tile_cell cell;
    cell.index = index;
    cell.tile_id = tile_set.document.tiles.at(local_id).tile_id;
    cell.tile_set_id = tile_set.document.asset_id;
    cell.flip_x = flags.flip_x;
    cell.flip_y = flags.flip_y;
    cell.rotation_quarter_turns = flags.rotation;
    cell.rotation_degrees = flags.rotation_degrees;
    chunk->cells.push_back(std::move(cell));
    ++total_cells;
    if (total_cells > max_cells)
        throw import_error{"DPE-TILED-LIMIT", "The imported map exceeds the non-empty cell limit."};
}

void add_data_array(
    tiles::tile_layer& layer,
    std::set<std::pair<int, int>>& occupied,
    const json& data,
    int origin_x,
    int origin_y,
    int width,
    int height,
    const coordinate_conversion& conversion,
    const std::vector<imported_tile_set>& tile_sets,
    std::size_t& total_cells)
{
    if (!data.is_array() || data.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        throw import_error{"DPE-TILED-LAYER-DATA", "Tile layer data must be a native JSON array matching its width and height."};
    }
    for (std::size_t index = 0; index < data.size(); ++index)
    {
        if (!data.at(index).is_number_unsigned() && !data.at(index).is_number_integer())
            throw import_error{"DPE-TILED-LAYER-DATA", "Tile layer GIDs must be integers."};
        const auto value = data.at(index).get<std::uint64_t>();
        const auto x = origin_x + static_cast<int>(index % static_cast<std::size_t>(width));
        const auto y = origin_y + static_cast<int>(index / static_cast<std::size_t>(width));
        add_cell(layer, occupied, x, y, value, conversion, tile_sets, total_cells);
    }
}

void validate_output_directory(const std::filesystem::path& staging)
{
    if (staging.empty())
        throw import_error{"DPE-TILED-STAGING", "The importer staging directory is required."};
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    const auto status = std::filesystem::symlink_status(staging, error);
    if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
        throw import_error{"DPE-TILED-STAGING", "The importer staging path must be a non-link directory.", staging};
    for (const auto& entry : std::filesystem::directory_iterator(staging, error))
    {
        if (error) break;
        throw import_error{"DPE-TILED-STAGING-COLLISION",
            "The importer staging directory already contains an output.", entry.path()};
    }
    if (error) throw import_error{"DPE-TILED-STAGING", "The importer staging directory could not be inspected.", staging};
}

void write_file(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream || !stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !stream.flush())
        throw import_error{"DPE-TILED-STAGING-WRITE", "Could not write a staged importer output.", path};
}

std::string required_string(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_string() || value.at(field).get<std::string>().empty())
        throw import_error{"DPE-TILED-REQUEST", std::string{"Missing string request field: "} + field};
    return value.at(field).get<std::string>();
}

core::uuid required_uuid(const json& value, const char* field)
{
    const auto parsed = core::uuid::parse(required_string(value, field));
    if (!parsed) throw import_error{"DPE-TILED-REQUEST", std::string{"Invalid UUID request field: "} + field};
    return *parsed;
}

json result_json(const import_request& request, const import_result& result)
{
    json diagnostics = json::array();
    for (const auto& item : result.diagnostics)
    {
        diagnostics.push_back({
            {"severity", "error"},
            {"code", item.code},
            {"message", item.message},
            {"path", item.path.generic_string()},
        });
    }
    json outputs = json::array();
    if (result.succeeded)
    {
        outputs.push_back({{"role", "tilemap"}, {"path", result.tilemap_path.filename().generic_string()},
            {"assetId", request.tilemap_asset_id.to_string()}});
        for (std::size_t index = 0; index < result.tileset_paths.size(); ++index)
            outputs.push_back({{"role", "tileset"}, {"index", index},
                {"path", result.tileset_paths[index].filename().generic_string()},
                {"assetId", result.tileset_asset_ids[index].to_string()}});
        for (std::size_t index = 0; index < result.texture_paths.size(); ++index)
            outputs.push_back({{"role", "texture"}, {"index", index},
                {"path", result.texture_paths[index].filename().generic_string()},
                {"assetId", result.texture_asset_ids[index].to_string()}});
    }
    return {
        {"format", "dpe.tile-import.result"},
        {"formatVersion", 2},
        {"importer", "dragonpixel.tiled-json"},
        {"succeeded", result.succeeded},
        {"outputs", std::move(outputs)},
        {"diagnostics", std::move(diagnostics)},
        {"statistics", {{"tiles", result.tile_count}, {"layers", result.layer_count}, {"cells", result.cell_count}}},
    };
}
}

import_result import_tiled_json(const import_request& request)
{
    import_result result;
    try
    {
        if (request.name.empty() || request.name.size() > 128U
            || !std::isfinite(request.pixels_per_unit) || request.pixels_per_unit <= 0.0)
        {
            throw import_error{"DPE-TILED-REQUEST", "The import name and a positive finite pixels-per-unit value are required."};
        }
        validate_output_directory(request.staging_directory);
        std::error_code filesystem_error;
        const auto source_map = std::filesystem::weakly_canonical(request.source_map, filesystem_error);
        if (filesystem_error)
            throw import_error{"DPE-TILED-SOURCE", "The selected Tiled map path could not be resolved.", request.source_map};
        const auto source_root = source_map.parent_path();
        const auto map = parse_json_file(source_map, max_map_bytes, "Tiled map");
        if (!map.is_object())
            throw import_error{"DPE-TILED-TYPE", "The selected JSON document is not a Tiled map.", source_map};
        const auto orientation = map.value("orientation", std::string{});
        if (orientation != "orthogonal" && orientation != "isometric"
            && orientation != "staggered" && orientation != "hexagonal")
            throw import_error{"DPE-TILED-ORIENTATION",
                "Only orthogonal, isometric, staggered, and hexagonal Tiled JSON maps are supported.", source_map};
        if (map.contains("type") && map.value("type", std::string{}) != "map")
            throw import_error{"DPE-TILED-TYPE", "The selected JSON document is not a Tiled map.", source_map};
        const auto map_tile_width = required_positive_int(map, "tilewidth", source_map);
        const auto map_tile_height = required_positive_int(map, "tileheight", source_map);
        coordinate_conversion conversion;
        conversion.orientation = orientation;
        conversion.layout = orientation == "isometric" || orientation == "staggered"
            ? (request.import_isometric_as_z_as_y ? tiles::grid_layout::isometric_z_as_y
                                                  : tiles::grid_layout::isometric)
            : tiles::grid_layout::rectangular;
        if (orientation == "staggered" || orientation == "hexagonal")
        {
            const auto axis = map.value("staggeraxis", std::string{});
            const auto index = map.value("staggerindex", std::string{});
            if ((axis != "x" && axis != "y") || (index != "odd" && index != "even"))
                throw import_error{"DPE-TILED-ORIENTATION",
                    "Staggered and hexagonal maps require supported staggeraxis and staggerindex values.", source_map};
            conversion.stagger_axis = axis.front();
            conversion.stagger_odd = index == "odd";
            if (orientation == "hexagonal")
                conversion.layout = axis == "y" ? tiles::grid_layout::hex_point_top
                                                  : tiles::grid_layout::hex_flat_top;
        }
        if (!map.contains("tilesets") || !map.at("tilesets").is_array()
            || map.at("tilesets").empty() || map.at("tilesets").size() > 256U)
            throw import_error{"DPE-TILED-TILESETS", "The map requires between one and 256 atlas TileSets.", source_map};

        std::vector<imported_tile_set> imported_sets;
        imported_sets.reserve(map.at("tilesets").size());
        std::size_t total_tile_count{};
        for (std::size_t set_index = 0; set_index < map.at("tilesets").size(); ++set_index)
        {
            const auto& map_tileset = map.at("tilesets").at(set_index);
            if (!map_tileset.is_object() || !map_tileset.contains("firstgid")
                || !map_tileset.at("firstgid").is_number_integer())
                throw import_error{"DPE-TILED-TILESET", "Every Tiled map TileSet requires a numeric firstgid.", source_map};
            const auto first_gid_value = map_tileset.at("firstgid").get<long long>();
            if (first_gid_value <= 0 || first_gid_value > static_cast<long long>(gid_mask)
                || (!imported_sets.empty() && first_gid_value <= imported_sets.back().first_gid))
                throw import_error{"DPE-TILED-TILESET", "Tiled TileSet firstgid values must be positive and increasing.", source_map};
            auto source_set = map_tileset;
            auto tileset_path = source_map;
            if (map_tileset.contains("source"))
            {
                if (!map_tileset.at("source").is_string())
                    throw import_error{"DPE-TILED-TILESET", "An external TileSet source must be a relative string.", source_map};
                tileset_path = contained_dependency(source_root, source_root,
                    map_tileset.at("source").get<std::string>(), "External TileSet");
                source_set = parse_json_file(tileset_path, max_tileset_bytes, "Tiled TileSet");
            }
            if (!source_set.is_object() || (source_set.contains("type")
                    && source_set.value("type", std::string{}) != "tileset"))
                throw import_error{"DPE-TILED-TILESET", "A referenced JSON document is not a Tiled TileSet.", tileset_path};
            const auto tile_width = required_positive_int(source_set, "tilewidth", tileset_path);
            const auto tile_height = required_positive_int(source_set, "tileheight", tileset_path);
            const auto tile_count = required_positive_int(source_set, "tilecount", tileset_path);
            const auto columns = required_positive_int(source_set, "columns", tileset_path);
            if (tile_count > static_cast<int>(max_tiles) || columns > tile_count
                || total_tile_count + static_cast<std::size_t>(tile_count) > max_tiles)
                throw import_error{"DPE-TILED-LIMIT", "The combined Tiled TileSets exceed the supported tile limit.", tileset_path};
            const auto margin = optional_int(source_set, "margin", 0, tileset_path);
            const auto spacing = optional_int(source_set, "spacing", 0, tileset_path);
            if (margin < 0 || spacing < 0)
                throw import_error{"DPE-TILED-TILESET", "TileSet margin and spacing must be non-negative.", tileset_path};
            if (!source_set.contains("image") || !source_set.at("image").is_string())
                throw import_error{"DPE-TILED-IMAGE", "Image-collection TileSets are not supported.", tileset_path};
            const auto texture_path = contained_dependency(source_root, tileset_path.parent_path(),
                source_set.at("image").get<std::string>(), "TileSet image");
            if (texture_path.extension().string() != ".png" && texture_path.extension().string() != ".PNG")
                throw import_error{"DPE-TILED-IMAGE", "Every TileSet atlas must be a PNG image.", texture_path};
            const auto set_id = set_index == 0 ? request.tileset_asset_id
                : stable_uuid(request.tileset_asset_id, "tileset:" + std::to_string(set_index));
            const auto texture_id = set_index == 0 ? request.texture_asset_id
                : stable_uuid(request.texture_asset_id, "texture:" + std::to_string(set_index));
            imported_tile_set imported;
            imported.first_gid = static_cast<std::uint32_t>(first_gid_value);
            imported.source_texture = texture_path;
            imported.texture_bytes = read_file(texture_path, max_texture_bytes, "TileSet PNG");
            auto& output_set = imported.document;
            output_set.asset_id = set_id;
            output_set.name = optional_name(source_set, request.name + " Tiles " + std::to_string(set_index + 1));
            output_set.texture_asset_id = texture_id;
            output_set.texture_asset_ids = {texture_id};
            output_set.cell_size = {tile_width, tile_height};
            output_set.margin = {margin, margin};
            output_set.spacing = {spacing, spacing};
            output_set.pixels_per_unit = request.pixels_per_unit;
            std::map<int, json> explicit_tiles;
            if (source_set.contains("tiles"))
            {
                if (!source_set.at("tiles").is_array())
                    throw import_error{"DPE-TILED-TILESET", "TileSet tile definitions must be an array.", tileset_path};
                for (const auto& tile : source_set.at("tiles"))
                {
                    if (!tile.is_object() || !tile.contains("id") || !tile.at("id").is_number_integer())
                        throw import_error{"DPE-TILED-TILESET", "Each Tiled tile definition requires an integer id.", tileset_path};
                    const auto id = tile.at("id").get<int>();
                    if (id < 0 || id >= tile_count || !explicit_tiles.emplace(id, tile).second)
                        throw import_error{"DPE-TILED-TILESET", "Tiled tile IDs must be unique and inside tilecount.", tileset_path};
                }
            }
            output_set.tiles.reserve(static_cast<std::size_t>(tile_count));
            for (int id = 0; id < tile_count; ++id)
            {
                const auto column = id % columns;
                const auto row = id / columns;
                const auto explicit_tile = explicit_tiles.find(id);
                tiles::tile_definition definition;
                definition.tile_id = stable_uuid(set_id, "tile:" + std::to_string(id));
                definition.name = explicit_tile == explicit_tiles.end()
                    ? output_set.name + " " + std::to_string(id)
                    : optional_name(explicit_tile->second, output_set.name + " " + std::to_string(id));
                definition.source = {margin + column * (tile_width + spacing),
                    margin + row * (tile_height + spacing), tile_width, tile_height};
                definition.texture_asset_id = texture_id;
                output_set.tiles.push_back(std::move(definition));
            }
            for (const auto& [id, tile] : explicit_tiles)
            {
                if (!tile.contains("animation")) continue;
                if (!tile.at("animation").is_array() || tile.at("animation").empty())
                    throw import_error{"DPE-TILED-ANIMATION", "Tiled animation frames must be a non-empty array.", tileset_path};
                auto& definition = output_set.tiles.at(static_cast<std::size_t>(id));
                definition.kind = tiles::tile_kind::animated;
                for (const auto& frame : tile.at("animation"))
                {
                    if (!frame.is_object() || !frame.contains("tileid") || !frame.at("tileid").is_number_integer()
                        || !frame.contains("duration") || !frame.at("duration").is_number_integer())
                        throw import_error{"DPE-TILED-ANIMATION", "Every animation frame requires integer tileid and duration.", tileset_path};
                    const auto frame_id = frame.at("tileid").get<int>();
                    const auto duration = frame.at("duration").get<int>();
                    if (frame_id < 0 || frame_id >= tile_count || duration <= 0)
                        throw import_error{"DPE-TILED-ANIMATION", "Animation frame values are outside the supported range.", tileset_path};
                    const auto& frame_tile = output_set.tiles.at(static_cast<std::size_t>(frame_id));
                    definition.animation_frames.push_back({
                        {texture_id, frame_tile.source, frame_tile.pivot},
                        static_cast<double>(duration) / 1000.0});
                }
            }
            const auto make_rule_tile = [&](int tile_id, const json& connectivity,
                                            const std::filesystem::path& source_path) {
                if (tile_id < 0 || tile_id >= tile_count || !connectivity.is_array()
                    || connectivity.size() != 8U)
                    throw import_error{"DPE-TILED-WANG-UNSUPPORTED",
                        "A Wang tile uses an unsupported tile id or connectivity shape.", source_path};
                int color{};
                for (const auto& value : connectivity)
                {
                    if (!value.is_number_integer() || value.get<int>() < 0)
                        throw import_error{"DPE-TILED-WANG-UNSUPPORTED",
                            "Wang connectivity entries must be non-negative integers.", source_path};
                    const auto current = value.get<int>();
                    if (current != 0 && color != 0 && current != color)
                        throw import_error{"DPE-TILED-WANG-UNSUPPORTED",
                            "Multi-color Wang transitions cannot be represented by the built-in Rule Tile.", source_path};
                    if (current != 0) color = current;
                }
                auto& definition = output_set.tiles.at(static_cast<std::size_t>(tile_id));
                if (definition.kind != tiles::tile_kind::basic)
                    throw import_error{"DPE-TILED-WANG-UNSUPPORTED",
                        "One Tiled tile cannot be both animated and a terrain Rule Tile.", source_path};
                definition.kind = tiles::tile_kind::rule;
                tiles::tile_rule rule;
                rule.topology = conversion.layout;
                rule.output_kind = tiles::rule_output_kind::fixed;
                rule.outputs.push_back({{set_id, definition.tile_id}, 1.0});
                static constexpr std::array<tiles::integer_point, 8> offsets{{
                    {0, 1}, {1, 1}, {1, 0}, {1, -1},
                    {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}}};
                for (std::size_t index = 0; index < connectivity.size(); ++index)
                {
                    if (connectivity.at(index).get<int>() != 0)
                        rule.neighbors.push_back({offsets[index],
                            tiles::rule_neighbor_condition::same_tile, std::nullopt});
                }
                definition.rules.push_back(std::move(rule));
            };
            if (source_set.contains("wangsets"))
            {
                if (!source_set.at("wangsets").is_array())
                    throw import_error{"DPE-TILED-WANG-UNSUPPORTED", "Tiled wangsets must be an array.", tileset_path};
                for (const auto& wang_set : source_set.at("wangsets"))
                {
                    if (!wang_set.is_object() || !wang_set.contains("wangtiles")
                        || !wang_set.at("wangtiles").is_array())
                        throw import_error{"DPE-TILED-WANG-UNSUPPORTED", "A Wang set is malformed.", tileset_path};
                    if (wang_set.at("wangtiles").size() != 1U)
                        throw import_error{"DPE-TILED-WANG-UNSUPPORTED",
                            "Only single-tile, single-color Wang sets are representable by the built-in Rule Tile.", tileset_path};
                    const auto& wang_tile = wang_set.at("wangtiles").front();
                    if (!wang_tile.is_object() || !wang_tile.contains("tileid")
                        || !wang_tile.at("tileid").is_number_integer() || !wang_tile.contains("wangid"))
                        throw import_error{"DPE-TILED-WANG-UNSUPPORTED", "A Wang tile is malformed.", tileset_path};
                    make_rule_tile(wang_tile.at("tileid").get<int>(), wang_tile.at("wangid"), tileset_path);
                }
            }
            if (source_set.contains("terrains"))
            {
                std::set<int> seen_terrains;
                for (const auto& [id, tile] : explicit_tiles)
                {
                    if (!tile.contains("terrain")) continue;
                    const auto& terrain = tile.at("terrain");
                    if (!terrain.is_array() || terrain.size() != 4U)
                        throw import_error{"DPE-TILED-WANG-UNSUPPORTED", "Legacy terrain corners are malformed.", tileset_path};
                    int terrain_id{-1};
                    json connectivity = json::array({0, 0, 0, 0, 0, 0, 0, 0});
                    for (std::size_t corner = 0; corner < terrain.size(); ++corner)
                    {
                        if (terrain.at(corner).is_null()) continue;
                        if (!terrain.at(corner).is_number_integer() || terrain.at(corner).get<int>() < 0
                            || (terrain_id >= 0 && terrain_id != terrain.at(corner).get<int>()))
                            throw import_error{"DPE-TILED-WANG-UNSUPPORTED",
                                "Mixed legacy terrain transitions cannot be represented by the built-in Rule Tile.", tileset_path};
                        terrain_id = terrain.at(corner).get<int>();
                        connectivity[corner * 2U] = terrain_id + 1;
                    }
                    if (terrain_id >= 0 && !seen_terrains.insert(terrain_id).second)
                        throw import_error{"DPE-TILED-WANG-UNSUPPORTED",
                            "A legacy terrain maps to multiple tiles and cannot be represented losslessly.", tileset_path};
                    if (terrain_id >= 0) make_rule_tile(id, connectivity, tileset_path);
                }
            }
            total_tile_count += output_set.tiles.size();
            imported_sets.push_back(std::move(imported));
        }

        if (!map.contains("layers") || !map.at("layers").is_array()
            || map.at("layers").size() > max_layers)
            throw import_error{"DPE-TILED-LAYERS", "The Tiled map requires a supported number of layers.", source_map};
        tiles::tilemap_document output_map;
        output_map.asset_id = request.tilemap_asset_id;
        output_map.name = request.name;
        output_map.grid.layout = conversion.layout;
        output_map.grid.cell_size = {
            static_cast<double>(map_tile_width) / request.pixels_per_unit,
            static_cast<double>(map_tile_height) / request.pixels_per_unit};
        if (orientation == "hexagonal")
        {
            const auto side = required_positive_int(map, "hexsidelength", source_map);
            if ((conversion.stagger_axis == 'y' && side > map_tile_height)
                || (conversion.stagger_axis == 'x' && side > map_tile_width))
                throw import_error{"DPE-TILED-ORIENTATION", "The hexadecimal side length exceeds the map tile size.", source_map};
            if (conversion.stagger_axis == 'y')
            {
                const auto desired_step = static_cast<double>(map_tile_height + side)
                    / (2.0 * request.pixels_per_unit);
                output_map.grid.cell_gap.y = desired_step / 0.75 - output_map.grid.cell_size.y;
            }
            else
            {
                const auto desired_step = static_cast<double>(map_tile_width + side)
                    / (2.0 * request.pixels_per_unit);
                output_map.grid.cell_gap.x = desired_step / 0.75 - output_map.grid.cell_size.x;
            }
        }
        for (const auto& imported : imported_sets)
            output_map.tile_set_dependencies.push_back(imported.document.asset_id);
        std::set<int> layer_ids;
        std::size_t total_cells{};
        unsigned layer_order{};
        for (const auto& layer_value : map.at("layers"))
        {
            if (!layer_value.is_object() || layer_value.value("type", std::string{}) != "tilelayer")
                throw import_error{"DPE-TILED-LAYER-TYPE", "Only Tiled tile layers are supported; no files were imported.", source_map};
            if (layer_value.contains("encoding") || layer_value.contains("compression"))
                throw import_error{"DPE-TILED-LAYER-ENCODING", "Encoded or compressed tile-layer data is not supported.", source_map};
            if (!layer_value.contains("id") || !layer_value.at("id").is_number_integer())
                throw import_error{"DPE-TILED-LAYER-ID", "Each Tiled tile layer requires an integer id.", source_map};
            const auto source_layer_id = layer_value.at("id").get<int>();
            if (source_layer_id <= 0 || !layer_ids.insert(source_layer_id).second)
                throw import_error{"DPE-TILED-LAYER-ID", "Tiled layer IDs must be positive and unique.", source_map};
            tiles::tile_layer layer;
            layer.layer_id = stable_uuid(request.tilemap_asset_id, "layer:" + std::to_string(source_layer_id));
            layer.name = optional_name(layer_value, "Layer " + std::to_string(source_layer_id));
            layer.visible = layer_value.value("visible", true);
            layer.order = layer_order++;
            std::set<std::pair<int, int>> occupied;
            if (layer_value.contains("chunks"))
            {
                if (!layer_value.at("chunks").is_array() || layer_value.at("chunks").size() > max_chunks)
                    throw import_error{"DPE-TILED-CHUNKS", "The Tiled layer has an invalid or excessive chunk list.", source_map};
                for (const auto& chunk : layer_value.at("chunks"))
                {
                    if (!chunk.is_object())
                        throw import_error{"DPE-TILED-CHUNKS", "Each Tiled chunk must be an object.", source_map};
                    const auto width = required_positive_int(chunk, "width", source_map);
                    const auto height = required_positive_int(chunk, "height", source_map);
                    const auto x = optional_int(chunk, "x", 0, source_map);
                    const auto y = optional_int(chunk, "y", 0, source_map);
                    if (!chunk.contains("data"))
                        throw import_error{"DPE-TILED-LAYER-DATA", "Each Tiled chunk requires data.", source_map};
                    add_data_array(layer, occupied, chunk.at("data"), x, y, width, height,
                        conversion, imported_sets, total_cells);
                }
            }
            else
            {
                const auto width = required_positive_int(layer_value, "width", source_map);
                const auto height = required_positive_int(layer_value, "height", source_map);
                const auto x = optional_int(layer_value, "x", 0, source_map);
                const auto y = optional_int(layer_value, "y", 0, source_map);
                if (!layer_value.contains("data"))
                    throw import_error{"DPE-TILED-LAYER-DATA", "Each Tiled tile layer requires data.", source_map};
                add_data_array(layer, occupied, layer_value.at("data"), x, y, width, height,
                    conversion, imported_sets, total_cells);
            }
            output_map.layers.push_back(std::move(layer));
        }

        const auto map_encoding = tiles::write_tilemap(output_map);
        const auto parsed_map = tiles::read_tilemap(map_encoding);
        if (!parsed_map.succeeded())
            throw import_error{"DPE-TILED-NATIVE-VALIDATION",
                "Generated Dragon Pixel Tilemap failed validation: " + parsed_map.error};
        result.tilemap_path = request.staging_directory / "tilemap.dpetilemap";
        std::vector<std::string> set_encodings;
        set_encodings.reserve(imported_sets.size());
        for (std::size_t index = 0; index < imported_sets.size(); ++index)
        {
            auto encoding = tiles::write_tile_set(imported_sets[index].document);
            const auto parsed = tiles::read_tile_set(encoding);
            if (!parsed.succeeded())
                throw import_error{"DPE-TILED-NATIVE-VALIDATION",
                    "Generated Dragon Pixel TileSet failed validation: " + parsed.error};
            set_encodings.push_back(std::move(encoding));
            result.tileset_paths.push_back(request.staging_directory /
                (imported_sets.size() == 1 ? "tileset.dpetileset"
                    : "tileset-" + std::to_string(index) + ".dpetileset"));
            result.texture_paths.push_back(request.staging_directory /
                (imported_sets.size() == 1 ? "texture.png"
                    : "texture-" + std::to_string(index) + ".png"));
            result.tileset_asset_ids.push_back(imported_sets[index].document.asset_id);
            result.texture_asset_ids.push_back(imported_sets[index].document.texture_asset_id);
        }
        result.tileset_path = result.tileset_paths.front();
        result.texture_path = result.texture_paths.front();
        try
        {
            write_file(result.tilemap_path, map_encoding);
            for (std::size_t index = 0; index < imported_sets.size(); ++index)
            {
                write_file(result.tileset_paths[index], set_encodings[index]);
                write_file(result.texture_paths[index], imported_sets[index].texture_bytes);
            }
        }
        catch (...)
        {
            std::filesystem::remove(result.tilemap_path, filesystem_error);
            for (const auto& path : result.tileset_paths) std::filesystem::remove(path, filesystem_error);
            for (const auto& path : result.texture_paths) std::filesystem::remove(path, filesystem_error);
            throw;
        }
        result.tile_count = total_tile_count;
        result.layer_count = output_map.layers.size();
        result.cell_count = total_cells;
        result.succeeded = true;
    }
    catch (const import_error& exception)
    {
        result.diagnostics.push_back({exception.code(), exception.what(), exception.path()});
    }
    catch (const std::exception& exception)
    {
        result.diagnostics.push_back({"DPE-TILED-UNEXPECTED", exception.what(), request.source_map});
    }
    return result;
}

int execute_request_file(const std::filesystem::path& request_path, std::string& error)
{
    try
    {
        const auto root = parse_json_file(request_path, 1024U * 1024U, "Importer request");
        if (!root.is_object() || root.value("format", std::string{}) != "dpe.tile-import.request"
            || (root.value("formatVersion", 0) != 1 && root.value("formatVersion", 0) != 2)
            || root.value("importer", std::string{}) != "dragonpixel.tiled-json")
        {
            throw import_error{"DPE-TILED-REQUEST", "Unsupported tile importer request format, version, or importer."};
        }
        if (!root.contains("assetIds") || !root.at("assetIds").is_object())
            throw import_error{"DPE-TILED-REQUEST", "The importer request is missing assetIds."};
        const auto& ids = root.at("assetIds");
        import_request request{
            std::filesystem::path{required_string(root, "sourceMap")},
            std::filesystem::path{required_string(root, "stagingDirectory")},
            required_string(root, "name"),
            required_uuid(ids, "tilemap"),
            required_uuid(ids, "tileset"),
            required_uuid(ids, "texture"),
            root.value("pixelsPerUnit", 32.0),
            root.value("importIsometricAsZAsY", false),
        };
        const auto result = import_tiled_json(request);
        write_file(request.staging_directory / "result.json", result_json(request, result).dump(2) + "\n");
        if (!result.succeeded)
        {
            error = result.diagnostics.empty() ? "Tiled import failed." : result.diagnostics.front().message;
            return 2;
        }
        return 0;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return 3;
    }
}
}
