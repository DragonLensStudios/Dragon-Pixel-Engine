#pragma once

#include <dragonpixel/core/diagnostic.h>
#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/scene.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dragonpixel::serialization
{
struct migration_record final
{
    std::string type_id;
    std::uint32_t from_version{};
    std::uint32_t to_version{};
    std::string description;
};

struct scene_load_result final
{
    std::optional<scene::scene> value;
    std::vector<migration_record> migrations;
    std::vector<core::diagnostic> diagnostics;
};

[[nodiscard]] std::string write_scene_json(const scene::scene& value);
[[nodiscard]] scene_load_result read_scene_json(std::string_view json, const metadata::registry& registry);
[[nodiscard]] nlohmann::ordered_json canonicalize_json(const nlohmann::ordered_json& value);
}
