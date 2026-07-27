#include "PrefabService.h"

#include <dragonpixel/serialization/scene_json.h>

#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <functional>
#include <iterator>
#include <set>
#include <unordered_set>
#include <utility>

namespace
{
using dragonpixel::core::diagnostic;
using dragonpixel::core::diagnostic_severity;
using dragonpixel::core::uuid;
using dragonpixel::prefab::document;
using dragonpixel::prefab::entity_mapping;
using dragonpixel::prefab::instance_record;
using dragonpixel::scene::command;
using dragonpixel::scene::component_record;
using dragonpixel::scene::entity;
using json = nlohmann::ordered_json;

constexpr auto wrapper_prefab_id = "f0000000-0000-4000-8000-000000000001";
constexpr auto wrapper_root_id = "f0000000-0000-4000-8000-000000000002";
constexpr std::size_t maximum_prefab_depth = 32;
constexpr std::size_t maximum_prefab_entities = 100000;

QString diagnostic_text(const diagnostic& value)
{
    auto text = QStringLiteral("%1: %2")
                    .arg(QString::fromStdString(value.code), QString::fromStdString(value.message));
    if (!value.context.empty())
    {
        text += QStringLiteral(" [%1]").arg(QString::fromStdString(value.context));
    }
    return text;
}

bool has_errors(const std::vector<diagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& value) {
        return value.severity == diagnostic_severity::error;
    });
}

json component_json(const component_record& component)
{
    if (component.opaque)
    {
        return component.raw_record;
    }
    return {
        {"enabled", component.enabled},
        {"owner", component.owner == dragonpixel::metadata::runtime_owner::managed ? "managed" : "native"},
        {"properties", component.properties},
        {"qualifiedName", component.qualified_name},
        {"schemaVersion", component.schema_version},
        {"typeId", component.type_id},
    };
}

template <typename OwnershipType>
json target_json(const OwnershipType& ownership)
{
    json path = json::array();
    for (const auto& id : ownership.nested_path)
    {
        path.push_back(id.to_string());
    }
    return {
        {"nestedPath", std::move(path)},
        {"sourceEntityId", ownership.source_entity_id.to_string()},
    };
}

QString override_key(const json& operation)
{
    const auto op = QString::fromStdString(operation.value("op", std::string{}));
    const auto target = QString::fromStdString(operation.value("target", json::object()).dump());
    const auto component = QString::fromStdString(operation.value("componentTypeId", std::string{}));
    const auto property = QString::fromStdString(operation.value("propertyId", std::string{}));
    return QStringLiteral("%1|%2|%3|%4").arg(op, target, component, property);
}

void append_normalized_override(json& instance, json operation)
{
    if (!instance.contains("overrides") || !instance["overrides"].is_array())
    {
        instance["overrides"] = json::array();
    }
    const auto key = override_key(operation);
    auto& overrides = instance["overrides"];
    overrides.erase(std::remove_if(overrides.begin(), overrides.end(), [&](const auto& existing) {
        return existing.is_object() && override_key(existing) == key;
    }), overrides.end());
    overrides.push_back(dragonpixel::serialization::canonicalize_json(operation));
}

bool same_path(std::span<const uuid> left, const json& right)
{
    if (!right.is_array() || right.size() != left.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (!right[index].is_string() || right[index].get<std::string>() != left[index].to_string())
        {
            return false;
        }
    }
    return true;
}

bool write_atomic(const QString& path, const QByteArray& bytes, QString& error)
{
    QSaveFile output{path};
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    {
        error = output.errorString();
        return false;
    }
    return true;
}

QString apply_backup_path(const QString& source_path)
{
    return source_path + QStringLiteral(".dpebak");
}

QString apply_recovery_marker_path(const QString& source_path)
{
    return source_path + QStringLiteral(".dpeapply");
}

QByteArray sha256_hex(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray apply_recovery_marker(const QByteArray& before, const QByteArray& after)
{
    return QByteArray::fromStdString(json{
        {"afterSha256", sha256_hex(after).toStdString()},
        {"beforeSha256", sha256_hex(before).toStdString()},
        {"format", "dpe.prefab-apply-recovery"},
        {"formatVersion", 1},
    }.dump(2) + "\n");
}

bool source_revision_is_current(const instance_record& instance, const document& source_document)
{
    return instance.source_revision.empty()
        || instance.source_revision == dragonpixel::prefab::compute_revision(source_document);
}

void remove_recovery_artifact(
    const QString& path,
    QStringList& diagnostics,
    const QString& diagnostic_code)
{
    if (QFileInfo::exists(path) && !QFile::remove(path))
    {
        diagnostics.push_back(QStringLiteral("%1: Could not remove recovery artifact %2.")
                                  .arg(diagnostic_code, path));
    }
}

bool path_within(const QString& root, const QString& candidate)
{
    if (root.isEmpty())
    {
        return false;
    }
#if defined(Q_OS_WIN)
    constexpr auto path_case = Qt::CaseInsensitive;
#else
    constexpr auto path_case = Qt::CaseSensitive;
#endif
    const auto normalized_root = QDir::fromNativeSeparators(QDir{root}.canonicalPath());
    if (normalized_root.isEmpty())
    {
        return false;
    }
    const QFileInfo candidate_info{candidate};
    auto normalized_candidate = QDir::fromNativeSeparators(candidate_info.canonicalFilePath());
    if (normalized_candidate.isEmpty())
    {
        auto ancestor = candidate_info.absoluteDir();
        auto suffix = candidate_info.fileName();
        while (!ancestor.exists())
        {
            const auto directory_name = QFileInfo{ancestor.absolutePath()}.fileName();
            if (directory_name.isEmpty() || !ancestor.cdUp())
            {
                return false;
            }
            suffix = directory_name + QLatin1Char{'/'} + suffix;
        }
        const auto canonical_ancestor = ancestor.canonicalPath();
        if (canonical_ancestor.isEmpty())
        {
            return false;
        }
        normalized_candidate = QDir::fromNativeSeparators(
            QDir{canonical_ancestor}.absoluteFilePath(suffix));
    }
    return normalized_candidate.startsWith(normalized_root + QLatin1Char{'/'}, path_case);
}

std::optional<uuid> parsed_id(const char* value)
{
    return uuid::parse(value);
}
}

void PrefabService::set_project_root(QString project_root)
{
    project_root_ = QDir::cleanPath(QFileInfo{std::move(project_root)}.absoluteFilePath());
    refresh_sources();
}

void PrefabService::clear()
{
    project_root_.clear();
    sources_.clear();
    ownership_.clear();
    source_journal_.clear();
}

