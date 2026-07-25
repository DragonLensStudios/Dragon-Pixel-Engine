#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/prefab/prefab.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace
{
using dragonpixel::core::uuid;
using dragonpixel::prefab::document;
using dragonpixel::prefab::entity_mapping;
using dragonpixel::prefab::instance_record;
using dragonpixel::scene::component_record;
using dragonpixel::scene::entity;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error{message};
    }
}

uuid id(const char* value)
{
    const auto parsed = uuid::parse(value);
    require(parsed.has_value(), "Invalid fixture UUID.");
    return *parsed;
}

component_record transform(double x = 0.0)
{
    return {
        std::string{dragonpixel::metadata::builtin_component_ids::transform},
        2,
        dragonpixel::metadata::runtime_owner::native,
        {{"dpe.transform.position", {{"x", x}, {"y", 0.0}, {"z", 0.0}}}},
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Native.TransformComponent",
    };
}

document make_leaf()
{
    const auto prefab_id = id("10000000-0000-4000-8000-000000000001");
    const auto root_id = id("10000000-0000-4000-8000-000000000002");
    const auto child_id = id("10000000-0000-4000-8000-000000000003");
    document value;
    value.prefab_id = prefab_id;
    value.root_entity_id = root_id;
    value.entities = {
        entity{root_id, "Leaf Root", std::nullopt, {transform()}, true, 0},
        entity{child_id, "Leaf Child", root_id, {transform(1.0)}, true, 0},
    };
    value.revision = dragonpixel::prefab::compute_revision(value);
    return value;
}

document make_middle(const document& leaf)
{
    const auto root_id = id("20000000-0000-4000-8000-000000000002");
    instance_record nested;
    nested.instance_id = id("20000000-0000-4000-8000-000000000003");
    nested.source_asset_id = leaf.prefab_id;
    nested.source_revision = leaf.revision;
    nested.root_entity_id = id("20000000-0000-4000-8000-000000000004");
    nested.placement_parent_id = root_id;

    document value;
    value.prefab_id = id("20000000-0000-4000-8000-000000000001");
    value.root_entity_id = root_id;
    value.entities = {entity{root_id, "Middle Root", std::nullopt, {transform()}, true, 0}};
    value.prefab_instances = {nested};
    value.dependencies = {leaf.prefab_id};
    value.revision = dragonpixel::prefab::compute_revision(value);
    return value;
}

void round_trip_preserves_opaque_records()
{
    auto value = make_leaf();
    nlohmann::ordered_json raw{
        {"typeId", "33333333-3333-4333-8333-333333333333"},
        {"schemaVersion", 44},
        {"owner", "vendor-runtime"},
        {"enabled", true},
        {"qualifiedName", "Vendor.FutureComponent"},
        {"properties", {{"value", 42}}},
        {"vendorPayload", {{"preserve", true}}},
    };
    value.entities.front().components.push_back({
        "33333333-3333-4333-8333-333333333333",
        44,
        dragonpixel::metadata::runtime_owner::native,
        raw.at("properties"),
        true,
        raw,
        true,
        "Vendor.FutureComponent",
    });
    value.revision = dragonpixel::prefab::compute_revision(value);

    const auto first = dragonpixel::prefab::write_json(value);
    const auto loaded = dragonpixel::prefab::read_json(first);
    require(loaded.value.has_value(), "Prefab did not parse after serialization.");
    const auto& loaded_raw = loaded.value->entities.front().components.back().raw_record;
    require(loaded_raw.at("vendorPayload") == raw.at("vendorPayload")
            && loaded_raw.at("properties") == raw.at("properties")
            && loaded_raw.at("schemaVersion") == raw.at("schemaVersion"),
        "Opaque prefab component payload did not round-trip.");
    require(dragonpixel::prefab::write_json(*loaded.value) == first,
        "Prefab serialization was not deterministic.");
}

void nested_resolution_applies_overrides_and_fallback()
{
    auto leaf = make_leaf();
    auto middle = make_middle(leaf);
    const auto nested_id = middle.prefab_instances.front().instance_id;
    const auto outer_root = id("30000000-0000-4000-8000-000000000003");
    const auto leaf_root_instance = id("30000000-0000-4000-8000-000000000004");
    const auto leaf_child_instance = id("30000000-0000-4000-8000-000000000005");

    instance_record instance;
    instance.instance_id = id("30000000-0000-4000-8000-000000000002");
    instance.source_asset_id = middle.prefab_id;
    instance.source_revision = middle.revision;
    instance.root_entity_id = outer_root;
    instance.entity_mappings = {
        entity_mapping{{}, middle.root_entity_id, outer_root},
        entity_mapping{{nested_id}, leaf.root_entity_id, leaf_root_instance},
        entity_mapping{{nested_id}, leaf.entities.back().id, leaf_child_instance},
    };
    nlohmann::ordered_json target{
        {"sourceEntityId", leaf.entities.back().id.to_string()},
        {"nestedPath", {nested_id.to_string()}},
    };
    instance.overrides = {
        {{"op", "rename-entity"}, {"target", target}, {"value", "Overridden Child"}},
        {
            {"op", "set-property"},
            {"target", target},
            {"componentTypeId", std::string{dragonpixel::metadata::builtin_component_ids::transform}},
            {"propertyId", "dpe.transform.position"},
            {"value", {{"x", 9.0}, {"y", 0.0}, {"z", 0.0}}},
        },
    };

    std::unordered_map<uuid, const document*, dragonpixel::core::uuid_hash> sources{
        {leaf.prefab_id, &leaf}, {middle.prefab_id, &middle},
    };
    const auto provider = [&](const uuid& asset_id) -> const document* {
        const auto found = sources.find(asset_id);
        return found == sources.end() ? nullptr : found->second;
    };
    const auto resolved = dragonpixel::prefab::resolve(instance, provider);
    require(resolved.entities.size() == 3, "Nested prefab resolution did not materialize all entities.");
    const auto child = std::find_if(resolved.entities.begin(), resolved.entities.end(), [&](const auto& value) {
        return value.id == leaf_child_instance;
    });
    require(child != resolved.entities.end() && child->name == "Overridden Child",
        "Nested entity override was not applied through its stable mapping path.");
    require(child->components.front().properties.at("dpe.transform.position").at("x") == 9.0,
        "Nested component property override was not applied.");
    const auto unpacked = dragonpixel::prefab::unpack_completely(instance, provider);
    require(unpacked.entities == resolved.entities
            && unpacked.diagnostics.size() == resolved.diagnostics.size(),
        "Unpack Completely did not materialize the same deterministic flattened entities.");

    instance.fallback_entities = resolved.entities;
    sources.erase(middle.prefab_id);
    const auto fallback = dragonpixel::prefab::resolve(instance, provider);
    require(fallback.used_fallback && fallback.entities == instance.fallback_entities,
        "Missing source did not retain the last resolved fallback.");
}

