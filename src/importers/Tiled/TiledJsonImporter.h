#pragma once

#include <dragonpixel/core/uuid.h>

#include <filesystem>
#include <string>
#include <vector>

namespace dragonpixel::importers::tiled
{
struct import_request final
{
    std::filesystem::path source_map;
    std::filesystem::path staging_directory;
    std::string name;
    core::uuid tilemap_asset_id;
    core::uuid tileset_asset_id;
    core::uuid texture_asset_id;
    double pixels_per_unit{32.0};
};

struct import_diagnostic final
{
    std::string code;
    std::string message;
    std::filesystem::path path;
};

struct import_result final
{
    bool succeeded{};
    std::filesystem::path tilemap_path;
    std::filesystem::path tileset_path;
    std::filesystem::path texture_path;
    std::size_t tile_count{};
    std::size_t layer_count{};
    std::size_t cell_count{};
    std::vector<import_diagnostic> diagnostics;
};

[[nodiscard]] import_result import_tiled_json(const import_request& request);

// Executes the versioned worker request and writes result.json below the
// operation-owned staging directory whenever the request envelope is valid.
[[nodiscard]] int execute_request_file(
    const std::filesystem::path& request_path,
    std::string& error);
}
