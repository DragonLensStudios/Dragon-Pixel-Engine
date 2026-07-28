#include <dragonpixel/tiles/tile_documents.h>
#include <dragonpixel/tiles/tile_grid.h>
#include <dragonpixel/tiles/tile_evaluator.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error{message};
}

dragonpixel::core::uuid id(const char* value)
{
    return *dragonpixel::core::uuid::parse(value);
}

void require_close(double actual, double expected, const char* message)
{
    if (std::abs(actual - expected) > 0.000001) throw std::runtime_error{message};
}
}

int main()
{
    try
    {
        using namespace dragonpixel;
        const auto set_id = id("4fe655df-c40f-4e48-a5cc-fbe9bd356ac6");
        const auto second_set_id = id("cf662678-aed8-4ff8-a801-e50041305316");
        const auto texture_id = id("dd02cd2a-8a7e-4b27-9d26-06331093885a");
        const auto second_texture_id = id("493d07cc-5fc4-4087-a194-8a6eb49acf35");
        const auto basic_id = id("b4a8bd46-f46a-4490-be95-f8a149187deb");
        const auto animated_id = id("d7d3e58d-e9c4-4d97-abd6-9c5d3ce51722");
        const auto rule_id = id("58d2fe42-1091-4f7a-90c3-dda1cdfaec3d");
        const auto custom_id = id("a9ba355a-51e8-49e9-b581-c6174026c160");

        tiles::tile_set_document set;
        set.asset_id = set_id;
        set.name = "Complete Tiles";
        set.texture_asset_id = texture_id;
        set.texture_asset_ids = {second_texture_id, texture_id};
        set.cell_size = {32, 32};
        set.pixels_per_unit = 16.0;
        set.slicing.mode = tiles::slice_mode::automatic;
        set.slicing.cell_count = {4, 2};
        set.slicing.keep_empty_rects = true;
        set.slicing.pivot = {0.5, 0.25};

        tiles::tile_definition basic;
        basic.tile_id = basic_id;
        basic.name = "Ground";
        basic.source = {0, 0, 32, 32};
        basic.texture_asset_id = texture_id;
        basic.collider_mode = tiles::tile_collider_mode::sprite_outline;
        basic.collision_outline = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};

        tiles::tile_definition animated;
        animated.tile_id = animated_id;
        animated.name = "Water";
        animated.source = {32, 0, 32, 32};
        animated.texture_asset_id = texture_id;
        animated.kind = tiles::tile_kind::animated;
        animated.minimum_speed = 0.75;
        animated.maximum_speed = 1.25;
        animated.animation_start_frame = 1;
        animated.loop_once = true;
        animated.update_physics = true;
        animated.animation_frames = {
            {{texture_id, {32, 0, 32, 32}, {0.5, 0.5}}, 0.1},
            {{second_texture_id, {0, 0, 32, 32}, {0.5, 0.5}}, 0.2}};

        tiles::tile_definition rule;
        rule.tile_id = rule_id;
        rule.name = "Edge Rule";
        rule.source = {64, 0, 32, 32};
        rule.texture_asset_id = texture_id;
        rule.kind = tiles::tile_kind::rule;
        tiles::tile_rule rule_entry;
        rule_entry.topology = tiles::grid_layout::hex_point_top;
        rule_entry.match_transform = tiles::rule_match_transform::rotated;
        rule_entry.output_kind = tiles::rule_output_kind::random;
        rule_entry.neighbors = {{{-1, 0}, tiles::rule_neighbor_condition::same_tile, std::nullopt}};
        rule_entry.outputs = {{{set_id, basic_id}, 3.0}, {{second_set_id, animated_id}, 1.0}};
        rule.rules.push_back(rule_entry);

        tiles::tile_definition custom;
        custom.tile_id = custom_id;
        custom.name = "Custom";
        custom.source = {96, 0, 32, 32};
        custom.texture_asset_id = second_texture_id;
        custom.kind = tiles::tile_kind::custom;
        custom.custom_type_id = "example.weather-tile";
        custom.custom_type_version = 7;
        custom.opaque_payload_json = R"({"strength":0.75,"unknown":[1,2,3]})";
        custom.opaque_fields_json = R"({"futureTileField":{"keep":true}})";
        set.tiles = {rule, custom, basic, animated};
        set.opaque_fields_json = R"({"futureSetField":"preserve"})";

        const auto encoded_set = tiles::write_tile_set(set);
        const auto set_json = nlohmann::ordered_json::parse(encoded_set);
        require(set_json.at("formatVersion") == 2, "TileSet writer did not emit v2.");
        require(set_json.at("futureSetField") == "preserve", "TileSet opaque field was not emitted.");
        const auto decoded_set = tiles::read_tile_set(encoded_set);
        require(decoded_set.succeeded() && *decoded_set.document == set, "TileSet v2 did not round-trip.");
        require(tiles::write_tile_set(*decoded_set.document) == encoded_set, "TileSet v2 output was not deterministic.");

        const std::string v1_set = R"({
          "format":"dpe.tileset","formatVersion":1,
          "assetId":"4fe655df-c40f-4e48-a5cc-fbe9bd356ac6","name":"Legacy",
          "textureAssetId":"dd02cd2a-8a7e-4b27-9d26-06331093885a",
          "cellSize":{"x":16,"y":16},"margin":{"x":0,"y":0},"spacing":{"x":0,"y":0},
          "pixelsPerUnit":16,"futureLegacy":{"value":4},
          "tiles":[{"tileId":"b4a8bd46-f46a-4490-be95-f8a149187deb","name":"One",
            "source":{"x":0,"y":0,"width":16,"height":16},"collision":null,"futureTile":true}]
        })";
        const auto migrated_set = tiles::read_tile_set(v1_set);
        require(migrated_set.succeeded() && migrated_set.document->source_format_version == 1,
            "TileSet v1 did not migrate in memory.");
        require(migrated_set.document->tiles.front().texture_asset_id == texture_id,
            "TileSet v1 primary texture was not qualified during migration.");
        const auto migrated_set_json = nlohmann::ordered_json::parse(tiles::write_tile_set(*migrated_set.document));
        require(migrated_set_json.at("formatVersion") == 2 && migrated_set_json.contains("futureLegacy")
                && migrated_set_json.at("tiles").front().contains("futureTile"),
            "TileSet v1 unknown data was not preserved through v2 writing.");

        tiles::tile_palette_document palette;
        palette.asset_id = id("39fcb37c-b704-4981-afaa-6e90723518cf");
        palette.name = "Universal Palette";
        palette.tile_set_dependencies = {set_id, second_set_id};
        palette.cells = {{1, -1, {second_set_id, animated_id}, true, false, 1},
            {0, 0, {set_id, basic_id}}};
        palette.opaque_fields_json = R"({"futurePalette":9})";
        const auto encoded_palette = tiles::write_tile_palette(palette);
        const auto decoded_palette = tiles::read_tile_palette(encoded_palette);
        require(decoded_palette.succeeded() && *decoded_palette.document == palette,
            "TilePalette v1 did not round-trip.");
        require(tiles::write_tile_palette(*decoded_palette.document) == encoded_palette,
            "TilePalette output was not deterministic.");

        tiles::tilemap_document map;
        map.asset_id = id("fd2f3574-8e6f-43d1-bc96-c6650ab49a54");
        map.name = "Map";
        map.tile_set_dependencies = {set_id, second_set_id};
        map.grid.layout = tiles::grid_layout::isometric_z_as_y;
        map.grid.cell_size = {2.0, 1.0};
        map.grid.cell_gap = {0.1, 0.2};
        map.grid.tile_anchor = {0.5, 0.0};
        tiles::tile_cell cell;
        cell.index = 33;
        cell.tile_id = rule_id;
        cell.tile_set_id = set_id;
        cell.flip_x = true;
        cell.rotation_quarter_turns = 2;
        cell.tint = {0.2, 0.4, 0.6, 0.8};
        cell.offset = {0.25, -0.5};
        cell.rotation_degrees = 17.5;
        cell.scale = {1.5, 0.75};
        cell.elevation = 3;
        cell.lock_color = true;
        cell.lock_transform = true;
        tiles::tile_layer layer;
        layer.layer_id = id("59737391-9417-45bf-a8af-cb6e24e7aa38");
        layer.name = "Ground";
        layer.chunks = {{-1, 2, {cell}}};
        layer.tint = {0.9, 0.8, 0.7, 1.0};
        layer.material_asset_id = id("a6dd2d44-8f36-43df-875e-5449881c574a");
        layer.sort_order = -5;
        layer.renderer_mode = tiles::tile_renderer_mode::individual;
        layer.animation_rate = 1.5;
        layer.culling_padding = {2.0, 3.0};
        map.layers = {layer};
        map.opaque_fields_json = R"({"futureMap":true})";
        const auto encoded_map = tiles::write_tilemap(map);
        const auto decoded_map = tiles::read_tilemap(encoded_map);
        require(decoded_map.succeeded() && *decoded_map.document == map, "Tilemap v2 did not round-trip.");
        require(tiles::write_tilemap(*decoded_map.document) == encoded_map, "Tilemap v2 output was not deterministic.");

        const std::string v1_map = R"({
          "format":"dpe.tilemap","formatVersion":1,"chunkSize":32,
          "assetId":"fd2f3574-8e6f-43d1-bc96-c6650ab49a54","name":"Legacy Map",
          "tileSetDependencies":["4fe655df-c40f-4e48-a5cc-fbe9bd356ac6"],"futureMapV1":5,
          "layers":[{"layerId":"59737391-9417-45bf-a8af-cb6e24e7aa38","name":"Ground","visible":true,"order":0,
            "chunks":[{"x":0,"y":0,"cells":[{"index":0,"tileId":"b4a8bd46-f46a-4490-be95-f8a149187deb"}]}]}]
        })";
        const auto migrated_map = tiles::read_tilemap(v1_map);
        require(migrated_map.succeeded() && migrated_map.document->source_format_version == 1
                && migrated_map.document->layers.front().chunks.front().cells.front().tile_set_id == set_id,
            "Tilemap v1 did not qualify its cell reference during migration.");
        require(nlohmann::ordered_json::parse(tiles::write_tilemap(*migrated_map.document)).contains("futureMapV1"),
            "Tilemap v1 unknown field was not preserved.");

        require(!tiles::read_tile_palette(R"({"format":"dpe.tilepalette","formatVersion":2})").succeeded(),
            "Unsupported TilePalette version was accepted.");
        require(!tiles::read_tilemap(R"({"format":"dpe.tilemap","formatVersion":3})").succeeded(),
            "Unsupported Tilemap version was accepted.");

        for (const auto layout : {tiles::grid_layout::rectangular, tiles::grid_layout::hex_point_top,
                 tiles::grid_layout::hex_flat_top, tiles::grid_layout::isometric,
                 tiles::grid_layout::isometric_z_as_y})
        {
            tiles::tile_grid_settings grid;
            grid.layout = layout;
            grid.cell_size = {2.0, 2.0};
            grid.cell_gap = {0.0, 0.0};
            for (const auto point : {tiles::integer_point{-3, 2}, tiles::integer_point{0, 0}, tiles::integer_point{4, -5}})
            {
                const auto projected = tiles::project_cell(grid, point, 2);
                const auto round_trip = tiles::unproject_cell(grid, {projected.x, projected.y}, 2);
                require(round_trip == point, "Grid projection did not round-trip.");
            }
            const auto neighbors = tiles::neighbor_offsets(layout);
            require(neighbors.size() == (layout == tiles::grid_layout::hex_point_top
                            || layout == tiles::grid_layout::hex_flat_top ? 6U : 8U),
                "Grid topology returned the wrong neighbor count.");
            const auto line = tiles::grid_line(layout, {-3, 2}, {4, -5});
            require(!line.empty() && line.front() == tiles::integer_point{-3, 2}
                    && line.back() == tiles::integer_point{4, -5},
                "Grid line did not preserve its endpoints.");
            require(tiles::grid_collision_polygon(grid, {0, 0}, 1).size()
                    == (layout == tiles::grid_layout::hex_point_top
                            || layout == tiles::grid_layout::hex_flat_top ? 6U : 4U),
                "Grid collision polygon has the wrong shape.");
        }
        tiles::tile_grid_settings iso;
        iso.layout = tiles::grid_layout::isometric_z_as_y;
        iso.cell_size = {2.0, 1.0};
        const auto low = tiles::project_cell(iso, {1, 1}, 0);
        const auto high = tiles::project_cell(iso, {1, 1}, 3);
        require_close(high.y - low.y, 1.5, "Z-as-Y elevation was not projected consistently.");
        require(high.sort_key > low.sort_key, "Z-as-Y elevation did not affect sorting.");

        const tiles::tile_reference animated_reference{set_id, animated_id};
        const auto animated_evaluation = tiles::evaluate_tile(animated, animated_reference,
            tiles::grid_layout::rectangular, {4, 2}, 0.25, 77,
            [](tiles::integer_point) { return std::optional<tiles::tile_reference>{}; });
        require(animated_evaluation.sprite.has_value()
                && animated_evaluation.animation_frame < animated.animation_frames.size()
                && animated_evaluation.refresh_physics,
            "Animated Tile evaluation did not return a timed frame and physics intent.");

        const tiles::tile_reference rule_reference{set_id, rule_id};
        const auto map_id = id("f7d862d6-922a-49c2-8e64-f73f0a24aa00");
        const auto layer_id = id("f7d862d6-922a-49c2-8e64-f73f0a24aa01");
        const auto seed = tiles::stable_tile_seed(map_id, layer_id, {7, -3}, "rule");
        require(seed == tiles::stable_tile_seed(map_id, layer_id, {7, -3}, "rule")
                && seed != tiles::stable_tile_seed(map_id, layer_id, {8, -3}, "rule"),
            "Stable tile seeds were not deterministic and cell-specific.");
        const auto rule_evaluation = tiles::evaluate_tile(rule, rule_reference,
            tiles::grid_layout::hex_point_top, {0, 0}, 0.0, seed,
            [rule_reference](tiles::integer_point point) {
                return point == tiles::integer_point{0, -1}
                    ? std::optional{rule_reference} : std::nullopt;
            });
        require(rule_evaluation.matched_rule && rule_evaluation.topology_rotation_steps == 1
                && !rule_evaluation.output.tile_id.is_nil(),
            "Hex Rule Tile rotation or deterministic output evaluation failed.");
        require(rule_evaluation == tiles::evaluate_tile(rule, rule_reference,
                tiles::grid_layout::hex_point_top, {0, 0}, 0.0, seed,
                [rule_reference](tiles::integer_point point) {
                    return point == tiles::integer_point{0, -1}
                        ? std::optional{rule_reference} : std::nullopt;
                }),
            "Rule Tile output changed for the same stable seed.");

        auto fixed_rule = rule;
        fixed_rule.rules.front().match_transform = tiles::rule_match_transform::fixed;
        fixed_rule.rules.front().topology = tiles::grid_layout::rectangular;
        fixed_rule.rules.front().neighbors.clear();
        fixed_rule.rules.front().output_kind = tiles::rule_output_kind::fixed;
        fixed_rule.rules.front().outputs = {{{set_id, basic_id}, 1.0}};
        tiles::tile_definition override;
        override.tile_id = id("f7d862d6-922a-49c2-8e64-f73f0a24aa02");
        override.kind = tiles::tile_kind::rule_override;
        override.override_source = tiles::tile_reference{set_id, fixed_rule.tile_id};
        override.overrides = {{basic_id, {set_id, animated_id}}};
        const auto override_evaluation = tiles::evaluate_tile(override,
            {set_id, override.tile_id}, tiles::grid_layout::rectangular, {0, 0}, 0.0, seed,
            [](tiles::integer_point) { return std::optional<tiles::tile_reference>{}; },
            [&fixed_rule](tiles::tile_reference) { return &fixed_rule; });
        require(override_evaluation.output == animated_reference,
            "Rule Override did not replace its source Rule Tile output.");
        const auto placeholder = tiles::evaluate_tile(custom, {set_id, custom_id},
            tiles::grid_layout::rectangular, {}, 0.0, seed,
            [](tiles::integer_point) { return std::optional<tiles::tile_reference>{}; });
        require(placeholder.placeholder && placeholder.output.tile_id == custom_id,
            "Missing custom Tile behavior did not degrade to a lossless placeholder.");

        const auto random_one = tiles::random_brush_tile(rule.rules.front().outputs,
            map_id, layer_id, {9, 9});
        const auto random_two = tiles::random_brush_tile(rule.rules.front().outputs,
            map_id, layer_id, {9, 9});
        require(random_one == random_two, "Random Brush output was not stable.");
        const auto line_brush = tiles::line_brush(tiles::grid_layout::hex_point_top,
            {-2, 1}, {3, -2}, {{set_id, basic_id}, {set_id, animated_id}});
        require(!line_brush.empty() && line_brush.front().cell == tiles::integer_point{-2, 1}
                && line_brush.back().cell == tiles::integer_point{3, -2}
                && line_brush.at(2).tile == line_brush.front().tile,
            "Line Brush did not follow topology or repeat its pattern.");
        const auto group = tiles::group_pick(line_brush, {-2, -2}, {3, 1}, 1, 3);
        require(group.size() == 3 && group.front().u == 0 && group.front().v == 6,
            "Group Pick did not preserve logical gaps and limit.");

        tiles::tilemap_document sparse;
        sparse.asset_id = id("42d273e2-c54c-4260-8be5-7db3d5a1124a");
        sparse.name = "Sparse 1024 Square";
        sparse.tile_set_dependencies = {set_id, second_set_id};
        for (auto layer_index = 0; layer_index < 2; ++layer_index)
        {
            tiles::tile_layer sparse_layer;
            sparse_layer.layer_id = layer_index == 0
                ? id("0bac790c-5d34-4d4e-a8c0-529303335b71")
                : id("473b1a62-d5d7-488a-99bb-15709748f583");
            sparse_layer.name = layer_index == 0 ? "Ground" : "Details";
            sparse_layer.order = static_cast<unsigned>(layer_index);
            std::map<std::pair<int, int>, tiles::tile_chunk> chunks;
            for (auto y = 0; y < 1024; ++y)
            {
                for (const auto x : {y, 1023 - y})
                {
                    auto& chunk = chunks[{x / 32, y / 32}];
                    chunk.x = x / 32;
                    chunk.y = y / 32;
                    tiles::tile_cell sparse_cell;
                    sparse_cell.index = static_cast<std::uint16_t>(
                        (y % 32) * 32 + (x % 32));
                    sparse_cell.tile_set_id = (x + y + layer_index) % 2 == 0
                        ? set_id : second_set_id;
                    sparse_cell.tile_id = sparse_cell.tile_set_id == set_id
                        ? basic_id : animated_id;
                    sparse_cell.elevation = layer_index;
                    chunk.cells.push_back(sparse_cell);
                }
            }
            for (auto& [position, chunk] : chunks)
            {
                (void)position;
                std::sort(chunk.cells.begin(), chunk.cells.end(), [](const auto& left, const auto& right) {
                    return left.index < right.index;
                });
                sparse_layer.chunks.push_back(std::move(chunk));
            }
            sparse.layers.push_back(std::move(sparse_layer));
        }
        const auto sparse_bytes = tiles::write_tilemap(sparse);
        const auto sparse_read = tiles::read_tilemap(sparse_bytes);
        require(sparse_read.succeeded() && sparse_read.document->layers.size() == 2
                && sparse_read.document->tile_set_dependencies.size() == 2,
            "The multi-layer/multi-TileSet sparse 1024x1024 fixture did not round-trip.");
        std::size_t sparse_cells{};
        for (const auto& sparse_layer : sparse_read.document->layers)
            for (const auto& chunk : sparse_layer.chunks) sparse_cells += chunk.cells.size();
        require(sparse_cells == 4096,
            "The sparse 1024x1024 fixture lost occupied cells.");
        require(tiles::write_tilemap(*sparse_read.document) == sparse_bytes,
            "The sparse 1024x1024 fixture was not deterministic.");

        std::cout << "Tile document and grid tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