void cycles_rebase_and_revert_are_deterministic()
{
    auto first = make_leaf();
    auto second = make_middle(first);
    first.dependencies = {second.prefab_id};
    first.revision = dragonpixel::prefab::compute_revision(first);
    std::unordered_map<uuid, const document*, dragonpixel::core::uuid_hash> sources{
        {first.prefab_id, &first}, {second.prefab_id, &second},
    };
    const auto provider = [&](const uuid& asset_id) -> const document* {
        const auto found = sources.find(asset_id);
        return found == sources.end() ? nullptr : found->second;
    };
    const auto diagnostics = dragonpixel::prefab::validate_dependency_graph(first, provider);
    require(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& value) {
        return value.code == "DPE.PREFAB.CYCLE";
    }), "Indirect prefab dependency cycle was not rejected.");

    instance_record instance;
    instance.instance_id = id("40000000-0000-4000-8000-000000000001");
    instance.source_asset_id = first.prefab_id;
    instance.source_revision = first.revision;
    instance.root_entity_id = id("40000000-0000-4000-8000-000000000002");
    const auto child_instance_id = id("40000000-0000-4000-8000-000000000005");
    instance.entity_mappings = {
        {{}, first.root_entity_id, instance.root_entity_id},
        {{}, first.entities.back().id, child_instance_id},
    };
    instance.overrides = {
        {{"op", "rename-entity"}, {"target", {{"sourceEntityId", first.root_entity_id.to_string()}}}, {"value", "Override"}},
        {
            {"op", "reparent-entity"},
            {"target", {{"sourceEntityId", first.entities.back().id.to_string()}}},
            {"value", instance.root_entity_id.to_string()},
        },
        {
            {"op", "add-component"},
            {"target", {{"sourceEntityId", first.entities.back().id.to_string()}}},
            {"componentTypeId", std::string{dragonpixel::metadata::builtin_component_ids::rotator}},
            {"component", {
                {"enabled", true},
                {"owner", "managed"},
                {"properties", {{"dpe.rotator.target", instance.root_entity_id.to_string()}}},
                {"qualifiedName", "DragonPixel.Managed.RotatorComponent"},
                {"schemaVersion", 1},
                {"typeId", std::string{dragonpixel::metadata::builtin_component_ids::rotator}},
            }},
        },
    };

    const auto applied = dragonpixel::prefab::apply_to_source(instance, first);
    require(applied.source.has_value()
            && applied.source->entities.front().name == "Override"
            && applied.source->entities.back().parent_id == first.root_entity_id
            && applied.source->entities.back().components.back().properties.at("dpe.rotator.target")
                == first.root_entity_id.to_string()
            && applied.remaining_instance.overrides.empty()
            && applied.remaining_instance.source_revision == applied.source->revision,
        "Apply did not update the explicit source level and consume its normalized override.");

    auto changed = first;
    const auto new_source_id = id("40000000-0000-4000-8000-000000000003");
    const auto new_instance_id = id("40000000-0000-4000-8000-000000000004");
    changed.entities.push_back(entity{new_source_id, "New Child", changed.root_entity_id, {transform()}, true, 0});
    changed.revision = dragonpixel::prefab::compute_revision(changed);
    const dragonpixel::prefab::rebase_allocation allocation{{}, new_source_id, new_instance_id};
    const auto rebased = dragonpixel::prefab::rebase(instance, changed, std::span{&allocation, std::size_t{1}});
    require(rebased.instance.source_revision == changed.revision
            && rebased.instance.entity_mappings.size() == 3
            && rebased.instance.overrides == instance.overrides,
        "Rebase did not retain mappings/overrides and allocate the added source entity.");
    auto reverted = rebased.instance;
    reverted.overrides.push_back({{"op", "set-entity-enabled"}, {"target", {{"sourceEntityId", first.root_entity_id.to_string()}}}, {"value", false}});
    const std::size_t selected_override = 0;
    dragonpixel::prefab::revert_selected(reverted, std::span{&selected_override, std::size_t{1}});
    require(reverted.overrides.size() == 3 && reverted.overrides.back().value("op", "") == "set-entity-enabled",
        "Revert Selected removed the wrong normalized override.");
    dragonpixel::prefab::revert_all(reverted);
    require(reverted.overrides.empty(), "Revert All did not clear normalized overrides.");
}
}

int main()
{
    try
    {
        round_trip_preserves_opaque_records();
        nested_resolution_applies_overrides_and_fallback();
        cycles_rebase_and_revert_are_deterministic();
        std::cout << "Linked prefab tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Linked prefab test failure: " << exception.what() << '\n';
        return 1;
    }
}
