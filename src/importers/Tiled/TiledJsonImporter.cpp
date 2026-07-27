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
};

transform_flags convert_flags(std::uint32_t gid)
{
    const auto horizontal = (gid & flip_horizontal) != 0;
    const auto vertical = (gid & flip_vertical) != 0;
    const auto diagonal = (gid & flip_diagonal) != 0;
    if (!diagonal)
    {
        return {horizontal, vertical, 0U};
    }
    if (horizontal && vertical) return {true, false, 1U};
    if (horizontal) return {false, false, 1U};
    if (vertical) return {false, false, 3U};
    return {true, false, 3U};
}

void add_cell(
    tiles::tile_layer& layer,
    std::set<std::pair<int, int>>& occupied,
    int tiled_x,
    int tiled_y,
    std::uint64_t encoded_gid,
    std::uint32_t first_gid,
    const std::vector<tiles::tile_definition>& definitions,
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
    if ((gid & reserved_high_bit) != 0U)
    {
        // Tiled requires this bit to be cleared even for non-hexagonal maps.
        // It has no orthogonal meaning and is intentionally ignored.
    }
    if (plain_gid < first_gid)
    {
        throw import_error{"DPE-TILED-GID", "A Tiled global tile ID precedes the imported TileSet."};
    }
    const auto local_id = static_cast<std::size_t>(plain_gid - first_gid);
    if (local_id >= definitions.size())
    {
        throw import_error{"DPE-TILED-GID", "A Tiled global tile ID does not resolve to the imported TileSet."};
    }
    if (tiled_x < -max_coordinate || tiled_x > max_coordinate
        || tiled_y < -max_coordinate || tiled_y > max_coordinate)
    {
        throw import_error{"DPE-TILED-LIMIT", "A Tiled cell coordinate exceeds the supported range."};
    }
    const auto x = tiled_x;
    const auto y = -tiled_y;
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
    const auto flags = convert_flags(gid);
    chunk->cells.push_back({index, definitions.at(local_id).tile_id,
        flags.flip_x, flags.flip_y, flags.rotation});
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
    std::uint32_t first_gid,
    const std::vector<tiles::tile_definition>& definitions,
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
        add_cell(layer, occupied, x, y, value, first_gid, definitions, total_cells);
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
    for (const auto& name : {"tilemap.dpetilemap", "tileset.dpetileset", "texture.png", "result.json"})
    {
        if (std::filesystem::exists(staging / name, error))
            throw import_error{"DPE-TILED-STAGING-COLLISION", "The importer staging directory already contains an output.", staging / name};
    }
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
        outputs.push_back({{"role", "tileset"}, {"path", result.tileset_path.filename().generic_string()},
            {"assetId", request.tileset_asset_id.to_string()}});
        outputs.push_back({{"role", "texture"}, {"path", result.texture_path.filename().generic_string()},
            {"assetId", request.texture_asset_id.to_string()}});
    }
    return {
        {"format", "dpe.tile-import.result"},
        {"formatVersion", 1},
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
        if (!map.is_object() || map.value("orientation", std::string{}) != "orthogonal")
            throw import_error{"DPE-TILED-ORIENTATION", "Only orthogonal Tiled JSON maps are supported.", source_map};
        if (map.contains("type") && map.value("type", std::string{}) != "map")
            throw import_error{"DPE-TILED-TYPE", "The selected JSON document is not a Tiled map.", source_map};
        const auto map_tile_width = required_positive_int(map, "tilewidth", source_map);
        const auto map_tile_height = required_positive_int(map, "tileheight", source_map);
        if (!map.contains("tilesets") || !map.at("tilesets").is_array() || map.at("tilesets").size() != 1U)
            throw import_error{"DPE-TILED-TILESETS", "This importer requires exactly one atlas TileSet.", source_map};

        const auto& map_tileset = map.at("tilesets").front();
        if (!map_tileset.is_object() || !map_tileset.contains("firstgid")
            || !map_tileset.at("firstgid").is_number_integer())
            throw import_error{"DPE-TILED-TILESET", "The Tiled map TileSet requires a numeric firstgid.", source_map};
        const auto first_gid_value = map_tileset.at("firstgid").get<long long>();
        if (first_gid_value <= 0 || first_gid_value > static_cast<long long>(gid_mask))
            throw import_error{"DPE-TILED-TILESET", "The Tiled TileSet firstgid is outside the supported range.", source_map};
        const auto first_gid = static_cast<std::uint32_t>(first_gid_value);
        auto tileset = map_tileset;
        auto tileset_path = source_map;
        if (map_tileset.contains("source"))
        {
            if (!map_tileset.at("source").is_string())
                throw import_error{"DPE-TILED-TILESET", "The external TileSet source must be a relative string.", source_map};
            tileset_path = contained_dependency(source_root, source_root,
                map_tileset.at("source").get<std::string>(), "External TileSet");
            tileset = parse_json_file(tileset_path, max_tileset_bytes, "Tiled TileSet");
        }
        if (!tileset.is_object())
            throw import_error{"DPE-TILED-TILESET", "The Tiled TileSet is not an object.", tileset_path};
        if (tileset.contains("type") && tileset.value("type", std::string{}) != "tileset")
            throw import_error{"DPE-TILED-TILESET", "The external JSON document is not a Tiled TileSet.", tileset_path};
        const auto tile_width = required_positive_int(tileset, "tilewidth", tileset_path);
        const auto tile_height = required_positive_int(tileset, "tileheight", tileset_path);
        if (tile_width != map_tile_width || tile_height != map_tile_height)
            throw import_error{"DPE-TILED-TILE-SIZE", "The Tiled map and TileSet tile dimensions must match.", tileset_path};
        const auto tile_count = required_positive_int(tileset, "tilecount", tileset_path);
        const auto columns = required_positive_int(tileset, "columns", tileset_path);
        if (tile_count > static_cast<int>(max_tiles) || columns > tile_count)
            throw import_error{"DPE-TILED-LIMIT", "The Tiled TileSet exceeds the supported tile limit.", tileset_path};
        const auto margin = optional_int(tileset, "margin", 0, tileset_path);
        const auto spacing = optional_int(tileset, "spacing", 0, tileset_path);
        if (margin < 0 || spacing < 0)
            throw import_error{"DPE-TILED-TILESET", "TileSet margin and spacing must be non-negative.", tileset_path};
        if (!tileset.contains("image") || !tileset.at("image").is_string())
            throw import_error{"DPE-TILED-IMAGE", "Only a single atlas-image TileSet is supported.", tileset_path};
        const auto texture_path = contained_dependency(source_root, tileset_path.parent_path(),
            tileset.at("image").get<std::string>(), "TileSet image");
        if (texture_path.extension().string() != ".png" && texture_path.extension().string() != ".PNG")
            throw import_error{"DPE-TILED-IMAGE", "The TileSet atlas must be a PNG image.", texture_path};
        const auto texture_bytes = read_file(texture_path, max_texture_bytes, "TileSet PNG");

        tiles::tile_set_document output_set;
        output_set.asset_id = request.tileset_asset_id;
        output_set.name = optional_name(tileset, request.name + " Tiles");
        output_set.texture_asset_id = request.texture_asset_id;
        output_set.cell_size = {tile_width, tile_height};
        output_set.margin = {margin, margin};
        output_set.spacing = {spacing, spacing};
        output_set.pixels_per_unit = request.pixels_per_unit;
        std::map<int, std::string> explicit_names;
        if (tileset.contains("tiles"))
        {
            if (!tileset.at("tiles").is_array())
                throw import_error{"DPE-TILED-TILESET", "TileSet tile definitions must be an array.", tileset_path};
            for (const auto& tile : tileset.at("tiles"))
            {
                if (!tile.is_object() || !tile.contains("id") || !tile.at("id").is_number_integer())
                    throw import_error{"DPE-TILED-TILESET", "Each Tiled tile definition requires an integer id.", tileset_path};
                const auto id = tile.at("id").get<int>();
                if (id < 0 || id >= tile_count || !explicit_names.emplace(id,
                        optional_name(tile, output_set.name + " " + std::to_string(id))).second)
                    throw import_error{"DPE-TILED-TILESET", "Tiled tile definition IDs must be unique and inside tilecount.", tileset_path};
            }
        }
        output_set.tiles.reserve(static_cast<std::size_t>(tile_count));
        for (int id = 0; id < tile_count; ++id)
        {
            const auto column = id % columns;
            const auto row = id / columns;
            const auto named = explicit_names.find(id);
            output_set.tiles.push_back({
                stable_uuid(request.tileset_asset_id, "tile:" + std::to_string(id)),
                named == explicit_names.end() ? output_set.name + " " + std::to_string(id) : named->second,
                {margin + column * (tile_width + spacing), margin + row * (tile_height + spacing), tile_width, tile_height},
                std::nullopt,
            });
        }

        if (!map.contains("layers") || !map.at("layers").is_array()
            || map.at("layers").size() > max_layers)
            throw import_error{"DPE-TILED-LAYERS", "The Tiled map requires a supported number of layers.", source_map};
        tiles::tilemap_document output_map;
        output_map.asset_id = request.tilemap_asset_id;
        output_map.name = request.name;
        output_map.tile_set_dependencies.push_back(request.tileset_asset_id);
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
                        first_gid, output_set.tiles, total_cells);
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
                    first_gid, output_set.tiles, total_cells);
            }
            output_map.layers.push_back(std::move(layer));
        }

        const auto set_encoding = tiles::write_tile_set(output_set);
        const auto map_encoding = tiles::write_tilemap(output_map);
        const auto parsed_set = tiles::read_tile_set(set_encoding);
        const auto parsed_map = tiles::read_tilemap(map_encoding);
        if (!parsed_set.succeeded() || !parsed_map.succeeded())
            throw import_error{"DPE-TILED-NATIVE-VALIDATION",
                "Generated Dragon Pixel tile documents failed validation: TileSet=" + parsed_set.error
                    + "; Tilemap=" + parsed_map.error};
        result.tileset_path = request.staging_directory / "tileset.dpetileset";
        result.tilemap_path = request.staging_directory / "tilemap.dpetilemap";
        result.texture_path = request.staging_directory / "texture.png";
        try
        {
            write_file(result.tileset_path, set_encoding);
            write_file(result.tilemap_path, map_encoding);
            write_file(result.texture_path, texture_bytes);
        }
        catch (...)
        {
            std::filesystem::remove(result.tileset_path, filesystem_error);
            std::filesystem::remove(result.tilemap_path, filesystem_error);
            std::filesystem::remove(result.texture_path, filesystem_error);
            throw;
        }
        result.tile_count = output_set.tiles.size();
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
            || root.value("formatVersion", 0) != 1
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
