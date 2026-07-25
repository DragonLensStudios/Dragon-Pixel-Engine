#pragma once

#include <dragonpixel/core/diagnostic.h>
#include <dragonpixel/core/uuid.h>
#include <dragonpixel/metadata/descriptor.h>
#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/commands.h>

#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace dragonpixel::scene
{
class scene;

using entity_exists_callback = std::function<bool(const core::uuid&)>;
using compatible_asset_callback = std::function<bool(
    std::string_view asset_reference,
    const metadata::property_descriptor& property)>;

struct command_validation_context final
{
    explicit command_validation_context(const metadata::registry& registry) : descriptors(registry) {}

    const metadata::registry& descriptors;
    const scene* current_scene{};
    entity_exists_callback entity_exists;
    compatible_asset_callback asset_is_compatible;
};

struct command_validation_result final
{
    bool succeeded{};
    std::vector<command> commands;
    std::vector<core::diagnostic> diagnostics;
};

// Validates metadata-owned component mutations without changing authoring state.
// The normalized command vector is populated only when every input command is
// valid, so callers cannot accidentally apply a partially validated transaction.
[[nodiscard]] command_validation_result validate_and_normalize_commands(
    std::span<const command> commands,
    const command_validation_context& context);
}