void PrefabService::refresh_sources(QStringList* diagnostics)
{
    sources_.clear();
    if (project_root_.isEmpty() || !QDir{project_root_}.exists())
    {
        return;
    }

    const auto append_diagnostic = [&](QString value) {
        if (diagnostics != nullptr)
        {
            diagnostics->push_back(std::move(value));
        }
    };
    QStringList recovery_markers;
    QDirIterator recovery_iterator{
        project_root_,
        {QStringLiteral("*.dpeprefab.dpeapply")},
        QDir::Files,
        QDirIterator::Subdirectories};
    while (recovery_iterator.hasNext())
    {
        recovery_markers.push_back(QDir::cleanPath(recovery_iterator.next()));
    }
    recovery_markers.sort(Qt::CaseInsensitive);
    for (const auto& marker_path : recovery_markers)
    {
        auto source_path = marker_path;
        source_path.chop(QStringLiteral(".dpeapply").size());
        const auto backup_path = apply_backup_path(source_path);
        if (!path_within(project_root_, marker_path)
            || !path_within(project_root_, source_path)
            || !path_within(project_root_, backup_path))
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_OUTSIDE_PROJECT: Pending Apply evidence resolved outside the project root and was not read or modified: %1")
                                  .arg(marker_path));
            continue;
        }
        QFile marker{marker_path};
        if (!marker.open(QIODevice::ReadOnly))
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_MARKER_INVALID: Pending Apply marker %1 was unreadable.")
                                  .arg(marker_path));
            continue;
        }
        const auto marker_json = json::parse(marker.readAll().toStdString(), nullptr, false);
        marker.close();
        if (marker_json.is_discarded() || !marker_json.is_object()
            || marker_json.value("format", std::string{}) != "dpe.prefab-apply-recovery"
            || marker_json.value("formatVersion", 0) != 1
            || !marker_json.contains("beforeSha256") || !marker_json["beforeSha256"].is_string()
            || !marker_json.contains("afterSha256") || !marker_json["afterSha256"].is_string())
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_MARKER_INVALID: Pending Apply marker %1 was malformed or incompatible.")
                                  .arg(marker_path));
            continue;
        }
        QFile backup{backup_path};
        if (!backup.open(QIODevice::ReadOnly))
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_BACKUP_MISSING: Pending Apply marker %1 has no readable recovery copy.")
                                  .arg(marker_path));
            continue;
        }
        const auto before = backup.readAll();
        backup.close();
        const auto expected_before = marker_json["beforeSha256"].get<std::string>();
        const auto expected_after = marker_json["afterSha256"].get<std::string>();
        if (sha256_hex(before).toStdString() != expected_before)
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_BACKUP_MISMATCH: Recovery copy %1 did not match its recorded hash; no source was changed.")
                                  .arg(backup_path));
            continue;
        }
        bool already_restored = false;
        if (QFileInfo::exists(source_path))
        {
            QFile current{source_path};
            if (!current.open(QIODevice::ReadOnly))
            {
                append_diagnostic(QStringLiteral(
                    "DPE.PREFAB.RECOVERY_CONFLICT: Current prefab source %1 was unreadable; no recovery was attempted.")
                                      .arg(source_path));
                continue;
            }
            const auto current_hash = sha256_hex(current.readAll()).toStdString();
            current.close();
            already_restored = current_hash == expected_before;
            if (!already_restored && current_hash != expected_after)
            {
                append_diagnostic(QStringLiteral(
                    "DPE.PREFAB.RECOVERY_CONFLICT: Current prefab source %1 no longer matched the interrupted Apply; no recovery was attempted.")
                                      .arg(source_path));
                continue;
            }
        }
        QString error;
        if (!already_restored && !write_atomic(source_path, before, error))
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_RESTORE_FAILED: Could not restore %1: %2")
                                  .arg(source_path, error));
            continue;
        }
        if (!QFile::remove(marker_path))
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_CLEANUP_FAILED: Restored %1 but could not remove its pending marker.")
                                  .arg(source_path));
            continue;
        }
        if (QFileInfo::exists(backup_path) && !QFile::remove(backup_path))
        {
            append_diagnostic(QStringLiteral(
                "DPE.PREFAB.RECOVERY_CLEANUP_FAILED: Restored %1 but could not remove its recovery copy.")
                                  .arg(source_path));
        }
        append_diagnostic(QStringLiteral(
            "DPE.PREFAB.RECOVERY_RESTORED: Restored the prefab source before-image for interrupted Apply %1.")
                              .arg(source_path));
    }

    QStringList paths;
    QDirIterator iterator{
        project_root_,
        {QStringLiteral("*.dpeprefab")},
        QDir::Files,
        QDirIterator::Subdirectories};
    while (iterator.hasNext())
    {
        paths.push_back(QDir::cleanPath(iterator.next()));
    }
    paths.sort(Qt::CaseInsensitive);
    for (const auto& path : paths)
    {
        QStringList local_diagnostics;
        if (!path_within(project_root_, path))
        {
            local_diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.OUTSIDE_PROJECT: Indexed prefab source resolved outside the project root and was ignored: %1")
                                            .arg(path));
        }
        else
        {
            QFile file{path};
            if (!file.open(QIODevice::ReadOnly))
            {
                local_diagnostics.push_back(
                    QStringLiteral("DPE.PREFAB.READ_FAILED: %1").arg(file.errorString()));
            }
            else
            {
                const auto loaded = dragonpixel::prefab::read_json(file.readAll().toStdString());
                for (const auto& item : loaded.diagnostics)
                {
                    local_diagnostics.push_back(diagnostic_text(item));
                }
                if (loaded.value)
                {
                    const auto [found, inserted] = sources_.emplace(
                        loaded.value->prefab_id,
                        SourceEntry{std::move(*loaded.value), path});
                    if (!inserted)
                    {
                        local_diagnostics.push_back(QStringLiteral(
                            "DPE.PREFAB.DUPLICATE_ID: %1 and %2 declare the same prefabId")
                                                        .arg(found->second.path, path));
                    }
                }
            }
        }
        if (diagnostics != nullptr)
        {
            diagnostics->append(local_diagnostics);
        }
    }
}

const PrefabService::SourceEntry* PrefabService::load_source_path(
    const QString& path,
    QStringList& diagnostics)
{
    const auto absolute_path = QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
    if (!path_within(project_root_, absolute_path))
    {
        diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.OUTSIDE_PROJECT: Prefab sources must remain below the project root."));
        return nullptr;
    }
    QFile file{absolute_path};
    if (!file.open(QIODevice::ReadOnly))
    {
        diagnostics.push_back(QStringLiteral("DPE.PREFAB.READ_FAILED: %1").arg(file.errorString()));
        return nullptr;
    }
    auto loaded = dragonpixel::prefab::read_json(file.readAll().toStdString());
    for (const auto& item : loaded.diagnostics)
    {
        diagnostics.push_back(diagnostic_text(item));
    }
    if (!loaded.value)
    {
        return nullptr;
    }
    const auto id = loaded.value->prefab_id;
    sources_.insert_or_assign(id, SourceEntry{std::move(*loaded.value), absolute_path});
    return source(id);
}

const PrefabService::SourceEntry* PrefabService::source(const uuid& asset_id) const
{
    const auto found = sources_.find(asset_id);
    return found == sources_.end() ? nullptr : &found->second;
}

std::optional<instance_record> PrefabService::parse_instance(
    const json& value,
    QStringList* diagnostics) const
{
    json wrapper{
        {"$schema", "https://dragonpixel.dev/schemas/v1/prefab.schema.json"},
        {"dependencies", json::array()},
        {"engineVersion", "0.2.0-slice2"},
        {"entities", json::array({
            {
                {"components", json::array()},
                {"enabled", true},
                {"id", wrapper_root_id},
                {"name", "Wrapper Root"},
                {"parentId", nullptr},
                {"siblingOrder", 0},
            },
        })},
        {"format", "dpe.prefab"},
        {"formatVersion", 1},
        {"prefabId", wrapper_prefab_id},
        {"prefabInstances", json::array({value})},
        {"revision", ""},
        {"rootEntityId", wrapper_root_id},
    };
    auto loaded = dragonpixel::prefab::read_json(wrapper.dump());
    if (!loaded.value || loaded.value->prefab_instances.size() != 1)
    {
        if (diagnostics != nullptr)
        {
            for (const auto& item : loaded.diagnostics)
            {
                if (item.code != "DPE.PREFAB.STALE_REVISION")
                {
                    diagnostics->push_back(diagnostic_text(item));
                }
            }
            diagnostics->push_back(QStringLiteral(
                "DPE.PREFAB.INVALID_INSTANCE: Scene prefab instance record could not be parsed."));
        }
        return std::nullopt;
    }
    return std::move(loaded.value->prefab_instances.front());
}

json PrefabService::instance_json(const instance_record& value, const json* preserve) const
{
    document wrapper;
    wrapper.prefab_id = *parsed_id(wrapper_prefab_id);
    wrapper.root_entity_id = *parsed_id(wrapper_root_id);
    wrapper.prefab_instances = {value};
    const auto serialized = json::parse(dragonpixel::prefab::write_json(wrapper));
    auto result = preserve != nullptr && preserve->is_object() ? *preserve : json::object();
    for (const auto& [key, child] : serialized.at("prefabInstances").at(0).items())
    {
        result[key] = child;
    }
    return dragonpixel::serialization::canonicalize_json(result);
}

PrefabHydrationResult PrefabService::hydrate(const dragonpixel::scene::scene& stored_scene)
{
    PrefabHydrationResult result;
    refresh_sources(&result.diagnostics);
    std::vector<entity> entities{stored_scene.entities().begin(), stored_scene.entities().end()};
    std::unordered_set<uuid, dragonpixel::core::uuid_hash> ids;
    bool identity_collision = false;
    bool unresolved_without_fallback = false;
    for (const auto& item : entities)
    {
        ids.insert(item.id);
    }

    const auto provider = [this](const uuid& asset_id) -> const document* {
        const auto* entry = source(asset_id);
        return entry == nullptr ? nullptr : &entry->document;
    };
    const auto& instances = stored_scene.prefab_instances();
    if (!instances.is_array())
    {
        result.diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.INVALID_ENVELOPE: Scene prefabInstances was not an array."));
        return result;
    }
    for (const auto& raw : instances)
    {
        auto instance = parse_instance(raw, &result.diagnostics);
        if (!instance)
        {
            continue;
        }
        dragonpixel::prefab::resolve_result resolved;
        const auto* current_source = source(instance->source_asset_id);
        if (current_source != nullptr && !source_revision_is_current(*instance, current_source->document))
        {
            resolved.entities = instance->fallback_entities;
            resolved.used_fallback = true;
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.SOURCE_REVISION_MISMATCH: Source %1 changed from revision %2 to %3; the last resolved fallback was retained until Repair/Rebase.")
                                             .arg(
                                                 QString::fromStdString(instance->source_asset_id.to_string()),
                                                 QString::fromStdString(instance->source_revision),
                                                 QString::fromStdString(dragonpixel::prefab::compute_revision(
                                                     current_source->document))));
        }
        else
        {
            resolved = dragonpixel::prefab::resolve(*instance, provider);
        }
        for (const auto& item : resolved.diagnostics)
        {
            result.diagnostics.push_back(diagnostic_text(item));
        }
        if (has_errors(resolved.diagnostics))
        {
            if (instance->fallback_entities.empty())
            {
                unresolved_without_fallback = true;
                result.diagnostics.push_back(QStringLiteral(
                    "DPE.PREFAB.CANDIDATE_REJECTED: Prefab resolution failed and no last-known-good fallback was available."));
                continue;
            }
            resolved.entities = instance->fallback_entities;
            resolved.used_fallback = true;
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.RESOLUTION_FALLBACK: Prefab resolution failed validation; the complete last-known-good fallback was retained instead of partial materialization."));
        }
        if (resolved.used_fallback && resolved.entities.empty())
        {
            unresolved_without_fallback = true;
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.CANDIDATE_REJECTED: The prefab source was unavailable or incompatible and no fallback entities were available."));
            continue;
        }
        for (const auto& materialized : resolved.entities)
        {
            if (!ids.insert(materialized.id).second)
            {
                result.diagnostics.push_back(QStringLiteral(
                    "DPE.PREFAB.ENTITY_COLLISION: Materialized entity %1 collides with saved authoring state.")
                        .arg(QString::fromStdString(materialized.id.to_string())));
                identity_collision = true;
                continue;
            }
            entities.push_back(materialized);
        }
    }

    if (identity_collision || unresolved_without_fallback)
    {
        if (identity_collision)
        {
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.CANDIDATE_REJECTED: The scene was not opened because accepting a linked/local identity collision could lose local data on save."));
        }
        ownership_.clear();
        return result;
    }

    dragonpixel::scene::scene_document_extras extras;
    extras.physics = stored_scene.physics_settings();
    extras.prefab_instances = instances;
    extras.has_explicit_sibling_order = true;
    result.scene.emplace(stored_scene.id(), stored_scene.name(), std::move(entities), std::move(extras));
    result.scene->mark_savepoint();
    rebuild_ownership(instances);
    return result;
}

