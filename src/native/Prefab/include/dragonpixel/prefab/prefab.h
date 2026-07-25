#pragma once

#include <dragonpixel/core/diagnostic.h>
#include <dragonpixel/core/uuid.h>
#include <dragonpixel/scene/entity.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dragonpixel::prefab
{
struct entity_mapping final
{
    std::vector<core::uuid> nested_path;
    core::uuid source_entity_id;
    core::uuid instance_entity_id;

    friend bool operator==(const entity_mapping&, const entity_mapping&) = default;
};

struct instance_record final
{
    core::uuid instance_id;
    core::uuid source_asset_id;
    std::string source_revision;
    core::uuid root_entity_id;
    std::optional<core::uuid> placement_parent_id;
    std::vector<entity_mapping> entity_mappings;
    std::vector<nlohmann::ordered_json> overrides;
    std::vector<scene::entity> fallback_entities;

    friend bool operator==(const instance_record&, const instance_record&) = default;
};

struct document final
{
    core::uuid prefab_id;
    std::string engine_version{"0.2.0-slice2"};
    std::string revision;
    core::uuid root_entity_id;
    std::vector<scene::entity> entities;
    std::vector<instance_record> prefab_instances;
    std::vector<core::uuid> dependencies;

    friend bool operator==(const document&, const document&) = default;
};

struct load_result final
{
    std::optional<document> value;
    std::vector<core::diagnostic> diagnostics;
};

struct resolve_limits final
{
    std::size_t maximum_depth{32};
    std::size_t maximum_entities{100000};
};

using source_provider = std::function<const document*(const core::uuid& asset_id)>;

struct resolve_result final
{
    std::vector<scene::entity> entities;
    std::vector<core::diagnostic> diagnostics;
    bool used_fallback{};
};

struct rebase_allocation final
{
    std::vector<core::uuid> nested_path;
    core::uuid source_entity_id;
    core::uuid instance_entity_id;
};

struct rebase_result final
{
    instance_record instance;
    std::vector<core::diagnostic> diagnostics;
};

struct apply_result final
{
    std::optional<document> source;
    instance_record remaining_instance;
    std::vector<core::diagnostic> diagnostics;
};

[[nodiscard]] std::string write_json(const document& value);
[[nodiscard]] load_result read_json(std::string_view json);
[[nodiscard]] std::string compute_revision(const document& value);
[[nodiscard]] resolve_result resolve(
    const instance_record& instance,
    const source_provider& sources,
    resolve_limits limits = {});
[[nodiscard]] std::vector<core::diagnostic> validate_dependency_graph(
    const document& root,
    const source_provider& sources,
    resolve_limits limits = {});
[[nodiscard]] rebase_result rebase(
    const instance_record& current,
    const document& new_source,
    std::span<const rebase_allocation> allocations);
[[nodiscard]] apply_result apply_to_source(
    const instance_record& current,
    const document& target_source,
    std::span<const core::uuid> nesting_path = {});
[[nodiscard]] resolve_result unpack_completely(
    const instance_record& instance,
    const source_provider& sources,
    resolve_limits limits = {});
void revert_selected(instance_record& instance, std::span<const std::size_t> override_indexes);
void revert_all(instance_record& instance) noexcept;
}
