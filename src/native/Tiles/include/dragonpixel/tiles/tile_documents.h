#pragma once

#include <dragonpixel/core/uuid.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dragonpixel::tiles
{
struct integer_point final
{
    int x{};
    int y{};
    friend bool operator==(const integer_point&, const integer_point&) = default;
};

struct source_rectangle final
{
    int x{};
    int y{};
    int width{1};
    int height{1};
    friend bool operator==(const source_rectangle&, const source_rectangle&) = default;
};

struct collision_rectangle final
{
    double offset_x{};
    double offset_y{};
    double width{1.0};
    double height{1.0};
    friend bool operator==(const collision_rectangle&, const collision_rectangle&) = default;
};

struct tile_definition final
{
    core::uuid tile_id;
    std::string name;
    source_rectangle source;
    std::optional<collision_rectangle> collision;
    friend bool operator==(const tile_definition&, const tile_definition&) = default;
};

struct tile_set_document final
{
    core::uuid asset_id;
    std::string name;
    core::uuid texture_asset_id;
    integer_point cell_size{32, 32};
    integer_point margin{};
    integer_point spacing{};
    double pixels_per_unit{32.0};
    std::vector<tile_definition> tiles;
    friend bool operator==(const tile_set_document&, const tile_set_document&) = default;
};

struct tile_cell final
{
    unsigned index{};
    core::uuid tile_id;
    bool flip_x{};
    bool flip_y{};
    unsigned rotation_quarter_turns{};
    friend bool operator==(const tile_cell&, const tile_cell&) = default;
};

struct tile_chunk final
{
    int x{};
    int y{};
    std::vector<tile_cell> cells;
    friend bool operator==(const tile_chunk&, const tile_chunk&) = default;
};

struct tile_layer final
{
    core::uuid layer_id;
    std::string name;
    bool visible{true};
    unsigned order{};
    std::vector<tile_chunk> chunks;
    friend bool operator==(const tile_layer&, const tile_layer&) = default;
};

struct tilemap_document final
{
    core::uuid asset_id;
    std::string name;
    std::vector<core::uuid> tile_set_dependencies;
    std::vector<tile_layer> layers;
    friend bool operator==(const tilemap_document&, const tilemap_document&) = default;
};

template <typename Document>
struct document_result final
{
    std::optional<Document> document;
    std::string error;
    [[nodiscard]] bool succeeded() const noexcept { return document.has_value(); }
};

[[nodiscard]] document_result<tile_set_document> read_tile_set(std::string_view json);
[[nodiscard]] document_result<tilemap_document> read_tilemap(std::string_view json);
[[nodiscard]] std::string write_tile_set(const tile_set_document& document);
[[nodiscard]] std::string write_tilemap(const tilemap_document& document);
}
