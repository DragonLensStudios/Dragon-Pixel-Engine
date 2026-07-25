#pragma once

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/prefab/prefab.h>
#include <dragonpixel/scene/scene.h>

#include <QString>
#include <QStringList>
#include <QByteArray>

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

struct PrefabOperationResult final
{
    bool succeeded{};
    QString message;
    QStringList diagnostics;
    std::vector<dragonpixel::core::uuid> selection;
};

struct PrefabHydrationResult final
{
    std::optional<dragonpixel::scene::scene> scene;
    QStringList diagnostics;
};

struct PrefabCommandResult final
{
    bool succeeded{true};
    std::vector<dragonpixel::scene::command> commands;
    QStringList diagnostics;
};

struct PrefabApplyLevel final
{
    QString label;
    std::vector<dragonpixel::core::uuid> nesting_path;
};

class PrefabService final
{
public:
    void set_metadata(const dragonpixel::metadata::registry* metadata) noexcept
    {
        metadata_ = metadata;
    }
    void set_project_root(QString project_root);
    void clear();

    [[nodiscard]] PrefabHydrationResult hydrate(const dragonpixel::scene::scene& stored_scene);
    [[nodiscard]] dragonpixel::scene::scene persistent_copy(
        const dragonpixel::scene::scene& authoring_scene) const;
    [[nodiscard]] PrefabCommandResult augment_commands(
        const dragonpixel::scene::scene& current_scene,
        std::span<const dragonpixel::scene::command> commands) const;
    void synchronize(const dragonpixel::scene::scene& current_scene);
    [[nodiscard]] bool prepare_undo_source_change(
        std::size_t current_history_position,
        QStringList& diagnostics);
    [[nodiscard]] bool prepare_redo_source_change(
        std::size_t target_history_position,
        QStringList& diagnostics);
    void truncate_source_journal(std::size_t history_position);

    [[nodiscard]] bool is_linked_entity(const dragonpixel::core::uuid& entity_id) const;
    [[nodiscard]] bool has_instance_for_entity(const dragonpixel::core::uuid& entity_id) const;
    [[nodiscard]] bool source_available_for_entity(const dragonpixel::core::uuid& entity_id) const;
    [[nodiscard]] bool has_overrides_for_entity(const dragonpixel::core::uuid& entity_id) const;
    [[nodiscard]] std::vector<PrefabApplyLevel> apply_levels_for_entity(
        const dragonpixel::core::uuid& entity_id) const;

    [[nodiscard]] PrefabOperationResult instantiate(
        dragonpixel::scene::scene& current_scene,
        const QString& source_path,
        const std::optional<dragonpixel::core::uuid>& placement_parent);
    [[nodiscard]] PrefabOperationResult create_from_selection(
        dragonpixel::scene::scene& current_scene,
        const dragonpixel::core::uuid& root_entity_id,
        const QString& destination_path);
    [[nodiscard]] PrefabOperationResult apply(
        dragonpixel::scene::scene& current_scene,
        const dragonpixel::core::uuid& selected_entity_id,
        std::span<const dragonpixel::core::uuid> nesting_path);
    [[nodiscard]] PrefabOperationResult revert_selected(
        dragonpixel::scene::scene& current_scene,
        const dragonpixel::core::uuid& selected_entity_id);
    [[nodiscard]] PrefabOperationResult revert_all(
        dragonpixel::scene::scene& current_scene,
        const dragonpixel::core::uuid& selected_entity_id);
    [[nodiscard]] PrefabOperationResult repair_rebase(
        dragonpixel::scene::scene& current_scene,
        const dragonpixel::core::uuid& selected_entity_id);
    [[nodiscard]] PrefabOperationResult unpack(
        dragonpixel::scene::scene& current_scene,
        const dragonpixel::core::uuid& selected_entity_id,
        bool completely);

private:
    struct SourceEntry final
    {
        dragonpixel::prefab::document document;
        QString path;
    };

    struct Ownership final
    {
        std::size_t instance_index{};
        dragonpixel::core::uuid instance_id;
        dragonpixel::core::uuid source_asset_id;
        dragonpixel::core::uuid source_entity_id;
        std::vector<dragonpixel::core::uuid> nested_path;
        bool root{};
        bool has_overrides{};
    };

    struct SourceJournalEntry final
    {
        std::size_t history_position_after{};
        QString path;
        std::optional<QByteArray> before;
        QByteArray after;
        bool applied{true};
    };

    void refresh_sources(QStringList* diagnostics = nullptr);
    [[nodiscard]] const SourceEntry* load_source_path(
        const QString& path,
        QStringList& diagnostics);
    [[nodiscard]] const SourceEntry* source(
        const dragonpixel::core::uuid& asset_id) const;
    [[nodiscard]] std::optional<dragonpixel::prefab::instance_record> parse_instance(
        const nlohmann::ordered_json& value,
        QStringList* diagnostics = nullptr) const;
    [[nodiscard]] nlohmann::ordered_json instance_json(
        const dragonpixel::prefab::instance_record& value,
        const nlohmann::ordered_json* preserve = nullptr) const;
    void rebuild_ownership(const nlohmann::ordered_json& instances);
    void allocate_mappings(
        const dragonpixel::prefab::document& source_document,
        std::vector<dragonpixel::core::uuid> path,
        dragonpixel::prefab::instance_record& instance,
        QStringList& diagnostics) const;
    [[nodiscard]] PrefabOperationResult replace_instance(
        dragonpixel::scene::scene& current_scene,
        std::size_t instance_index,
        dragonpixel::prefab::instance_record instance,
        QString description);
    [[nodiscard]] std::vector<dragonpixel::scene::command> materialization_commands(
        const dragonpixel::scene::scene& current_scene,
        std::vector<dragonpixel::scene::entity> entities) const;
    [[nodiscard]] std::optional<std::size_t> instance_index_for_entity(
        const dragonpixel::core::uuid& entity_id) const;

    QString project_root_;
    const dragonpixel::metadata::registry* metadata_{};
    std::unordered_map<dragonpixel::core::uuid, SourceEntry, dragonpixel::core::uuid_hash> sources_;
    std::unordered_map<dragonpixel::core::uuid, Ownership, dragonpixel::core::uuid_hash> ownership_;
    std::vector<SourceJournalEntry> source_journal_;
};