dragonpixel::scene::scene PrefabService::persistent_copy(
    const dragonpixel::scene::scene& authoring_scene) const
{
    std::vector<entity> local_entities;
    for (const auto& item : authoring_scene.entities())
    {
        if (!ownership_.contains(item.id))
        {
            local_entities.push_back(item);
        }
    }
    dragonpixel::scene::scene_document_extras extras;
    extras.physics = authoring_scene.physics_settings();
    extras.prefab_instances = authoring_scene.prefab_instances();
    if (extras.prefab_instances.is_array())
    {
        for (std::size_t index = 0; index < extras.prefab_instances.size(); ++index)
        {
            auto parsed = parse_instance(extras.prefab_instances[index]);
            if (!parsed)
            {
                continue;
            }
            std::unordered_set<uuid, dragonpixel::core::uuid_hash> linked_ids;
            for (const auto& mapping : parsed->entity_mappings)
            {
                linked_ids.insert(mapping.instance_entity_id);
            }
            for (const auto& fallback : parsed->fallback_entities)
            {
                linked_ids.insert(fallback.id);
            }
            std::vector<entity> fallback;
            for (const auto& candidate : authoring_scene.entities())
            {
                if (linked_ids.contains(candidate.id))
                {
                    fallback.push_back(candidate);
                }
            }
            parsed->fallback_entities = std::move(fallback);
            extras.prefab_instances[index] = instance_json(*parsed, &extras.prefab_instances[index]);
        }
    }
    extras.has_explicit_sibling_order = true;
    return {authoring_scene.id(), authoring_scene.name(), std::move(local_entities), std::move(extras)};
}

void PrefabService::rebuild_ownership(const json& instances)
{
    ownership_.clear();
    if (!instances.is_array())
    {
        return;
    }
    for (std::size_t index = 0; index < instances.size(); ++index)
    {
        const auto parsed = parse_instance(instances[index]);
        if (!parsed)
        {
            continue;
        }
        for (const auto& mapping : parsed->entity_mappings)
        {
            ownership_.insert_or_assign(mapping.instance_entity_id, Ownership{
                index,
                parsed->instance_id,
                parsed->source_asset_id,
                parsed->source_revision,
                mapping.source_entity_id,
                mapping.nested_path,
                mapping.instance_entity_id == parsed->root_entity_id,
                !parsed->overrides.empty(),
            });
        }
        for (const auto& fallback : parsed->fallback_entities)
        {
            if (!ownership_.contains(fallback.id))
            {
                ownership_.insert_or_assign(fallback.id, Ownership{
                    index,
                    parsed->instance_id,
                    parsed->source_asset_id,
                    parsed->source_revision,
                    fallback.id,
                    {},
                    fallback.id == parsed->root_entity_id,
                    !parsed->overrides.empty(),
                });
            }
        }
    }
}

void PrefabService::synchronize(const dragonpixel::scene::scene& current_scene)
{
    rebuild_ownership(current_scene.prefab_instances());
}

void PrefabService::truncate_source_journal(std::size_t history_position)
{
    std::erase_if(source_journal_, [&](const auto& entry) {
        return entry.history_position_after > history_position;
    });
}

bool PrefabService::prepare_undo_source_change(
    std::size_t current_history_position,
    QStringList& diagnostics)
{
    const auto found = std::find_if(source_journal_.rbegin(), source_journal_.rend(), [&](const auto& entry) {
        return entry.applied && entry.history_position_after == current_history_position;
    });
    if (found == source_journal_.rend())
    {
        return true;
    }
    QFile current{found->path};
    if (!current.open(QIODevice::ReadOnly) || current.readAll() != found->after)
    {
        diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.UNDO_SOURCE_CONFLICT: The prefab source changed externally; Undo was blocked to avoid overwriting it."));
        return false;
    }
    current.close();
    QString error;
    if (found->before)
    {
        if (!write_atomic(found->path, *found->before, error))
        {
            diagnostics.push_back(QStringLiteral("DPE.PREFAB.UNDO_SOURCE_FAILED: %1").arg(error));
            return false;
        }
    }
    else if (!QFile::remove(found->path))
    {
        diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.UNDO_SOURCE_FAILED: The newly created prefab source could not be removed."));
        return false;
    }
    found->applied = false;
    refresh_sources();
    return true;
}

bool PrefabService::prepare_redo_source_change(
    std::size_t target_history_position,
    QStringList& diagnostics)
{
    const auto found = std::find_if(source_journal_.begin(), source_journal_.end(), [&](const auto& entry) {
        return !entry.applied && entry.history_position_after == target_history_position;
    });
    if (found == source_journal_.end())
    {
        return true;
    }
    if (found->before)
    {
        QFile current{found->path};
        if (!current.open(QIODevice::ReadOnly) || current.readAll() != *found->before)
        {
            diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.REDO_SOURCE_CONFLICT: The prefab source changed externally; Redo was blocked to avoid overwriting it."));
            return false;
        }
        current.close();
    }
    else if (QFileInfo::exists(found->path))
    {
        diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.REDO_SOURCE_CONFLICT: A file now occupies the prefab creation path; Redo was blocked."));
        return false;
    }
    QString error;
    if (!write_atomic(found->path, found->after, error))
    {
        diagnostics.push_back(QStringLiteral("DPE.PREFAB.REDO_SOURCE_FAILED: %1").arg(error));
        return false;
    }
    found->applied = true;
    refresh_sources();
    return true;
}

bool PrefabService::is_linked_entity(const uuid& entity_id) const
{
    return ownership_.contains(entity_id);
}

bool PrefabService::has_instance_for_entity(const uuid& entity_id) const
{
    return instance_index_for_entity(entity_id).has_value();
}

std::optional<std::size_t> PrefabService::instance_index_for_entity(const uuid& entity_id) const
{
    const auto found = ownership_.find(entity_id);
    return found == ownership_.end() ? std::nullopt : std::optional<std::size_t>{found->second.instance_index};
}

bool PrefabService::source_available_for_entity(const uuid& entity_id) const
{
    const auto found = ownership_.find(entity_id);
    if (found == ownership_.end())
    {
        return false;
    }
    const auto* current_source = source(found->second.source_asset_id);
    return current_source != nullptr
        && (found->second.source_revision.empty()
            || found->second.source_revision
                == dragonpixel::prefab::compute_revision(current_source->document));
}

bool PrefabService::has_overrides_for_entity(const uuid& entity_id) const
{
    const auto found = ownership_.find(entity_id);
    if (found == ownership_.end())
    {
        return false;
    }
    return found->second.has_overrides;
}

