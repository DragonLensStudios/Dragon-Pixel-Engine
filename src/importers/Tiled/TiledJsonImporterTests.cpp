#include "TiledJsonImporter.h"

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/tiles/tile_documents.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using json = nlohmann::ordered_json;

constexpr std::uint32_t h = 0x80000000U;
constexpr std::uint32_t v = 0x40000000U;
constexpr std::uint32_t d = 0x20000000U;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error{message};
}

struct temporary_tree final
{
    temporary_tree()
        : path(std::filesystem::temp_directory_path()
              / ("dpe-tiled-import-" + dragonpixel::core::uuid::random_v4().to_string()))
    {
        std::filesystem::create_directories(path);
    }

    ~temporary_tree()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

void write_bytes(const std::filesystem::path& path, std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    require(stream.good(), "Could not create test fixture file.");
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "Could not write test fixture file.");
}

std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary};
    require(stream.good(), "Could not read test output file.");
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

json inline_tileset(std::string image = "atlas.png")
{
    return {
        {"type", "tileset"},
        {"name", "Terrain"},
        {"tilewidth", 16},
        {"tileheight", 16},
        {"tilecount", 1},
        {"columns", 1},
        {"margin", 0},
        {"spacing", 0},
        {"image", std::move(image)},
        {"tiles", json::array({{{"id", 0}, {"name", "Grass"}}})},
    };
}

json finite_map()
{
    return {
        {"type", "map"},
        {"orientation", "orthogonal"},
        {"infinite", false},
        {"tilewidth", 16},
        {"tileheight", 16},
        {"width", 8},
        {"height", 1},
        {"tilesets", json::array({{{"firstgid", 1}, {"name", "Terrain"}, {"tilewidth", 16},
            {"tileheight", 16}, {"tilecount", 1}, {"columns", 1}, {"margin", 0}, {"spacing", 0},
            {"image", "atlas.png"}, {"tiles", json::array({{{"id", 0}, {"name", "Grass"}}})}}})},
        {"layers", json::array({{
            {"id", 7},
            {"name", "Ground"},
            {"type", "tilelayer"},
            {"visible", true},
            {"width", 8},
            {"height", 1},
            {"x", 0},
            {"y", 0},
            {"data", json::array({1U, h | 1U, v | 1U, h | v | 1U,
                d | 1U, h | d | 1U, v | d | 1U, h | v | d | 1U})},
        }})},
    };
}

dragonpixel::importers::tiled::import_request request_for(
    const temporary_tree& tree,
    const std::filesystem::path& staging)
{
    return {
        tree.path / "map.tmj",
        staging,
        "Imported Map",
        *dragonpixel::core::uuid::parse("c7de7bcc-1dc6-41d0-8195-4db6025fa2cc"),
        *dragonpixel::core::uuid::parse("07b1954d-7e87-48d4-9502-9ff1fdd2d94f"),
        *dragonpixel::core::uuid::parse("561c8ac6-f2f5-4749-aef7-7b747a7583d9"),
        16.0,
    };
}

