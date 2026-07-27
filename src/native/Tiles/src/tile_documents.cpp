#include <dragonpixel/tiles/tile_documents.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace dragonpixel::tiles
{
namespace
{
using json = nlohmann::ordered_json;

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

std::string required_string(const json& value, const char* field)
{
    if (!value.contains(field) || !value.at(field).is_string() || value.at(field).get<std::string>().empty())
    {
        throw std::runtime_error{std::string{"Missing string field: "} + field};
    }
    return value.at(field).get<std::string>();
}

integer_point read_point(const json& value, const char* field, bool positive)
{
    const auto& point = value.at(field);
    integer_point result{point.at("x").get<int>(), point.at("y").get<int>()};
    if ((positive && (result.x <= 0 || result.y <= 0)) || (!positive && (result.x < 0 || result.y < 0)))
    {
        throw std::runtime_error{std::string{"Invalid point field: "} + field};
    }
    return result;
}

json point_json(const integer_point& point)
{
    return {{"x", point.x}, {"y", point.y}};
}

void require_envelope(const json& value, std::string_view format)
{
    if (value.value("format", "") != format || value.value("formatVersion", 0) != 1)
    {
        throw std::runtime_error{"Unsupported tile document format or version."};
    }
}
}

document_result<tile_set_document> read_tile_set(std::string_view encoded)
{
    try
    {
        const auto value = json::parse(encoded);
        require_envelope(value, "dpe.tileset");
        tile_set_document result;
        result.asset_id = required_uuid(value, "assetId");
        result.name = required_string(value, "name");
        result.texture_asset_id = required_uuid(value, "textureAssetId");
        result.cell_size = read_point(value, "cellSize", true);
        result.margin = read_point(value, "margin", false);
        result.spacing = read_point(value, "spacing", false);
        result.pixels_per_unit = value.at("pixelsPerUnit").get<double>();
        if (!std::isfinite(result.pixels_per_unit) || result.pixels_per_unit <= 0.0)
        {
            throw std::runtime_error{"pixelsPerUnit must be finite and positive."};
        }
        std::set<core::uuid> ids;
        for (const auto& item : value.at("tiles"))
        {
            tile_definition tile;
            tile.tile_id = required_uuid(item, "tileId");
            tile.name = required_string(item, "name");
            const auto& source = item.at("source");
            tile.source = {source.at("x").get<int>(), source.at("y").get<int>(),
                source.at("width").get<int>(), source.at("height").get<int>()};
            if (tile.source.x < 0 || tile.source.y < 0 || tile.source.width <= 0 || tile.source.height <= 0
                || !ids.insert(tile.tile_id).second)
            {
                throw std::runtime_error{"Tile IDs must be unique and source rectangles must be positive."};
            }
            if (item.contains("collision") && !item.at("collision").is_null())
            {
                const auto& collision = item.at("collision");
                tile.collision = collision_rectangle{collision.at("offsetX").get<double>(),
                    collision.at("offsetY").get<double>(), collision.at("width").get<double>(),
                    collision.at("height").get<double>()};
                if (!std::isfinite(tile.collision->width) || !std::isfinite(tile.collision->height)
                    || tile.collision->width <= 0.0 || tile.collision->height <= 0.0)
                {
                    throw std::runtime_error{"Collision rectangles must be finite and positive."};
                }
            }
            result.tiles.push_back(std::move(tile));
        }
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
        require_envelope(value, "dpe.tilemap");
        if (value.value("chunkSize", 0) != 32)
        {
            throw std::runtime_error{"Tilemap chunkSize must be 32."};
        }
        tilemap_document result;
        result.asset_id = required_uuid(value, "assetId");
        result.name = required_string(value, "name");
        std::set<core::uuid> dependency_ids;
        for (const auto& dependency : value.at("tileSetDependencies"))
        {
            const auto parsed = core::uuid::parse(dependency.get<std::string>());
            if (!parsed || !dependency_ids.insert(*parsed).second)
            {
                throw std::runtime_error{"TileSet dependency IDs must be valid and unique."};
            }
            result.tile_set_dependencies.push_back(*parsed);
        }
        std::set<core::uuid> layer_ids;
        std::set<unsigned> orders;
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
            std::set<std::pair<int, int>> chunk_positions;
            for (const auto& chunk_value : item.at("chunks"))
            {
                tile_chunk chunk{chunk_value.at("x").get<int>(), chunk_value.at("y").get<int>(), {}};
                if (!chunk_positions.emplace(chunk.x, chunk.y).second)
                {
                    throw std::runtime_error{"Chunk coordinates must be unique per layer."};
                }
                std::set<unsigned> cell_indices;
                for (const auto& cell_value : chunk_value.at("cells"))
                {
                    tile_cell cell;
                    cell.index = cell_value.at("index").get<unsigned>();
                    cell.tile_id = required_uuid(cell_value, "tileId");
                    cell.flip_x = cell_value.value("flipX", false);
                    cell.flip_y = cell_value.value("flipY", false);
                    cell.rotation_quarter_turns = cell_value.value("rotationQuarterTurns", 0U);
                    if (cell.index >= 1024 || cell.rotation_quarter_turns > 3 || !cell_indices.insert(cell.index).second)
                    {
                        throw std::runtime_error{"Cells require unique indexes below 1024 and rotations from zero to three."};
                    }
                    chunk.cells.push_back(cell);
                }
                layer.chunks.push_back(std::move(chunk));
            }
            result.layers.push_back(std::move(layer));
        }
        return {std::move(result), {}};
    }
    catch (const std::exception& exception)
    {
        return {std::nullopt, exception.what()};
    }
}