PrefabCommandResult PrefabService::augment_commands(
    const dragonpixel::scene::scene& current_scene,
    std::span<const command> commands) const
{
    PrefabCommandResult result;
    result.commands.assign(commands.begin(), commands.end());
    auto instances = current_scene.prefab_instances();
    if (!instances.is_array())
    {
        result.succeeded = false;
        result.diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.INVALID_ENVELOPE: Prefab overrides require an array envelope."));
        return result;
    }
    bool changed = false;

    const auto linked_descendant = [&](const uuid& root_id) {
        for (const auto& [linked_id, ignored] : ownership_)
        {
            static_cast<void>(ignored);
            auto current = current_scene.find_entity(linked_id);
            while (current != nullptr && current->parent_id)
            {
                if (*current->parent_id == root_id)
                {
                    return true;
                }
                current = current_scene.find_entity(*current->parent_id);
            }
        }
        return false;
    };

    for (const auto& value : commands)
    {
        std::optional<uuid> target_id;
        json operation;
        bool unsupported = false;
        std::visit([&](const auto& concrete) {
            using command_type = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<command_type, dragonpixel::scene::create_entity_command>
                || std::is_same_v<command_type, dragonpixel::scene::create_preset_command>)
            {
                if (concrete.parent_id && is_linked_entity(*concrete.parent_id))
                {
                    unsupported = true;
                }
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::duplicate_subtree_command>)
            {
                target_id = concrete.root_entity_id;
                unsupported = is_linked_entity(*target_id) || linked_descendant(*target_id);
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::delete_subtree_command>)
            {
                target_id = concrete.root_entity_id;
                if (!is_linked_entity(*target_id) && linked_descendant(*target_id))
                {
                    unsupported = true;
                }
                operation = {{"op", "remove-entity"}};
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::rename_entity_command>)
            {
                target_id = concrete.entity_id;
                operation = {{"op", "rename-entity"}, {"value", concrete.name}};
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::set_entity_enabled_command>)
            {
                target_id = concrete.entity_id;
                operation = {{"op", "set-entity-enabled"}, {"value", concrete.enabled}};
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::reparent_entity_command>)
            {
                target_id = concrete.entity_id;
                if (!is_linked_entity(*target_id) && concrete.parent_id && is_linked_entity(*concrete.parent_id))
                {
                    unsupported = true;
                }
                operation = {
                    {"op", "reparent-entity"},
                    {"value", concrete.parent_id ? concrete.parent_id->to_string() : std::string{}},
                };
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::reorder_entity_command>)
            {
                target_id = concrete.entity_id;
                operation = {{"op", "reorder-entity"}, {"value", concrete.sibling_index}};
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::upsert_component_command>)
            {
                target_id = concrete.entity_id;
                operation = {
                    {"op", "add-component"},
                    {"componentTypeId", concrete.component.type_id},
                    {"component", component_json(concrete.component)},
                };
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::remove_component_command>)
            {
                target_id = concrete.entity_id;
                operation = {{"op", "remove-component"}, {"componentTypeId", concrete.type_id}};
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::reorder_component_command>)
            {
                target_id = concrete.entity_id;
                operation = {
                    {"op", "reorder-component"},
                    {"componentTypeId", concrete.type_id},
                    {"value", concrete.destination_index},
                };
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::set_component_enabled_command>)
            {
                target_id = concrete.entity_id;
                operation = {
                    {"op", "set-component-enabled"},
                    {"componentTypeId", concrete.type_id},
                    {"value", concrete.enabled},
                };
            }
            else if constexpr (std::is_same_v<command_type, dragonpixel::scene::set_component_property_command>)
            {
                target_id = concrete.entity_id;
                operation = {
                    {"op", "set-property"},
                    {"componentTypeId", concrete.type_id},
                    {"propertyId", concrete.property_id},
                    {"value", concrete.value},
                };
            }
        }, value);

        if (unsupported)
        {
            result.succeeded = false;
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.UNSUPPORTED_CROSS_BOUNDARY_EDIT: Unpack the linked instance before duplicating it or parenting local state through it."));
            return result;
        }
        if (!target_id)
        {
            continue;
        }
        const auto owner = ownership_.find(*target_id);
        if (owner == ownership_.end())
        {
            continue;
        }
        if (owner->second.instance_index >= instances.size() || !instances[owner->second.instance_index].is_object())
        {
            result.succeeded = false;
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.MISSING_INSTANCE: Linked entity ownership no longer matches the scene envelope."));
            return result;
        }
        operation["target"] = target_json(owner->second);
        append_normalized_override(instances[owner->second.instance_index], std::move(operation));
        changed = true;
    }
    if (changed)
    {
        result.commands.emplace_back(dragonpixel::scene::set_prefab_instances_command{std::move(instances)});
    }
    return result;
}

bool PrefabService::allocate_mappings(
    const document& source_document,
    std::vector<uuid> path,
    instance_record& instance,
    QStringList& diagnostics) const
{
    std::size_t entity_count = 0;
    std::vector<uuid> dependency_stack;
    std::function<bool(const document&, std::vector<uuid>, std::size_t)> visit =
        [&](const document& current, std::vector<uuid> current_path, std::size_t depth) {
            if (depth > maximum_prefab_depth)
            {
                diagnostics.push_back(QStringLiteral(
                    "DPE.PREFAB.DEPTH_LIMIT: Prefab mapping allocation exceeded the configured nesting depth at %1.")
                                          .arg(QString::fromStdString(current.prefab_id.to_string())));
                return false;
            }
            if (std::find(dependency_stack.begin(), dependency_stack.end(), current.prefab_id)
                != dependency_stack.end())
            {
                diagnostics.push_back(QStringLiteral(
                    "DPE.PREFAB.CYCLE: Prefab mapping allocation encountered a direct or indirect dependency cycle at %1.")
                                          .arg(QString::fromStdString(current.prefab_id.to_string())));
                return false;
            }
            dependency_stack.push_back(current.prefab_id);
            for (const auto& source_entity : current.entities)
            {
                if (entity_count >= maximum_prefab_entities)
                {
                    diagnostics.push_back(QStringLiteral(
                        "DPE.PREFAB.ENTITY_LIMIT: Prefab mapping allocation exceeded the configured entity limit at %1.")
                                              .arg(QString::fromStdString(current.prefab_id.to_string())));
                    dependency_stack.pop_back();
                    return false;
                }
                ++entity_count;
                const auto exists = std::any_of(
                    instance.entity_mappings.begin(), instance.entity_mappings.end(), [&](const auto& mapping) {
                        return mapping.source_entity_id == source_entity.id
                            && mapping.nested_path == current_path;
                    });
                if (!exists)
                {
                    instance.entity_mappings.push_back(
                        {current_path, source_entity.id, uuid::random_v4()});
                }
            }
            for (const auto& nested : current.prefab_instances)
            {
                auto nested_path = current_path;
                nested_path.push_back(nested.instance_id);
                const auto* dependency = source(nested.source_asset_id);
                if (dependency != nullptr)
                {
                    if (!visit(dependency->document, std::move(nested_path), depth + 1))
                    {
                        dependency_stack.pop_back();
                        return false;
                    }
                    continue;
                }
                diagnostics.push_back(QStringLiteral(
                    "DPE.PREFAB.MISSING_SOURCE: Allocating stable fallback mappings for unavailable nested source %1.")
                                          .arg(QString::fromStdString(nested.source_asset_id.to_string())));
                for (const auto& fallback : nested.fallback_entities)
                {
                    if (entity_count >= maximum_prefab_entities)
                    {
                        diagnostics.push_back(QStringLiteral(
                            "DPE.PREFAB.ENTITY_LIMIT: Nested prefab fallback mapping allocation exceeded the configured entity limit."));
                        dependency_stack.pop_back();
                        return false;
                    }
                    ++entity_count;
                    const auto exists = std::any_of(
                        instance.entity_mappings.begin(), instance.entity_mappings.end(), [&](const auto& mapping) {
                            return mapping.source_entity_id == fallback.id
                                && mapping.nested_path == nested_path;
                        });
                    if (!exists)
                    {
                        instance.entity_mappings.push_back(
                            {nested_path, fallback.id, uuid::random_v4()});
                    }
                }
            }
            dependency_stack.pop_back();
            return true;
        };
    return visit(source_document, std::move(path), 0);
}

std::vector<command> PrefabService::materialization_commands(
    const dragonpixel::scene::scene& current_scene,
    std::vector<entity> entities) const
{
    static_cast<void>(current_scene);
    const auto depth = [&](const entity& value) {
        std::size_t result = 0;
        auto parent = value.parent_id;
        while (parent && result <= entities.size())
        {
            const auto found = std::find_if(entities.begin(), entities.end(), [&](const auto& candidate) {
                return candidate.id == *parent;
            });
            if (found == entities.end())
            {
                break;
            }
            ++result;
            parent = found->parent_id;
        }
        return result;
    };
    std::sort(entities.begin(), entities.end(), [&](const auto& left, const auto& right) {
        const auto left_depth = depth(left);
        const auto right_depth = depth(right);
        if (left_depth != right_depth)
        {
            return left_depth < right_depth;
        }
        if (left.parent_id != right.parent_id)
        {
            return left.parent_id < right.parent_id;
        }
        if (left.sibling_order != right.sibling_order)
        {
            return left.sibling_order < right.sibling_order;
        }
        return left.id < right.id;
    });

    std::vector<command> commands;
    for (const auto& item : entities)
    {
        commands.emplace_back(dragonpixel::scene::create_entity_command{
            item.id, item.name, item.parent_id, std::nullopt});
        if (!item.enabled)
        {
            commands.emplace_back(dragonpixel::scene::set_entity_enabled_command{item.id, false});
        }
        for (const auto& component : item.components)
        {
            commands.emplace_back(dragonpixel::scene::upsert_component_command{item.id, component});
        }
    }
    return commands;
}