void write_common_texture(const temporary_tree& tree)
{
    // The converter treats texture bytes as opaque. Editor-owned validation
    // performs actual PNG decoding before project publication.
    static constexpr std::array<unsigned char, 8> png_signature{
        0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
    write_bytes(tree.path / "atlas.png",
        {reinterpret_cast<const char*>(png_signature.data()), png_signature.size()});
}

void write_second_texture(const temporary_tree& tree)
{
    write_bytes(tree.path / "props.png", read_bytes(tree.path / "atlas.png"));
}

std::pair<int, int> absolute_cell(const dragonpixel::tiles::tile_chunk& chunk,
    const dragonpixel::tiles::tile_cell& cell)
{
    return {
        chunk.x * 32 + static_cast<int>(cell.index % 32U),
        chunk.y * 32 + static_cast<int>(cell.index / 32U),
    };
}

void finite_inline_map_converts_deterministically_with_all_flags()
{
    temporary_tree tree;
    write_common_texture(tree);
    write_bytes(tree.path / "map.tmj", finite_map().dump(2));
    const auto request = request_for(tree, tree.path / "staging-a");
    const auto result = dragonpixel::importers::tiled::import_tiled_json(request);
    require(result.succeeded, result.diagnostics.empty() ? "Finite import failed." : result.diagnostics.front().message);
    require(result.tile_count == 1 && result.layer_count == 1 && result.cell_count == 8,
        "Finite import statistics were incorrect.");
    const auto parsed_set = dragonpixel::tiles::read_tile_set(read_bytes(result.tileset_path));
    const auto parsed_map = dragonpixel::tiles::read_tilemap(read_bytes(result.tilemap_path));
    require(parsed_set.succeeded() && parsed_map.succeeded(), "Generated native tile documents were invalid.");
    require(parsed_set.document->asset_id == request.tileset_asset_id
        && parsed_set.document->texture_asset_id == request.texture_asset_id
        && parsed_map.document->asset_id == request.tilemap_asset_id,
        "Editor-assigned asset IDs were not retained.");
    require(parsed_set.document->tiles.front().name == "Grass", "Explicit Tiled tile name was not retained.");
    require(parsed_map.document->layers.front().chunks.size() == 1,
        "Finite cells were not lowered into the expected sparse chunk.");
    const auto& cells = parsed_map.document->layers.front().chunks.front().cells;
    require(cells.size() == 8, "Finite layer did not retain every non-empty cell.");
    const std::array<std::tuple<bool, bool, unsigned>, 8> expected{{
        {false, false, 0U}, {true, false, 0U}, {false, true, 0U}, {true, true, 0U},
        {true, false, 3U}, {false, false, 1U}, {false, false, 3U}, {true, false, 1U},
    }};
    for (std::size_t index = 0; index < cells.size(); ++index)
    {
        const auto& [flip_x, flip_y, rotation] = expected.at(index);
        require(cells.at(index).flip_x == flip_x && cells.at(index).flip_y == flip_y
            && cells.at(index).rotation_quarter_turns == rotation,
            "A Tiled orthogonal flip combination was mapped incorrectly.");
    }
    auto second_request = request;
    second_request.staging_directory = tree.path / "staging-b";
    const auto second = dragonpixel::importers::tiled::import_tiled_json(second_request);
    require(second.succeeded, "Deterministic repeat import failed.");
    require(read_bytes(result.tileset_path) == read_bytes(second.tileset_path)
        && read_bytes(result.tilemap_path) == read_bytes(second.tilemap_path),
        "Repeated import with the same assigned identities was not deterministic.");
}

void external_infinite_map_converts_negative_chunks_and_y_axis()
{
    temporary_tree tree;
    write_common_texture(tree);
    write_bytes(tree.path / "terrain.tsj", inline_tileset().dump(2));
    const json map{
        {"type", "map"}, {"orientation", "orthogonal"}, {"infinite", true},
        {"tilewidth", 16}, {"tileheight", 16},
        {"tilesets", json::array({{{"firstgid", 1}, {"source", "terrain.tsj"}}})},
        {"layers", json::array({{
            {"id", 11}, {"name", "Infinite"}, {"type", "tilelayer"}, {"visible", false},
            {"chunks", json::array({{{"x", -2}, {"y", -1}, {"width", 2}, {"height", 2},
                {"data", json::array({1, 0, 0, 1})}}})},
        }})},
    };
    write_bytes(tree.path / "map.tmj", map.dump(2));
    const auto result = dragonpixel::importers::tiled::import_tiled_json(
        request_for(tree, tree.path / "staging"));
    require(result.succeeded, result.diagnostics.empty() ? "Infinite import failed." : result.diagnostics.front().message);
    const auto parsed = dragonpixel::tiles::read_tilemap(read_bytes(result.tilemap_path));
    require(parsed.succeeded() && !parsed.document->layers.front().visible,
        "Infinite layer visibility was not retained.");
    std::vector<std::pair<int, int>> coordinates;
    for (const auto& chunk : parsed.document->layers.front().chunks)
        for (const auto& cell : chunk.cells) coordinates.push_back(absolute_cell(chunk, cell));
    std::sort(coordinates.begin(), coordinates.end());
    require(coordinates == std::vector<std::pair<int, int>>{{-2, 1}, {-1, 0}},
        "Tiled negative chunks or top-down coordinates were converted incorrectly.");
}

void multiple_tilesets_isometric_animation_and_hex_rotation_convert()
{
    temporary_tree tree;
    write_common_texture(tree);
    write_second_texture(tree);
    auto animated = inline_tileset();
    animated["tilecount"] = 2;
    animated["columns"] = 2;
    animated["tiles"] = json::array({{{"id", 0}, {"name", "Water"},
        {"animation", json::array({{{"tileid", 0}, {"duration", 100}},
            {{"tileid", 1}, {"duration", 250}}})}}});
    auto props = inline_tileset("props.png");
    props["name"] = "Props";
    props["wangsets"] = json::array({{{"name", "Solid"},
        {"wangtiles", json::array({{{"tileid", 0},
            {"wangid", json::array({1, 1, 1, 1, 1, 1, 1, 1})}}})}}});
    animated["firstgid"] = 1;
    props["firstgid"] = 3;
    const json map{
        {"type", "map"}, {"orientation", "isometric"}, {"infinite", false},
        {"tilewidth", 16}, {"tileheight", 16}, {"width", 2}, {"height", 1},
        {"tilesets", json::array({animated, props})},
        {"layers", json::array({{{"id", 5}, {"type", "tilelayer"}, {"width", 2},
            {"height", 1}, {"data", json::array({1, 3})}}})},
    };
    write_bytes(tree.path / "map.tmj", map.dump(2));
    const auto result = dragonpixel::importers::tiled::import_tiled_json(
        request_for(tree, tree.path / "staging"));
    require(result.succeeded, result.diagnostics.empty() ? "Multi-set import failed."
        : result.diagnostics.front().message);
    require(result.tileset_paths.size() == 2 && result.texture_paths.size() == 2,
        "Multiple atlas TileSets did not produce isolated outputs.");
    const auto first = dragonpixel::tiles::read_tile_set(read_bytes(result.tileset_paths[0]));
    const auto second = dragonpixel::tiles::read_tile_set(read_bytes(result.tileset_paths[1]));
    const auto parsed_map = dragonpixel::tiles::read_tilemap(read_bytes(result.tilemap_path));
    require(first.succeeded() && second.succeeded() && parsed_map.succeeded(),
        "Multi-set outputs failed native validation.");
    require(first.document->tiles[0].kind == dragonpixel::tiles::tile_kind::animated
        && first.document->tiles[0].animation_frames.size() == 2,
        "Tiled animation was not converted to an Animated Tile.");
    require(second.document->tiles[0].kind == dragonpixel::tiles::tile_kind::rule
        && !second.document->tiles[0].rules.empty(),
        "Representable Tiled Wang data was not converted to a Rule Tile.");
    require(parsed_map.document->grid.layout == dragonpixel::tiles::grid_layout::isometric
        && parsed_map.document->tile_set_dependencies.size() == 2,
        "Isometric layout or multiple TileSet dependencies were not retained.");
    std::set<dragonpixel::core::uuid> referenced_sets;
    for (const auto& chunk : parsed_map.document->layers.front().chunks)
        for (const auto& cell : chunk.cells) referenced_sets.insert(cell.tile_set_id);
    require(referenced_sets.size() == 2, "Qualified cells did not retain both TileSets.");

    auto hex = finite_map();
    hex["orientation"] = "hexagonal";
    hex["staggeraxis"] = "y";
    hex["staggerindex"] = "odd";
    hex["hexsidelength"] = 8;
    hex["width"] = 1;
    hex["layers"].front()["width"] = 1;
    hex["layers"].front()["data"] = json::array({d | 0x10000000U | 1U});
    write_bytes(tree.path / "hex.tmj", hex.dump(2));
    auto hex_request = request_for(tree, tree.path / "hex-staging");
    hex_request.source_map = tree.path / "hex.tmj";
    const auto hex_result = dragonpixel::importers::tiled::import_tiled_json(hex_request);
    require(hex_result.succeeded, hex_result.diagnostics.empty() ? "Hex import failed."
        : hex_result.diagnostics.front().message);
    const auto hex_map = dragonpixel::tiles::read_tilemap(read_bytes(hex_result.tilemap_path));
    require(hex_map.succeeded()
        && hex_map.document->grid.layout == dragonpixel::tiles::grid_layout::hex_point_top,
        "Hexagonal Tiled orientation was not converted.");
    const auto& hex_cell = hex_map.document->layers.front().chunks.front().cells.front();
    require(std::abs(hex_cell.rotation_degrees - 180.0) < 0.001,
        "Tiled 60/120 degree hexadecimal GID rotation flags were not preserved.");
}

void request_protocol_writes_a_versioned_result()
{
    temporary_tree tree;
    write_common_texture(tree);
    write_bytes(tree.path / "map.tmj", finite_map().dump(2));
    const auto staging = tree.path / "staging";
    std::filesystem::create_directories(staging);
    const json request{
        {"format", "dpe.tile-import.request"},
        {"formatVersion", 1},
        {"importer", "dragonpixel.tiled-json"},
        {"sourceMap", (tree.path / "map.tmj").generic_string()},
        {"stagingDirectory", staging.generic_string()},
        {"name", "Protocol Map"},
        {"pixelsPerUnit", 16.0},
        {"assetIds", {
            {"tilemap", "c7de7bcc-1dc6-41d0-8195-4db6025fa2cc"},
            {"tileset", "07b1954d-7e87-48d4-9502-9ff1fdd2d94f"},
            {"texture", "561c8ac6-f2f5-4749-aef7-7b747a7583d9"},
        }},
    };
    const auto request_path = tree.path / "request.json";
    write_bytes(request_path, request.dump(2));
    std::string error;
    require(dragonpixel::importers::tiled::execute_request_file(request_path, error) == 0,
        "Versioned request execution failed: " + error);
    const auto result = json::parse(read_bytes(staging / "result.json"));
    require(result.value("format", std::string{}) == "dpe.tile-import.result"
        && result.value("formatVersion", 0) == 2
        && result.value("importer", std::string{}) == "dragonpixel.tiled-json"
        && result.value("succeeded", false)
        && result.at("outputs").size() == 3,
        "Worker result envelope was incomplete or incompatible.");
}

void rejected_inputs_leave_no_staged_artifacts()
{
    const auto expect_failure = [](json map, std::string expected_code,
                                    const std::function<void(const temporary_tree&)>& setup = {}) {
        temporary_tree tree;
        write_common_texture(tree);
        if (setup) setup(tree);
        write_bytes(tree.path / "map.tmj", map.dump(2));
        const auto staging = tree.path / "staging";
        const auto result = dragonpixel::importers::tiled::import_tiled_json(request_for(tree, staging));
        require(!result.succeeded && !result.diagnostics.empty()
                && result.diagnostics.front().code == expected_code,
            "Rejected Tiled input did not report the expected diagnostic: " + expected_code);
        for (const auto& name : {"tilemap.dpetilemap", "tileset.dpetileset", "texture.png"})
            require(!std::filesystem::exists(staging / name), "Rejected input left a staged output behind.");
    };

    auto map = finite_map();
    map["orientation"] = "oblique";
    expect_failure(map, "DPE-TILED-ORIENTATION");

    map = finite_map();
    map["tilesets"].front().erase("image");
    map["tilesets"].front()["tiles"].front()["image"] = "single.png";
    expect_failure(map, "DPE-TILED-IMAGE");

    map = finite_map();
    map["layers"].push_back({{"id", 9}, {"type", "objectgroup"}, {"objects", json::array({{{"id", 1}}})}});
    expect_failure(map, "DPE-TILED-LAYER-TYPE");

    map = finite_map();
    map["layers"].front()["data"] = "AQAAAA==";
    map["layers"].front()["encoding"] = "base64";
    expect_failure(map, "DPE-TILED-LAYER-ENCODING");

    map = finite_map();
    map["tilesets"] = json::array({{{"firstgid", 1}, {"source", "../outside.tsj"}}});
    expect_failure(map, "DPE-TILED-DEPENDENCY-ESCAPE");

    map = finite_map();
    map["layers"].front()["data"].front() = 99;
    expect_failure(map, "DPE-TILED-GID");
}
}

int main()
{
    try
    {
        std::cout << "finite inline\n" << std::flush;
        finite_inline_map_converts_deterministically_with_all_flags();
        std::cout << "external infinite\n" << std::flush;
        external_infinite_map_converts_negative_chunks_and_y_axis();
        std::cout << "multiple layouts\n" << std::flush;
        multiple_tilesets_isometric_animation_and_hex_rotation_convert();
        std::cout << "request protocol\n" << std::flush;
        request_protocol_writes_a_versioned_result();
        std::cout << "rejections\n" << std::flush;
        rejected_inputs_leave_no_staged_artifacts();
        std::cout << "Tiled JSON importer tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