std::string write_tile_set(const tile_set_document& document)
{
    auto tiles = document.tiles;
    std::sort(tiles.begin(), tiles.end(), [](const auto& left, const auto& right) { return left.tile_id < right.tile_id; });
    json values = json::array();
    for (const auto& tile : tiles)
    {
        json value{{"tileId", tile.tile_id.to_string()}, {"name", tile.name},
            {"source", {{"x", tile.source.x}, {"y", tile.source.y}, {"width", tile.source.width}, {"height", tile.source.height}}}};
        value["collision"] = tile.collision
            ? json{{"offsetX", tile.collision->offset_x}, {"offsetY", tile.collision->offset_y},
                {"width", tile.collision->width}, {"height", tile.collision->height}}
            : json{nullptr};
        values.push_back(std::move(value));
    }
    return json{{"$schema", "https://dragonpixel.dev/schemas/v1/tileset.schema.json"}, {"format", "dpe.tileset"},
        {"formatVersion", 1}, {"engineVersion", "0.3.0-slice2"}, {"assetId", document.asset_id.to_string()},
        {"name", document.name}, {"textureAssetId", document.texture_asset_id.to_string()},
        {"cellSize", point_json(document.cell_size)}, {"margin", point_json(document.margin)},
        {"spacing", point_json(document.spacing)}, {"pixelsPerUnit", document.pixels_per_unit}, {"tiles", values}}.dump(2) + "\n";
}

std::string write_tilemap(const tilemap_document& document)
{
    auto dependencies = document.tile_set_dependencies;
    std::sort(dependencies.begin(), dependencies.end());
    json dependency_values = json::array();
    for (const auto& id : dependencies) dependency_values.push_back(id.to_string());
    auto layers = document.layers;
    std::sort(layers.begin(), layers.end(), [](const auto& left, const auto& right) { return left.order < right.order; });
    json layer_values = json::array();
    for (auto& layer : layers)
    {
        std::sort(layer.chunks.begin(), layer.chunks.end(), [](const auto& left, const auto& right) {
            return left.y != right.y ? left.y < right.y : left.x < right.x;
        });
        json chunks = json::array();
        for (auto& chunk : layer.chunks)
        {
            std::sort(chunk.cells.begin(), chunk.cells.end(), [](const auto& left, const auto& right) { return left.index < right.index; });
            json cells = json::array();
            for (const auto& cell : chunk.cells)
            {
                cells.push_back({{"index", cell.index}, {"tileId", cell.tile_id.to_string()}, {"flipX", cell.flip_x},
                    {"flipY", cell.flip_y}, {"rotationQuarterTurns", cell.rotation_quarter_turns}});
            }
            chunks.push_back({{"x", chunk.x}, {"y", chunk.y}, {"cells", cells}});
        }
        layer_values.push_back({{"layerId", layer.layer_id.to_string()}, {"name", layer.name}, {"visible", layer.visible},
            {"order", layer.order}, {"chunks", chunks}});
    }
    return json{{"$schema", "https://dragonpixel.dev/schemas/v1/tilemap.schema.json"}, {"format", "dpe.tilemap"},
        {"formatVersion", 1}, {"engineVersion", "0.3.0-slice2"}, {"assetId", document.asset_id.to_string()},
        {"name", document.name}, {"chunkSize", 32}, {"tileSetDependencies", dependency_values}, {"layers", layer_values}}.dump(2) + "\n";
}
}