PrefabOperationResult PrefabService::instantiate(
    dragonpixel::scene::scene& current_scene,
    const QString& source_path,
    const std::optional<uuid>& placement_parent)
{
    PrefabOperationResult result;
    const auto* entry = load_source_path(source_path, result.diagnostics);
    if (entry == nullptr)
    {
        result.message = QStringLiteral("Prefab source could not be loaded.");
        return result;
    }

    instance_record instance;
    instance.instance_id = uuid::random_v4();
    instance.source_asset_id = entry->document.prefab_id;
    instance.source_revision = dragonpixel::prefab::compute_revision(entry->document);
    instance.placement_parent_id = placement_parent;
    if (!allocate_mappings(entry->document, {}, instance, result.diagnostics))
    {
        result.message = QStringLiteral(
            "Prefab mapping allocation rejected an unsafe dependency graph before scene mutation.");
        return result;
    }
    const auto root_mapping = std::find_if(instance.entity_mappings.begin(), instance.entity_mappings.end(),
        [&](const auto& mapping) {
            return mapping.nested_path.empty() && mapping.source_entity_id == entry->document.root_entity_id;
        });
    if (root_mapping == instance.entity_mappings.end())
    {
        result.message = QStringLiteral("Prefab root has no stable entity mapping.");
        return result;
    }
    instance.root_entity_id = root_mapping->instance_entity_id;

    const auto provider = [this](const uuid& asset_id) -> const document* {
        const auto* found = source(asset_id);
        return found == nullptr ? nullptr : &found->document;
    };
    auto resolved = dragonpixel::prefab::resolve(instance, provider);
    for (const auto& item : resolved.diagnostics)
    {
        result.diagnostics.push_back(diagnostic_text(item));
    }
    if (has_errors(resolved.diagnostics))
    {
        result.message = QStringLiteral("Prefab resolution failed validation.");
        return result;
    }
    for (const auto& item : resolved.entities)
    {
        if (current_scene.find_entity(item.id) != nullptr)
        {
            result.message = QStringLiteral("Prefab entity mapping collided with existing scene identity.");
            return result;
        }
    }
    instance.fallback_entities = resolved.entities;
    auto instances = current_scene.prefab_instances();
    instances.push_back(instance_json(instance));
    std::vector<command> commands;
    commands.emplace_back(dragonpixel::scene::set_prefab_instances_command{std::move(instances)});
    auto materialize = materialization_commands(current_scene, std::move(resolved.entities));
    commands.insert(commands.end(),
        std::make_move_iterator(materialize.begin()),
        std::make_move_iterator(materialize.end()));
    const auto previous_history_position = current_scene.history_position();
    const auto applied = current_scene.apply_transaction(commands, "Instantiate linked prefab");
    if (!applied.succeeded)
    {
        result.message = applied.diagnostic
            ? QString::fromStdString(applied.diagnostic->message)
            : QStringLiteral("Prefab instantiation transaction was rejected.");
        return result;
    }
    truncate_source_journal(previous_history_position);
    synchronize(current_scene);
    result.succeeded = true;
    result.selection = {instance.root_entity_id};
    result.message = QStringLiteral("Instantiated linked prefab %1 with %2 stable entity mapping(s).")
                         .arg(QFileInfo{source_path}.completeBaseName())
                         .arg(instance.entity_mappings.size());
    return result;
}

PrefabOperationResult PrefabService::create_from_selection(
    dragonpixel::scene::scene& current_scene,
    const uuid& root_entity_id,
    const QString& destination_path)
{
    PrefabOperationResult result;
    const auto* root = current_scene.find_entity(root_entity_id);
    if (root == nullptr)
    {
        result.message = QStringLiteral("Select a valid GameObject subtree before creating a prefab.");
        return result;
    }
    auto path = destination_path;
    if (!path.endsWith(QStringLiteral(".dpeprefab"), Qt::CaseInsensitive))
    {
        path += QStringLiteral(".dpeprefab");
    }
    path = QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
    if (!path_within(project_root_, path))
    {
        result.message = QStringLiteral("Prefab destination must remain below the project root.");
        return result;
    }
    if (QFileInfo::exists(path))
    {
        result.message = QStringLiteral("Create from Selection will not overwrite an existing prefab source.");
        return result;
    }

    std::vector<entity> selected_entities;
    std::unordered_set<uuid, dragonpixel::core::uuid_hash> selected_ids{root_entity_id};
    for (std::size_t frontier = 0; frontier < selected_ids.size(); ++frontier)
    {
        std::vector<uuid> snapshot{selected_ids.begin(), selected_ids.end()};
        for (const auto& candidate : current_scene.entities())
        {
            if (candidate.parent_id && selected_ids.contains(*candidate.parent_id))
            {
                selected_ids.insert(candidate.id);
            }
        }
        if (snapshot.size() == selected_ids.size())
        {
            break;
        }
    }
    for (const auto& candidate : current_scene.entities())
    {
        if (!selected_ids.contains(candidate.id))
        {
            continue;
        }
        if (is_linked_entity(candidate.id))
        {
            result.message = QStringLiteral(
                "Create from Selection currently requires a locally owned subtree; unpack linked children first.");
            return result;
        }
        selected_entities.push_back(candidate);
    }
    if (selected_entities.empty())
    {
        result.message = QStringLiteral("The selected subtree was empty.");
        return result;
    }

    bool external_reference = false;
    std::function<void(const json&)> inspect_references = [&](const json& value) {
        if (value.is_string())
        {
            const auto referenced = uuid::parse(value.get<std::string>());
            if (referenced && current_scene.find_entity(*referenced) != nullptr
                && !selected_ids.contains(*referenced))
            {
                external_reference = true;
            }
        }
        else if (value.is_array() || value.is_object())
        {
            for (const auto& child : value)
            {
                inspect_references(child);
            }
        }
    };
    for (const auto& selected : selected_entities)
    {
        for (const auto& component : selected.components)
        {
            inspect_references(component.opaque ? component.raw_record : component.properties);
        }
    }
    if (external_reference)
    {
        result.message = QStringLiteral(
            "Prefab creation rejected a reference from reusable content to a scene-local GameObject outside the selection.");
        return result;
    }

    document source_document;
    source_document.prefab_id = uuid::random_v4();
    source_document.root_entity_id = root_entity_id;
    source_document.entities = selected_entities;
    for (auto& selected : source_document.entities)
    {
        if (selected.id == root_entity_id)
        {
            selected.parent_id.reset();
            selected.sibling_order = 0;
        }
    }
    source_document.revision = dragonpixel::prefab::compute_revision(source_document);
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    const auto source_bytes = QByteArray::fromStdString(dragonpixel::prefab::write_json(source_document));
    QString error;
    if (!write_atomic(path, source_bytes, error))
    {
        result.message = QStringLiteral("Could not create prefab source: %1").arg(error);
        return result;
    }

    instance_record instance;
    instance.instance_id = uuid::random_v4();
    instance.source_asset_id = source_document.prefab_id;
    instance.source_revision = source_document.revision;
    instance.root_entity_id = root_entity_id;
    instance.placement_parent_id = root->parent_id;
    instance.fallback_entities = selected_entities;
    for (const auto& selected : selected_entities)
    {
        instance.entity_mappings.push_back({{}, selected.id, selected.id});
    }
    auto instances = current_scene.prefab_instances();
    instances.push_back(instance_json(instance));
    const auto previous_history_position = current_scene.history_position();
    const auto applied = current_scene.apply(
        command{dragonpixel::scene::set_prefab_instances_command{std::move(instances)}},
        "Create linked prefab from selection");
    if (!applied.succeeded)
    {
        QFile::remove(path);
        result.message = QStringLiteral("Scene rejected the linked prefab provenance transaction.");
        return result;
    }
    truncate_source_journal(previous_history_position);
    sources_.insert_or_assign(source_document.prefab_id, SourceEntry{std::move(source_document), path});
    source_journal_.push_back({
        current_scene.history_position(), path, std::nullopt, source_bytes, true});
    synchronize(current_scene);
    result.succeeded = true;
    result.selection = {root_entity_id};
    result.message = QStringLiteral("Created linked prefab source %1 from %2 GameObject(s).")
                         .arg(path)
                         .arg(selected_entities.size());
    return result;
}

PrefabOperationResult PrefabService::replace_instance(
    dragonpixel::scene::scene& current_scene,
    std::size_t instance_index,
    instance_record instance,
    QString description)
{
    PrefabOperationResult result;
    auto instances = current_scene.prefab_instances();
    if (!instances.is_array() || instance_index >= instances.size())
    {
        result.message = QStringLiteral("Prefab instance envelope no longer matches the selected GameObject.");
        return result;
    }
    const auto provider = [this](const uuid& asset_id) -> const document* {
        const auto* found = source(asset_id);
        return found == nullptr ? nullptr : &found->document;
    };
    auto resolved = dragonpixel::prefab::resolve(instance, provider);
    for (const auto& item : resolved.diagnostics)
    {
        result.diagnostics.push_back(diagnostic_text(item));
    }
    if (has_errors(resolved.diagnostics))
    {
        result.message = QStringLiteral("Prefab rematerialization failed validation.");
        return result;
    }
    if (!resolved.used_fallback)
    {
        instance.fallback_entities = resolved.entities;
    }
    instances[instance_index] = instance_json(instance, &instances[instance_index]);

    std::vector<command> commands;
    if (current_scene.find_entity(instance.root_entity_id) != nullptr)
    {
        commands.emplace_back(dragonpixel::scene::delete_subtree_command{instance.root_entity_id});
    }
    commands.emplace_back(dragonpixel::scene::set_prefab_instances_command{std::move(instances)});
    auto materialize = materialization_commands(current_scene, std::move(resolved.entities));
    commands.insert(commands.end(),
        std::make_move_iterator(materialize.begin()),
        std::make_move_iterator(materialize.end()));
    const auto previous_history_position = current_scene.history_position();
    const auto applied = current_scene.apply_transaction(commands, description.toStdString());
    if (!applied.succeeded)
    {
        result.message = applied.diagnostic
            ? QString::fromStdString(applied.diagnostic->message)
            : QStringLiteral("Prefab rematerialization transaction was rejected.");
        return result;
    }
    truncate_source_journal(previous_history_position);
    synchronize(current_scene);
    result.succeeded = true;
    result.selection = {instance.root_entity_id};
    result.message = std::move(description);
    return result;
}

std::vector<PrefabApplyLevel> PrefabService::apply_levels_for_entity(const uuid& entity_id) const
{
    std::vector<PrefabApplyLevel> result;
    const auto index = instance_index_for_entity(entity_id);
    if (!index)
    {
        return result;
    }
    const auto owner = ownership_.find(entity_id);
    if (owner == ownership_.end())
    {
        return result;
    }
    const auto* source_entry = source(owner->second.source_asset_id);
    if (source_entry == nullptr
        || (!owner->second.source_revision.empty()
            && owner->second.source_revision
                != dragonpixel::prefab::compute_revision(source_entry->document)))
    {
        return result;
    }
    result.push_back({
        QStringLiteral("Root source — %1").arg(QFileInfo{source_entry->path}.completeBaseName()),
        {},
    });
    bool safe = true;
    std::vector<uuid> dependency_stack;
    std::function<void(const document&, std::vector<uuid>, QString, std::size_t)> visit =
        [&](const document& current, std::vector<uuid> path, QString label, std::size_t depth) {
            if (!safe || depth > maximum_prefab_depth
                || std::find(dependency_stack.begin(), dependency_stack.end(), current.prefab_id)
                    != dependency_stack.end())
            {
                safe = false;
                return;
            }
            dependency_stack.push_back(current.prefab_id);
            for (const auto& nested : current.prefab_instances)
            {
                auto nested_path = path;
                nested_path.push_back(nested.instance_id);
                const auto* dependency = source(nested.source_asset_id);
                if (dependency == nullptr)
                {
                    continue;
                }
                const auto nested_label = label + QStringLiteral(" / ")
                    + QFileInfo{dependency->path}.completeBaseName();
                result.push_back({nested_label, nested_path});
                visit(dependency->document, std::move(nested_path), nested_label, depth + 1);
            }
            dependency_stack.pop_back();
        };
    visit(source_entry->document, {}, result.front().label, 0);
    if (!safe)
    {
        result.clear();
    }
    return result;
}

PrefabOperationResult PrefabService::apply(
    dragonpixel::scene::scene& current_scene,
    const uuid& selected_entity_id,
    std::span<const uuid> nesting_path,
    PrefabApplyFault injected_fault)
{
    PrefabOperationResult result;
    const auto index = instance_index_for_entity(selected_entity_id);
    if (!index || *index >= current_scene.prefab_instances().size())
    {
        result.message = QStringLiteral("Select a linked prefab GameObject before applying overrides.");
        return result;
    }
    auto instance = parse_instance(current_scene.prefab_instances()[*index], &result.diagnostics);
    if (!instance)
    {
        result.message = QStringLiteral("Selected prefab instance record is incompatible.");
        return result;
    }
    const auto* outer_source = source(instance->source_asset_id);
    if (outer_source == nullptr)
    {
        result.message = QStringLiteral("Apply is disabled while the prefab source is missing.");
        return result;
    }
    if (!source_revision_is_current(*instance, outer_source->document))
    {
        result.message = QStringLiteral(
            "Apply is disabled because the prefab source revision changed; use Repair/Rebase first.");
        result.diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.SOURCE_REVISION_MISMATCH: Apply cannot overwrite a source that changed outside this instance."));
        return result;
    }
    const SourceEntry* target = outer_source;
    for (const auto& nesting_id : nesting_path)
    {
        const auto nested = std::find_if(target->document.prefab_instances.begin(),
            target->document.prefab_instances.end(), [&](const auto& candidate) {
                return candidate.instance_id == nesting_id;
            });
        if (nested == target->document.prefab_instances.end())
        {
            result.message = QStringLiteral("The selected apply level no longer exists in the prefab source.");
            return result;
        }
        target = source(nested->source_asset_id);
        if (target == nullptr)
        {
            result.message = QStringLiteral("The selected nested prefab source is unavailable.");
            return result;
        }
    }

    std::unordered_map<std::string, std::string> inverse_entity_ids;
    for (const auto& mapping : instance->entity_mappings)
    {
        if (mapping.nested_path.size() == nesting_path.size()
            && std::equal(mapping.nested_path.begin(), mapping.nested_path.end(), nesting_path.begin()))
        {
            inverse_entity_ids.insert_or_assign(
                mapping.instance_entity_id.to_string(),
                mapping.source_entity_id.to_string());
        }
    }
    std::unordered_set<std::string> target_source_ids;
    for (const auto& item : target->document.entities)
    {
        target_source_ids.insert(item.id.to_string());
    }
    const auto validate_entity_reference = [&](const json& value, bool strict, const QString& context) {
        if (value.is_null() || (value.is_string() && value.get<std::string>().empty()))
        {
            return true;
        }
        if (!value.is_string())
        {
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.APPLY_INVALID_ENTITY_REFERENCE: %1 must be a UUID string or null.").arg(context));
            return false;
        }
        const auto text = value.get<std::string>();
        const auto referenced = uuid::parse(text);
        if (!referenced)
        {
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.APPLY_INVALID_ENTITY_REFERENCE: %1 was not a UUID.").arg(context));
            return false;
        }
        if (inverse_entity_ids.contains(text) || target_source_ids.contains(text))
        {
            return true;
        }
        if (strict || current_scene.find_entity(*referenced) != nullptr)
        {
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.APPLY_EXTERNAL_ENTITY_REFERENCE: %1 points outside the explicitly selected reusable prefab level.")
                    .arg(context));
            return false;
        }
        return true;
    };
    const auto validate_component_properties = [&](const json& component, const QString& context) {
        if (!component.is_object())
        {
            return false;
        }
        const auto type_id = component.value("typeId", std::string{});
        const auto properties = component.value("properties", json::object());
        const auto* descriptor = metadata_ == nullptr ? nullptr : metadata_->find(type_id);
        if (descriptor != nullptr)
        {
            for (const auto& property : descriptor->properties)
            {
                if (property.type == dragonpixel::metadata::value_type::entity_reference
                    && properties.contains(property.property_id)
                    && !validate_entity_reference(
                        properties.at(property.property_id),
                        true,
                        context + QStringLiteral(" / ") + QString::fromStdString(property.property_id)))
                {
                    return false;
                }
            }
            return true;
        }
        bool valid = true;
        std::function<void(const json&)> scan_unknown = [&](const json& value) {
            if (!valid)
            {
                return;
            }
            if (value.is_string())
            {
                const auto referenced = uuid::parse(value.get<std::string>());
                if (referenced && current_scene.find_entity(*referenced) != nullptr)
                {
                    valid = validate_entity_reference(value, true, context + QStringLiteral(" / opaque property"));
                }
            }
            else if (value.is_array() || value.is_object())
            {
                for (const auto& child : value)
                {
                    scan_unknown(child);
                }
            }
        };
        scan_unknown(properties);
        return valid;
    };
    for (const auto& operation : instance->overrides)
    {
        const auto target_value = operation.value("target", json::object());
        if (!same_path(nesting_path, target_value.value("nestedPath", json::array())))
        {
            continue;
        }
        const auto op = operation.value("op", std::string{});
        if (op == "add-entity")
        {
            result.message = QStringLiteral(
                "Apply of instance-added entities requires an explicit durable source-ID allocation; this build retains that override instead of risking instance UUID leakage.");
            result.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.APPLY_ENTITY_ALLOCATION_REQUIRED: Repair/Rebase or Unpack before applying an instance-added GameObject."));
            return result;
        }
        if (op == "reparent-entity"
            && !validate_entity_reference(operation.value("value", json(nullptr)), true,
                QStringLiteral("Reparent target")))
        {
            result.message = QStringLiteral("Apply rejected an external reparent target.");
            return result;
        }
        if (op == "add-component"
            && !validate_component_properties(operation.value("component", json::object()),
                QStringLiteral("Added component")))
        {
            result.message = QStringLiteral("Apply rejected an external component entity reference.");
            return result;
        }
        if (op == "set-property")
        {
            const auto component_type = operation.value("componentTypeId", std::string{});
            const auto property_id = operation.value("propertyId", std::string{});
            const auto* descriptor = metadata_ == nullptr ? nullptr : metadata_->find(component_type);
            const dragonpixel::metadata::property_descriptor* property = nullptr;
            if (descriptor != nullptr)
            {
                const auto found_property = std::find_if(
                    descriptor->properties.begin(), descriptor->properties.end(), [&](const auto& item) {
                    return item.property_id == property_id;
                });
                if (found_property != descriptor->properties.end())
                {
                    property = &*found_property;
                }
            }
            const auto known_entity_reference = descriptor != nullptr
                && property != nullptr
                && property->type == dragonpixel::metadata::value_type::entity_reference;
            if (known_entity_reference
                && !validate_entity_reference(operation.value("value", json(nullptr)), true,
                    QString::fromStdString(property_id)))
            {
                result.message = QStringLiteral("Apply rejected an external component entity reference.");
                return result;
            }
        }
    }

    auto applied = dragonpixel::prefab::apply_to_source(*instance, target->document, nesting_path);
    for (const auto& item : applied.diagnostics)
    {
        result.diagnostics.push_back(diagnostic_text(item));
    }
    if (!applied.source || has_errors(applied.diagnostics))
    {
        result.message = QStringLiteral("Prefab source rejected the selected override level.");
        return result;
    }

    const std::unordered_set<uuid, dragonpixel::core::uuid_hash> reusable_ids = [&] {
        std::unordered_set<uuid, dragonpixel::core::uuid_hash> ids;
        for (const auto& item : applied.source->entities)
        {
            ids.insert(item.id);
        }
        return ids;
    }();
    for (const auto& item : applied.source->entities)
    {
        if (item.parent_id && !reusable_ids.contains(*item.parent_id))
        {
            result.message = QStringLiteral(
                "Apply rejected a reusable prefab entity parent that points to scene-local state.");
            return result;
        }
    }

    const auto target_path = target->path;
    const auto backup_path = apply_backup_path(target_path);
    const auto marker_path = apply_recovery_marker_path(target_path);
    if (!path_within(project_root_, target_path)
        || !path_within(project_root_, backup_path)
        || !path_within(project_root_, marker_path))
    {
        result.message = QStringLiteral("Prefab Apply recovery paths must remain below the project root.");
        result.diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.APPLY_OUTSIDE_PROJECT: Source or recovery evidence resolved outside the project root."));
        return result;
    }
    const auto artifact_exists = [](const QString& path) {
        const QFileInfo info{path};
        return info.exists() || info.isSymLink();
    };
    if (artifact_exists(backup_path) || artifact_exists(marker_path))
    {
        result.message = QStringLiteral(
            "Prefab Apply found pre-existing recovery evidence and refused to overwrite it.");
        result.diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.APPLY_RECOVERY_ARTIFACT_CONFLICT: Resolve or preserve the existing .dpebak/.dpeapply evidence before retrying Apply."));
        return result;
    }

    QFile old_file{target_path};
    if (!old_file.open(QIODevice::ReadOnly))
    {
        result.message = QStringLiteral("Could not read the current prefab source before atomic Apply.");
        return result;
    }
    const auto old_bytes = old_file.readAll();
    old_file.close();
    QString error;
    const auto new_bytes = QByteArray::fromStdString(dragonpixel::prefab::write_json(*applied.source));
    if (injected_fault == PrefabApplyFault::backup_write)
    {
        error = QStringLiteral("Injected failure while writing the prefab recovery copy.");
    }
    if (!error.isEmpty() || !write_atomic(backup_path, old_bytes, error))
    {
        result.message = QStringLiteral("Atomic prefab Apply failed before source mutation: %1").arg(error);
        result.diagnostics.push_back(QStringLiteral("DPE.PREFAB.APPLY_BACKUP_WRITE_FAILED: %1").arg(error));
        return result;
    }
    if (injected_fault == PrefabApplyFault::recovery_marker_write)
    {
        error = QStringLiteral("Injected failure while writing the prefab Apply recovery marker.");
    }
    if (!error.isEmpty()
        || !write_atomic(marker_path, apply_recovery_marker(old_bytes, new_bytes), error))
    {
        remove_recovery_artifact(
            backup_path, result.diagnostics, QStringLiteral("DPE.PREFAB.RECOVERY_CLEANUP_FAILED"));
        result.message = QStringLiteral("Atomic prefab Apply failed before source mutation: %1").arg(error);
        result.diagnostics.push_back(QStringLiteral("DPE.PREFAB.APPLY_MARKER_WRITE_FAILED: %1").arg(error));
        return result;
    }
    if (injected_fault == PrefabApplyFault::source_write)
    {
        error = QStringLiteral("Injected failure while committing the prefab source.");
    }
    if (!error.isEmpty() || !write_atomic(target_path, new_bytes, error))
    {
        remove_recovery_artifact(
            marker_path, result.diagnostics, QStringLiteral("DPE.PREFAB.RECOVERY_CLEANUP_FAILED"));
        remove_recovery_artifact(
            backup_path, result.diagnostics, QStringLiteral("DPE.PREFAB.RECOVERY_CLEANUP_FAILED"));
        result.message = QStringLiteral("Atomic prefab Apply failed while committing the source: %1").arg(error);
        result.diagnostics.push_back(QStringLiteral("DPE.PREFAB.APPLY_SOURCE_WRITE_FAILED: %1").arg(error));
        return result;
    }

    const auto target_id = applied.source->prefab_id;
    const auto old_document = sources_.at(target_id).document;
    sources_.at(target_id).document = *applied.source;
    PrefabOperationResult replaced;
    if (injected_fault == PrefabApplyFault::scene_transaction
        || injected_fault == PrefabApplyFault::scene_transaction_and_rollback_write)
    {
        replaced.message = QStringLiteral("Injected prefab scene transaction failure.");
        replaced.diagnostics.push_back(QStringLiteral(
            "DPE.PREFAB.APPLY_SCENE_TRANSACTION_FAILED: Injected failure before scene mutation."));
    }
    else
    {
        replaced = replace_instance(
            current_scene,
            *index,
            std::move(applied.remaining_instance),
            QStringLiteral("Applied overrides to explicit prefab level %1")
                .arg(QFileInfo{target_path}.completeBaseName()));
    }
    if (!replaced.succeeded)
    {
        QString restore_error;
        const auto restored = injected_fault != PrefabApplyFault::scene_transaction_and_rollback_write
            && write_atomic(target_path, old_bytes, restore_error);
        if (restored)
        {
            sources_.at(target_id).document = old_document;
            remove_recovery_artifact(
                marker_path, replaced.diagnostics, QStringLiteral("DPE.PREFAB.RECOVERY_CLEANUP_FAILED"));
            remove_recovery_artifact(
                backup_path, replaced.diagnostics, QStringLiteral("DPE.PREFAB.RECOVERY_CLEANUP_FAILED"));
            replaced.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.APPLY_ROLLBACK: Scene transaction failed; the exact source before-image was restored."));
        }
        else
        {
            if (restore_error.isEmpty())
            {
                restore_error = QStringLiteral("Injected failure while restoring the prefab source before-image.");
            }
            replaced.diagnostics.push_back(QStringLiteral(
                "DPE.PREFAB.APPLY_ROLLBACK_FAILED: Scene transaction failed and immediate source restore failed (%1); the recovery marker and exact backup were retained for startup recovery.")
                                                   .arg(restore_error));
        }
        return replaced;
    }
    remove_recovery_artifact(
        marker_path, replaced.diagnostics, QStringLiteral("DPE.PREFAB.RECOVERY_CLEANUP_FAILED"));
    remove_recovery_artifact(
        backup_path, replaced.diagnostics, QStringLiteral("DPE.PREFAB.RECOVERY_CLEANUP_FAILED"));
    source_journal_.push_back({
        current_scene.history_position(), target_path, old_bytes, new_bytes, true});
    return replaced;
}

PrefabOperationResult PrefabService::revert_selected(
    dragonpixel::scene::scene& current_scene,
    const uuid& selected_entity_id)
{
    PrefabOperationResult result;
    const auto owner = ownership_.find(selected_entity_id);
    if (owner == ownership_.end() || owner->second.instance_index >= current_scene.prefab_instances().size())
    {
        result.message = QStringLiteral("Select a linked prefab GameObject before reverting overrides.");
        return result;
    }
    auto instance = parse_instance(
        current_scene.prefab_instances()[owner->second.instance_index], &result.diagnostics);
    const auto* current_source = instance ? source(instance->source_asset_id) : nullptr;
    if (!instance || current_source == nullptr
        || !source_revision_is_current(*instance, current_source->document))
    {
        result.message = QStringLiteral(
            "Revert is disabled while the prefab source is missing, incompatible, or at a different revision.");
        return result;
    }
    std::vector<std::size_t> indexes;
    for (std::size_t index = 0; index < instance->overrides.size(); ++index)
    {
        const auto& target = instance->overrides[index].value("target", json::object());
        if (target.value("sourceEntityId", std::string{}) == owner->second.source_entity_id.to_string()
            && same_path(owner->second.nested_path, target.value("nestedPath", json::array())))
        {
            indexes.push_back(index);
        }
    }
    if (indexes.empty())
    {
        result.message = QStringLiteral("The selected linked GameObject has no overrides to revert.");
        return result;
    }
    dragonpixel::prefab::revert_selected(*instance, indexes);
    return replace_instance(
        current_scene,
        owner->second.instance_index,
        std::move(*instance),
        QStringLiteral("Reverted selected prefab GameObject overrides"));
}

PrefabOperationResult PrefabService::revert_all(
    dragonpixel::scene::scene& current_scene,
    const uuid& selected_entity_id)
{
    PrefabOperationResult result;
    const auto index = instance_index_for_entity(selected_entity_id);
    if (!index || *index >= current_scene.prefab_instances().size())
    {
        result.message = QStringLiteral("Select a linked prefab GameObject before reverting overrides.");
        return result;
    }
    auto instance = parse_instance(current_scene.prefab_instances()[*index], &result.diagnostics);
    const auto* current_source = instance ? source(instance->source_asset_id) : nullptr;
    if (!instance || current_source == nullptr
        || !source_revision_is_current(*instance, current_source->document))
    {
        result.message = QStringLiteral(
            "Revert All is disabled while the prefab source is missing, incompatible, or at a different revision.");
        return result;
    }
    if (instance->overrides.empty())
    {
        result.message = QStringLiteral("The selected linked prefab has no overrides to revert.");
        return result;
    }
    dragonpixel::prefab::revert_all(*instance);
    return replace_instance(
        current_scene,
        *index,
        std::move(*instance),
        QStringLiteral("Reverted all prefab instance overrides"));
}

PrefabOperationResult PrefabService::repair_rebase(
    dragonpixel::scene::scene& current_scene,
    const uuid& selected_entity_id)
{
    PrefabOperationResult result;
    const auto index = instance_index_for_entity(selected_entity_id);
    if (!index || *index >= current_scene.prefab_instances().size())
    {
        result.message = QStringLiteral("Select a linked prefab GameObject before repairing mappings.");
        return result;
    }
    auto current = parse_instance(current_scene.prefab_instances()[*index], &result.diagnostics);
    if (!current)
    {
        result.message = QStringLiteral("Selected prefab instance record is incompatible.");
        return result;
    }
    const auto* current_source = source(current->source_asset_id);
    if (current_source == nullptr)
    {
        result.message = QStringLiteral(
            "Repair/Rebase requires the source; fallback data remains available for Unpack Completely.");
        return result;
    }

    auto allocated = *current;
    if (!allocate_mappings(current_source->document, {}, allocated, result.diagnostics))
    {
        result.message = QStringLiteral(
            "Repair/Rebase rejected an unsafe prefab dependency graph before scene mutation.");
        return result;
    }
    std::vector<dragonpixel::prefab::rebase_allocation> allocations;
    for (const auto& mapping : allocated.entity_mappings)
    {
        const auto exists = std::any_of(current->entity_mappings.begin(), current->entity_mappings.end(),
            [&](const auto& candidate) {
                return candidate.nested_path == mapping.nested_path
                    && candidate.source_entity_id == mapping.source_entity_id;
            });
        if (!exists)
        {
            allocations.push_back({mapping.nested_path, mapping.source_entity_id, mapping.instance_entity_id});
        }
    }
    auto rebased = dragonpixel::prefab::rebase(*current, current_source->document, allocations);
    for (const auto& item : rebased.diagnostics)
    {
        result.diagnostics.push_back(diagnostic_text(item));
    }
    const auto root_mapping = std::find_if(rebased.instance.entity_mappings.begin(),
        rebased.instance.entity_mappings.end(), [&](const auto& mapping) {
            return mapping.nested_path.empty()
                && mapping.source_entity_id == current_source->document.root_entity_id;
        });
    if (root_mapping == rebased.instance.entity_mappings.end())
    {
        result.message = QStringLiteral("Rebase could not establish a stable root mapping.");
        return result;
    }
    rebased.instance.root_entity_id = root_mapping->instance_entity_id;
    return replace_instance(
        current_scene,
        *index,
        std::move(rebased.instance),
        QStringLiteral("Repaired and rebased prefab mappings"));
}

PrefabOperationResult PrefabService::unpack(
    dragonpixel::scene::scene& current_scene,
    const uuid& selected_entity_id,
    bool completely)
{
    PrefabOperationResult result;
    const auto index = instance_index_for_entity(selected_entity_id);
    if (!index || *index >= current_scene.prefab_instances().size())
    {
        result.message = QStringLiteral("Select a linked prefab GameObject before unpacking.");
        return result;
    }
    auto outer = parse_instance(current_scene.prefab_instances()[*index], &result.diagnostics);
    if (!outer)
    {
        result.message = QStringLiteral("Selected prefab instance record is incompatible.");
        return result;
    }
    const auto root_id = outer->root_entity_id;
    auto instances = current_scene.prefab_instances();
    instances.erase(instances.begin() + static_cast<json::difference_type>(*index));

    if (!completely)
    {
        const auto* outer_source = source(outer->source_asset_id);
        if (outer_source == nullptr || !source_revision_is_current(*outer, outer_source->document))
        {
            result.message = QStringLiteral(
                "Unpack at one level requires the matching source revision. Unpack Completely remains available from fallback data.");
            return result;
        }
        for (const auto& nested : outer_source->document.prefab_instances)
        {
            instance_record promoted;
            promoted.instance_id = uuid::random_v4();
            promoted.source_asset_id = nested.source_asset_id;
            promoted.source_revision = nested.source_revision;
            if (nested.placement_parent_id)
            {
                const auto parent_mapping = std::find_if(outer->entity_mappings.begin(),
                    outer->entity_mappings.end(), [&](const auto& mapping) {
                        return mapping.nested_path.empty()
                            && mapping.source_entity_id == *nested.placement_parent_id;
                    });
                if (parent_mapping != outer->entity_mappings.end())
                {
                    promoted.placement_parent_id = parent_mapping->instance_entity_id;
                }
            }
            if (!promoted.placement_parent_id)
            {
                promoted.placement_parent_id = outer->placement_parent_id;
            }
            for (const auto& mapping : outer->entity_mappings)
            {
                if (!mapping.nested_path.empty() && mapping.nested_path.front() == nested.instance_id)
                {
                    auto path = mapping.nested_path;
                    path.erase(path.begin());
                    promoted.entity_mappings.push_back({
                        std::move(path), mapping.source_entity_id, mapping.instance_entity_id});
                }
            }
            const auto* nested_source = source(nested.source_asset_id);
            if (nested_source != nullptr)
            {
                promoted.source_revision = nested_source->document.revision;
                const auto root_mapping = std::find_if(promoted.entity_mappings.begin(),
                    promoted.entity_mappings.end(), [&](const auto& mapping) {
                        return mapping.nested_path.empty()
                            && mapping.source_entity_id == nested_source->document.root_entity_id;
                    });
                if (root_mapping != promoted.entity_mappings.end())
                {
                    promoted.root_entity_id = root_mapping->instance_entity_id;
                }
            }
            if (promoted.root_entity_id.is_nil())
            {
                result.diagnostics.push_back(QStringLiteral(
                    "DPE.PREFAB.UNPACK_NESTED_ROOT: A direct nested link lacked a stable root mapping and was unpacked completely."));
                continue;
            }
            promoted.overrides = nested.overrides;
            for (const auto& operation : outer->overrides)
            {
                const auto target = operation.value("target", json::object());
                const auto path = target.value("nestedPath", json::array());
                if (!path.empty() && path.front().is_string()
                    && path.front().get<std::string>() == nested.instance_id.to_string())
                {
                    auto promoted_operation = operation;
                    auto promoted_path = promoted_operation["target"]["nestedPath"];
                    promoted_path.erase(promoted_path.begin());
                    promoted_operation["target"]["nestedPath"] = std::move(promoted_path);
                    promoted.overrides.push_back(std::move(promoted_operation));
                }
            }
            std::unordered_set<uuid, dragonpixel::core::uuid_hash> promoted_ids;
            for (const auto& mapping : promoted.entity_mappings)
            {
                promoted_ids.insert(mapping.instance_entity_id);
            }
            for (const auto& candidate : current_scene.entities())
            {
                if (promoted_ids.contains(candidate.id))
                {
                    promoted.fallback_entities.push_back(candidate);
                }
            }
            instances.push_back(instance_json(promoted));
        }
    }

    const auto previous_history_position = current_scene.history_position();
    const auto applied = current_scene.apply(
        command{dragonpixel::scene::set_prefab_instances_command{std::move(instances)}},
        completely ? "Unpack linked prefab completely" : "Unpack linked prefab one level");
    if (!applied.succeeded)
    {
        result.message = QStringLiteral("Scene rejected the prefab Unpack transaction.");
        return result;
    }
    truncate_source_journal(previous_history_position);
    synchronize(current_scene);
    result.succeeded = true;
    result.selection = {root_id};
    result.message = completely
        ? QStringLiteral("Unpacked prefab completely; fallback entities are now locally owned.")
        : QStringLiteral("Unpacked one level; direct nested prefab links remain linked.");
    return result;
}
