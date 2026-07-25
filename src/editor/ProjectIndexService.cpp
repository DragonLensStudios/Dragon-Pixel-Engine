#include "ProjectIndexService.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <utility>

namespace
{
constexpr auto project_format = "dpe.project";
constexpr auto scene_format = "dpe.scene";
constexpr auto prefab_format = "dpe.prefab";
constexpr auto asset_format = "dpe.asset";

struct ContainedPath final
{
    bool valid{};
    QString absolute_path;
};

QString normalized_absolute_path(const QString& path)
{
    return QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
}

QString path_key(const QString& path)
{
    auto result = QDir::fromNativeSeparators(normalized_absolute_path(path));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool relative_path_is_contained(const QString& base, const QString& path)
{
    const auto relative = QDir{base}.relativeFilePath(path);
    return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !relative.startsWith(QStringLiteral("..\\"));
}

bool canonical_ancestor_is_contained(const QString& base, const QString& path)
{
    const auto canonical_base = QFileInfo{base}.canonicalFilePath();
    if (canonical_base.isEmpty())
    {
        return false;
    }

    QFileInfo probe{path};
    while (!probe.exists())
    {
        const auto parent = QFileInfo{probe.absolutePath()};
        if (parent.absoluteFilePath() == probe.absoluteFilePath())
        {
            return false;
        }
        probe = parent;
    }

    const auto canonical_probe = probe.canonicalFilePath();
    return !canonical_probe.isEmpty()
        && relative_path_is_contained(canonical_base, canonical_probe);
}

ContainedPath resolve_contained_path(
    const QString& project_root,
    const QString& declared_path,
    const QString& relative_base = {})
{
    if (declared_path.trimmed().isEmpty() || QDir::isAbsolutePath(declared_path))
    {
        return {};
    }

    const auto base = relative_base.isEmpty() ? project_root : relative_base;
    const auto absolute = QDir::cleanPath(QDir{base}.absoluteFilePath(declared_path));
    if (!relative_path_is_contained(project_root, absolute)
        || !canonical_ancestor_is_contained(project_root, absolute))
    {
        return {};
    }
    return {true, absolute};
}

bool is_uuid(const QString& value)
{
    static const QRegularExpression expression{
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")};
    return expression.match(value).hasMatch();
}

QString normalized_uuid(const QString& value)
{
    return value.toLower();
}

void add_diagnostic(
    ProjectIndexBuildResult& result,
    ProjectIndexDiagnosticCode code,
    QString message,
    const QString& document_path = {},
    const QString& json_pointer = {},
    const QString& related_id = {},
    ProjectIndexDiagnosticSeverity severity = ProjectIndexDiagnosticSeverity::error)
{
    result.diagnostics.push_back(ProjectIndexDiagnostic{
        severity,
        code,
        std::move(message),
        document_path,
        json_pointer,
        related_id,
    });
}

std::optional<QJsonObject> read_json_object(
    const QString& path,
    ProjectIndexBuildResult& result,
    bool report_invalid)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
    {
        if (report_invalid)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unreadable_document,
                QStringLiteral("Cannot read '%1': %2").arg(path, file.errorString()),
                path);
        }
        return std::nullopt;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (report_invalid)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::invalid_json,
                QStringLiteral("'%1' is not a JSON object: %2")
                    .arg(path, error.errorString()),
                path);
        }
        return std::nullopt;
    }
    return document.object();
}

QString required_string(
    const QJsonObject& object,
    const QString& field,
    const QString& path,
    ProjectIndexBuildResult& result,
    bool uuid = false)
{
    const auto value = object.value(field);
    if (!value.isString() || value.toString().trimmed().isEmpty())
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::missing_field,
            QStringLiteral("Required string '%1' is missing or empty.").arg(field),
            path,
            QStringLiteral("/%1").arg(field));
        return {};
    }
    if (uuid && !is_uuid(value.toString()))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::invalid_identifier,
            QStringLiteral("'%1' is not a canonical UUID.").arg(field),
            path,
            QStringLiteral("/%1").arg(field),
            value.toString());
        return {};
    }
    return uuid ? normalized_uuid(value.toString()) : value.toString();
}

std::optional<int> required_version(
    const QJsonObject& object,
    const QString& path,
    ProjectIndexBuildResult& result)
{
    const auto value = object.value(QStringLiteral("formatVersion"));
    if (!value.isDouble() || value.toDouble() != static_cast<double>(value.toInt()))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::missing_field,
            QStringLiteral("Required integer 'formatVersion' is missing."),
            path,
            QStringLiteral("/formatVersion"));
        return std::nullopt;
    }
    return value.toInt();
}

QStringList string_array(
    const QJsonObject& object,
    const QString& field,
    const QString& path,
    ProjectIndexBuildResult& result,
    bool required,
    bool uuids)
{
    const auto value = object.value(field);
    if (!value.isArray())
    {
        if (required)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::missing_field,
                QStringLiteral("Required array '%1' is missing.").arg(field),
                path,
                QStringLiteral("/%1").arg(field));
        }
        return {};
    }

    QStringList values;
    QSet<QString> seen;
    const auto array = value.toArray();
    for (qsizetype index = 0; index < array.size(); ++index)
    {
        if (!array.at(index).isString() || array.at(index).toString().trimmed().isEmpty())
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::missing_field,
                QStringLiteral("'%1' contains a non-string or empty item.").arg(field),
                path,
                QStringLiteral("/%1/%2").arg(field).arg(index));
            continue;
        }
        auto item = array.at(index).toString();
        if (uuids && !is_uuid(item))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::invalid_identifier,
                QStringLiteral("'%1' contains a non-UUID item.").arg(field),
                path,
                QStringLiteral("/%1/%2").arg(field).arg(index),
                item);
            continue;
        }
        if (uuids)
        {
            item = normalized_uuid(item);
        }
        if (seen.contains(item))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::duplicate_identifier,
                QStringLiteral("'%1' contains duplicate value '%2'.").arg(field, item),
                path,
                QStringLiteral("/%1/%2").arg(field).arg(index),
                item);
            continue;
        }
        seen.insert(item);
        values.push_back(item);
    }
    return values;
}

bool is_sorted(const QStringList& values)
{
    return std::is_sorted(values.cbegin(), values.cend());
}

QString logical_path(const QString& root, const QString& path)
{
    return QDir::fromNativeSeparators(QDir{root}.relativeFilePath(path));
}

std::optional<ProjectIndexEntryKind> recognized_kind(const QString& format)
{
    if (format == QString::fromLatin1(scene_format))
    {
        return ProjectIndexEntryKind::scene;
    }
    if (format == QString::fromLatin1(prefab_format))
    {
        return ProjectIndexEntryKind::prefab;
    }
    if (format == QString::fromLatin1(asset_format))
    {
        return ProjectIndexEntryKind::asset;
    }
    return std::nullopt;
}

std::optional<ProjectIndexEntryKind> expected_kind_for_path(const QString& path)
{
    const auto suffix = QFileInfo{path}.suffix().toLower();
    if (suffix == QStringLiteral("dpescene"))
    {
        return ProjectIndexEntryKind::scene;
    }
    if (suffix == QStringLiteral("dpeprefab"))
    {
        return ProjectIndexEntryKind::prefab;
    }
    if (suffix == QStringLiteral("dpeasset"))
    {
        return ProjectIndexEntryKind::asset;
    }
    return std::nullopt;
}

QString kind_name(ProjectIndexEntryKind kind)
{
    switch (kind)
    {
    case ProjectIndexEntryKind::scene:
        return QStringLiteral("dpe.scene");
    case ProjectIndexEntryKind::prefab:
        return QStringLiteral("dpe.prefab");
    case ProjectIndexEntryKind::asset:
        return QStringLiteral("dpe.asset");
    }
    return QStringLiteral("unknown");
}

void append_instance_dependencies(
    const QJsonObject& document,
    const QString& path,
    ProjectIndexBuildResult& result,
    QStringList& dependencies)
{
    const auto instances_value = document.value(QStringLiteral("prefabInstances"));
    if (instances_value.isUndefined())
    {
        return;
    }
    if (!instances_value.isArray())
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::missing_field,
            QStringLiteral("'prefabInstances' must be an array."),
            path,
            QStringLiteral("/prefabInstances"));
        return;
    }

    const auto instances = instances_value.toArray();
    for (qsizetype index = 0; index < instances.size(); ++index)
    {
        if (!instances.at(index).isObject())
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::missing_field,
                QStringLiteral("Prefab instance must be an object."),
                path,
                QStringLiteral("/prefabInstances/%1").arg(index));
            continue;
        }
        const auto source = instances.at(index).toObject().value(QStringLiteral("sourceAssetId"));
        if (!source.isString() || !is_uuid(source.toString()))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::invalid_identifier,
                QStringLiteral("Prefab instance sourceAssetId must be a UUID."),
                path,
                QStringLiteral("/prefabInstances/%1/sourceAssetId").arg(index),
                source.toString());
            continue;
        }
        dependencies.push_back(normalized_uuid(source.toString()));
    }
    dependencies.removeDuplicates();
    dependencies.sort();
}

void validate_asset_source(
    ProjectIndexEntry& entry,
    const QString& project_root,
    ProjectIndexBuildResult& result)
{
    if (entry.source.isEmpty())
    {
        return;
    }

    const QUrl source_url{entry.source};
    if (source_url.isValid() && !source_url.scheme().isEmpty())
    {
        const auto scheme = source_url.scheme().toLower();
        if (scheme == QStringLiteral("builtin") || scheme == QStringLiteral("generated")
            || scheme == QStringLiteral("package"))
        {
            return;
        }
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsupported_asset_source,
            QStringLiteral("Asset source scheme '%1' is not supported by the project index.").arg(scheme),
            entry.absolute_path,
            QStringLiteral("/source"));
        entry.structurally_valid = false;
        return;
    }

    const auto resolved = resolve_contained_path(
        project_root,
        entry.source,
        QFileInfo{entry.absolute_path}.absolutePath());
    if (!resolved.valid)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Asset source escapes the project root: '%1'.").arg(entry.source),
            entry.absolute_path,
            QStringLiteral("/source"));
        entry.structurally_valid = false;
        return;
    }
    entry.resolved_source_path = resolved.absolute_path;
    if (!QFileInfo{resolved.absolute_path}.isFile())
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::missing_asset_source,
            QStringLiteral("Asset source does not exist: '%1'.").arg(entry.source),
            entry.absolute_path,
            QStringLiteral("/source"));
        entry.structurally_valid = false;
    }
}

std::optional<ProjectIndexEntry> parse_index_entry(
    const QString& path,
    const QString& project_root,
    ProjectIndexBuildResult& result,
    std::optional<ProjectIndexEntryKind> expected_kind)
{
    const auto object = read_json_object(path, result, expected_kind.has_value());
    if (!object)
    {
        return std::nullopt;
    }

    const auto format_value = object->value(QStringLiteral("format"));
    if (!format_value.isString())
    {
        if (expected_kind)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unexpected_document_format,
                QStringLiteral("Expected %1 document.").arg(kind_name(*expected_kind)),
                path,
                QStringLiteral("/format"));
        }
        return std::nullopt;
    }
    const auto kind = recognized_kind(format_value.toString());
    if (!kind)
    {
        if (expected_kind)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unexpected_document_format,
                QStringLiteral("Expected %1 but found '%2'.")
                    .arg(kind_name(*expected_kind), format_value.toString()),
                path,
                QStringLiteral("/format"));
        }
        return std::nullopt;
    }
    if (expected_kind && *expected_kind != *kind)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unexpected_document_format,
            QStringLiteral("File extension expects %1 but document contains %2.")
                .arg(kind_name(*expected_kind), kind_name(*kind)),
            path,
            QStringLiteral("/format"));
    }

    ProjectIndexEntry entry;
    entry.kind = *kind;
    entry.absolute_path = normalized_absolute_path(path);
    entry.logical_path = logical_path(project_root, entry.absolute_path);
    entry.document = *object;

    const auto version = required_version(*object, path, result);
    if (!version)
    {
        entry.structurally_valid = false;
        return entry;
    }
    entry.format_version = *version;
    const auto engine_version = required_string(
        *object, QStringLiteral("engineVersion"), path, result);
    if (engine_version.isEmpty())
    {
        entry.structurally_valid = false;
    }

    QString id_field;
    QString expected_schema;
    bool supported_version = true;
    switch (*kind)
    {
    case ProjectIndexEntryKind::scene:
        id_field = QStringLiteral("sceneId");
        expected_schema = QStringLiteral("https://dragonpixel.dev/schemas/v%1/scene.schema.json")
                              .arg(*version);
        if (*version < 1 || *version > 3)
        {
            supported_version = false;
            entry.structurally_valid = false;
        }
        entry.display_name = required_string(
            *object, QStringLiteral("name"), path, result);
        if (entry.display_name.isEmpty()
            || !object->value(QStringLiteral("entities")).isArray())
        {
            if (!object->value(QStringLiteral("entities")).isArray())
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_field,
                    QStringLiteral("Scene requires array 'entities'."),
                    path,
                    QStringLiteral("/entities"));
            }
            entry.structurally_valid = false;
        }
        append_instance_dependencies(*object, path, result, entry.dependencies);
        break;
    case ProjectIndexEntryKind::prefab:
        id_field = QStringLiteral("prefabId");
        expected_schema = QStringLiteral("https://dragonpixel.dev/schemas/v1/prefab.schema.json");
        if (*version != 1)
        {
            supported_version = false;
            entry.structurally_valid = false;
        }
        if (required_string(*object, QStringLiteral("revision"), path, result).isEmpty()
            || required_string(
                   *object, QStringLiteral("rootEntityId"), path, result, true).isEmpty()
            || !object->value(QStringLiteral("entities")).isArray())
        {
            if (!object->value(QStringLiteral("entities")).isArray())
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_field,
                    QStringLiteral("Prefab requires array 'entities'."),
                    path,
                    QStringLiteral("/entities"));
            }
            entry.structurally_valid = false;
        }
        entry.dependencies = string_array(
            *object, QStringLiteral("dependencies"), path, result, true, true);
        append_instance_dependencies(*object, path, result, entry.dependencies);
        break;
    case ProjectIndexEntryKind::asset:
        id_field = QStringLiteral("assetId");
        expected_schema = QStringLiteral("https://dragonpixel.dev/schemas/v%1/asset-metadata.schema.json")
                              .arg(*version);
        if (*version < 1 || *version > 2)
        {
            supported_version = false;
            entry.structurally_valid = false;
        }
        entry.asset_type = required_string(
            *object, QStringLiteral("assetType"), path, result);
        entry.source = required_string(
            *object, QStringLiteral("source"), path, result);
        if (entry.asset_type.isEmpty() || entry.source.isEmpty())
        {
            entry.structurally_valid = false;
        }
        entry.dependencies = string_array(
            *object,
            QStringLiteral("dependencies"),
            path,
            result,
            *version == 2,
            true);
        if (*version == 2 && !object->value(QStringLiteral("importSettings")).isObject())
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::missing_field,
                QStringLiteral("Version 2 asset requires object 'importSettings'."),
                path,
                QStringLiteral("/importSettings"));
            entry.structurally_valid = false;
        }
        validate_asset_source(entry, project_root, result);
        break;
    }

    if (object->value(QStringLiteral("$schema")).toString() != expected_schema)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::invalid_schema,
            QStringLiteral("Document schema must be '%1'.").arg(expected_schema),
            path,
            QStringLiteral("/$schema"));
        entry.structurally_valid = false;
    }

    entry.id = required_string(*object, id_field, path, result, true);
    if (entry.id.isEmpty())
    {
        entry.structurally_valid = false;
    }
    if (!supported_version)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsupported_document_version,
            QStringLiteral("Unsupported %1 version %2 document.")
                .arg(kind_name(*kind))
                .arg(*version),
            path,
            QStringLiteral("/formatVersion"),
            entry.id);
    }
    return entry;
}

void add_root(
    ProjectIndexCandidate& candidate,
    ProjectIndexBuildResult& result,
    ProjectIndexRootKind kind,
    const QString& declared,
    QSet<QString>& declared_roots,
    const QString& pointer)
{
    const auto key = QDir::fromNativeSeparators(QDir::cleanPath(declared));
#ifdef Q_OS_WIN
    const auto normalized_key = key.toCaseFolded();
#else
    const auto normalized_key = key;
#endif
    const auto comparison_key = QStringLiteral("%1:%2")
                                    .arg(kind == ProjectIndexRootKind::scenes
                                             ? QStringLiteral("scenes")
                                             : QStringLiteral("assets"),
                                         normalized_key);
    if (declared_roots.contains(comparison_key))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::duplicate_root,
            QStringLiteral("Project root is declared more than once: '%1'.").arg(declared),
            candidate.manifest_path,
            pointer);
        return;
    }
    declared_roots.insert(comparison_key);

    const auto resolved = resolve_contained_path(candidate.project_root, declared);
    if (!resolved.valid)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project root escapes the project directory: '%1'.").arg(declared),
            candidate.manifest_path,
            pointer);
        return;
    }
    const QFileInfo root_info{resolved.absolute_path};
    if (!root_info.exists() || !root_info.isDir())
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::missing_root,
            QStringLiteral("Project root does not exist or is not a directory: '%1'.").arg(declared),
            candidate.manifest_path,
            pointer);
        return;
    }
    candidate.roots.push_back(ProjectIndexRoot{kind, declared, resolved.absolute_path});
}

void build_id_index(ProjectIndexCandidate& candidate, ProjectIndexBuildResult& result)
{
    std::sort(candidate.entries.begin(), candidate.entries.end(), [](const auto& left, const auto& right) {
        return left.logical_path < right.logical_path;
    });
    for (qsizetype index = 0; index < candidate.entries.size(); ++index)
    {
        const auto& entry = candidate.entries.at(index);
        if (entry.id.isEmpty())
        {
            continue;
        }
        const auto existing = candidate.entry_indices_by_id.constFind(entry.id);
        if (existing != candidate.entry_indices_by_id.cend())
        {
            const auto& first = candidate.entries.at(existing.value());
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::duplicate_identifier,
                QStringLiteral("Identifier '%1' is used by both '%2' and '%3'.")
                    .arg(entry.id, first.logical_path, entry.logical_path),
                entry.absolute_path,
                {},
                entry.id);
            continue;
        }
        candidate.entry_indices_by_id.insert(entry.id, index);
    }
}

void validate_dependencies(ProjectIndexCandidate& candidate, ProjectIndexBuildResult& result)
{
    QHash<QString, QStringList> graph;
    QStringList identifiers = candidate.entry_indices_by_id.keys();
    identifiers.sort();
    for (const auto& id : identifiers)
    {
        const auto* entry = candidate.find_by_id(id);
        if (entry == nullptr)
        {
            continue;
        }
        QStringList known_dependencies;
        for (const auto& dependency : entry->dependencies)
        {
            if (!candidate.entry_indices_by_id.contains(dependency))
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_dependency,
                    QStringLiteral("'%1' depends on missing identifier '%2'.")
                        .arg(entry->logical_path, dependency),
                    entry->absolute_path,
                    QStringLiteral("/dependencies"),
                    dependency);
                continue;
            }
            known_dependencies.push_back(dependency);
        }
        known_dependencies.removeDuplicates();
        known_dependencies.sort();
        graph.insert(id, known_dependencies);
    }

    QHash<QString, int> discovery;
    QHash<QString, int> low_link;
    QList<QString> stack;
    QSet<QString> on_stack;
    int next_index = 0;
    std::function<void(const QString&)> visit;
    visit = [&](const QString& id) {
        discovery.insert(id, next_index);
        low_link.insert(id, next_index);
        ++next_index;
        stack.push_back(id);
        on_stack.insert(id);

        for (const auto& dependency : graph.value(id))
        {
            if (!discovery.contains(dependency))
            {
                visit(dependency);
                low_link[id] = std::min(low_link.value(id), low_link.value(dependency));
            }
            else if (on_stack.contains(dependency))
            {
                low_link[id] = std::min(low_link.value(id), discovery.value(dependency));
            }
        }

        if (low_link.value(id) != discovery.value(id))
        {
            return;
        }
        QStringList component;
        while (!stack.isEmpty())
        {
            const auto current = stack.takeLast();
            on_stack.remove(current);
            component.push_back(current);
            if (current == id)
            {
                break;
            }
        }
        component.sort();
        const auto direct_cycle = component.size() == 1
            && graph.value(component.constFirst()).contains(component.constFirst());
        if (component.size() > 1 || direct_cycle)
        {
            const auto* entry = candidate.find_by_id(component.constFirst());
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::dependency_cycle,
                direct_cycle
                    ? QStringLiteral("Direct dependency cycle: %1 -> %1.").arg(component.constFirst())
                    : QStringLiteral("Indirect dependency cycle contains: %1.")
                          .arg(component.join(QStringLiteral(", "))),
                entry == nullptr ? QString{} : entry->absolute_path,
                QStringLiteral("/dependencies"),
                component.join(QStringLiteral(",")));
        }
    };

    for (const auto& id : identifiers)
    {
        if (!discovery.contains(id))
        {
            visit(id);
        }
    }
}
}

QString project_index_diagnostic_code_name(ProjectIndexDiagnosticCode code)
{
    switch (code)
    {
    case ProjectIndexDiagnosticCode::manifest_not_found: return QStringLiteral("manifest-not-found");
    case ProjectIndexDiagnosticCode::unreadable_document: return QStringLiteral("unreadable-document");
    case ProjectIndexDiagnosticCode::invalid_json: return QStringLiteral("invalid-json");
    case ProjectIndexDiagnosticCode::invalid_project_format: return QStringLiteral("invalid-project-format");
    case ProjectIndexDiagnosticCode::unsupported_project_version: return QStringLiteral("unsupported-project-version");
    case ProjectIndexDiagnosticCode::invalid_schema: return QStringLiteral("invalid-schema");
    case ProjectIndexDiagnosticCode::missing_field: return QStringLiteral("missing-field");
    case ProjectIndexDiagnosticCode::invalid_identifier: return QStringLiteral("invalid-identifier");
    case ProjectIndexDiagnosticCode::unsafe_path: return QStringLiteral("unsafe-path");
    case ProjectIndexDiagnosticCode::missing_root: return QStringLiteral("missing-root");
    case ProjectIndexDiagnosticCode::duplicate_root: return QStringLiteral("duplicate-root");
    case ProjectIndexDiagnosticCode::unsorted_roots: return QStringLiteral("unsorted-roots");
    case ProjectIndexDiagnosticCode::unexpected_document_format: return QStringLiteral("unexpected-document-format");
    case ProjectIndexDiagnosticCode::unsupported_document_version: return QStringLiteral("unsupported-document-version");
    case ProjectIndexDiagnosticCode::duplicate_identifier: return QStringLiteral("duplicate-identifier");
    case ProjectIndexDiagnosticCode::missing_startup_scene: return QStringLiteral("missing-startup-scene");
    case ProjectIndexDiagnosticCode::startup_scene_not_indexed: return QStringLiteral("startup-scene-not-indexed");
    case ProjectIndexDiagnosticCode::missing_asset_source: return QStringLiteral("missing-asset-source");
    case ProjectIndexDiagnosticCode::unsupported_asset_source: return QStringLiteral("unsupported-asset-source");
    case ProjectIndexDiagnosticCode::missing_dependency: return QStringLiteral("missing-dependency");
    case ProjectIndexDiagnosticCode::dependency_cycle: return QStringLiteral("dependency-cycle");
    }
    return QStringLiteral("unknown");
}

const ProjectIndexEntry* ProjectIndexCandidate::find_by_id(const QString& id) const noexcept
{
    const auto iterator = entry_indices_by_id.constFind(id.toLower());
    return iterator == entry_indices_by_id.cend() ? nullptr : &entries.at(iterator.value());
}

const ProjectIndexEntry* ProjectIndexCandidate::find_by_path(const QString& absolute_path) const noexcept
{
    const auto key = path_key(absolute_path);
    const auto iterator = std::find_if(entries.cbegin(), entries.cend(), [&](const auto& entry) {
        return path_key(entry.absolute_path) == key;
    });
    return iterator == entries.cend() ? nullptr : &*iterator;
}

bool ProjectIndexBuildResult::has_errors() const noexcept
{
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [](const auto& diagnostic) {
        return diagnostic.severity == ProjectIndexDiagnosticSeverity::error;
    });
}

bool ProjectIndexBuildResult::succeeded() const noexcept
{
    return candidate.has_value() && !has_errors();
}

ProjectIndexBuildResult ProjectIndexService::build_candidate(const QString& manifest_path) const
{
    ProjectIndexBuildResult result;
    const auto absolute_manifest = normalized_absolute_path(manifest_path);
    const QFileInfo manifest_info{absolute_manifest};
    if (!manifest_info.exists() || !manifest_info.isFile())
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::manifest_not_found,
            QStringLiteral("Project manifest does not exist: '%1'.").arg(absolute_manifest),
            absolute_manifest);
        return result;
    }

    const auto manifest_object = read_json_object(absolute_manifest, result, true);
    if (!manifest_object)
    {
        return result;
    }

    ProjectIndexCandidate candidate;
    candidate.manifest_path = absolute_manifest;
    candidate.project_root = QFileInfo{manifest_info.absolutePath()}.canonicalFilePath();
    if (candidate.project_root.isEmpty())
    {
        candidate.project_root = normalized_absolute_path(manifest_info.absolutePath());
    }
    candidate.project_id = required_string(
        *manifest_object, QStringLiteral("projectId"), absolute_manifest, result, true);
    candidate.name = required_string(
        *manifest_object, QStringLiteral("name"), absolute_manifest, result);
    static_cast<void>(required_string(
        *manifest_object, QStringLiteral("engineVersion"), absolute_manifest, result));
    candidate.startup_scene = required_string(
        *manifest_object, QStringLiteral("startupScene"), absolute_manifest, result);

    if (manifest_object->value(QStringLiteral("format")).toString()
        != QString::fromLatin1(project_format))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::invalid_project_format,
            QStringLiteral("Manifest format must be 'dpe.project'."),
            absolute_manifest,
            QStringLiteral("/format"));
    }
    const auto version = required_version(*manifest_object, absolute_manifest, result);
    if (version)
    {
        candidate.format_version = *version;
        if (*version != 1 && *version != 2)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsupported_project_version,
                QStringLiteral("Project format version %1 is unsupported.").arg(*version),
                absolute_manifest,
                QStringLiteral("/formatVersion"));
        }
        const auto expected_schema = QStringLiteral("https://dragonpixel.dev/schemas/v%1/project.schema.json")
                                         .arg(*version);
        if (manifest_object->value(QStringLiteral("$schema")).toString() != expected_schema)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::invalid_schema,
                QStringLiteral("Project schema must be '%1'.").arg(expected_schema),
                absolute_manifest,
                QStringLiteral("/$schema"));
        }
    }

    QStringList scene_roots;
    QStringList asset_roots;
    if (version && *version == 1)
    {
        const auto scene_directory = QFileInfo{candidate.startup_scene}.path();
        scene_roots = {scene_directory.isEmpty() ? QStringLiteral(".") : scene_directory};
        asset_roots = string_array(
            *manifest_object,
            QStringLiteral("assetRoots"),
            absolute_manifest,
            result,
            false,
            false);
    }
    else if (version && *version == 2)
    {
        scene_roots = string_array(
            *manifest_object,
            QStringLiteral("sceneRoots"),
            absolute_manifest,
            result,
            true,
            false);
        asset_roots = string_array(
            *manifest_object,
            QStringLiteral("assetRoots"),
            absolute_manifest,
            result,
            true,
            false);
        if (!is_sorted(scene_roots))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsorted_roots,
                QStringLiteral("Version 2 sceneRoots must be sorted."),
                absolute_manifest,
                QStringLiteral("/sceneRoots"));
        }
        if (!is_sorted(asset_roots))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsorted_roots,
                QStringLiteral("Version 2 assetRoots must be sorted."),
                absolute_manifest,
                QStringLiteral("/assetRoots"));
        }
    }

    QSet<QString> declared_roots;
    for (qsizetype index = 0; index < scene_roots.size(); ++index)
    {
        add_root(
            candidate,
            result,
            ProjectIndexRootKind::scenes,
            scene_roots.at(index),
            declared_roots,
            QStringLiteral("/sceneRoots/%1").arg(index));
    }
    for (qsizetype index = 0; index < asset_roots.size(); ++index)
    {
        add_root(
            candidate,
            result,
            ProjectIndexRootKind::assets,
            asset_roots.at(index),
            declared_roots,
            QStringLiteral("/assetRoots/%1").arg(index));
    }

    if (!candidate.startup_scene.isEmpty())
    {
        const auto startup = resolve_contained_path(candidate.project_root, candidate.startup_scene);
        if (!startup.valid)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Startup scene escapes the project directory."),
                absolute_manifest,
                QStringLiteral("/startupScene"));
        }
        else
        {
            candidate.startup_scene_path = startup.absolute_path;
            if (!QFileInfo{startup.absolute_path}.isFile())
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_startup_scene,
                    QStringLiteral("Startup scene does not exist: '%1'.").arg(candidate.startup_scene),
                    absolute_manifest,
                    QStringLiteral("/startupScene"));
            }
        }
    }

    QSet<QString> indexed_paths;
    const auto inspect_path = [&](const QString& path, std::optional<ProjectIndexEntryKind> expected) {
        const auto absolute = normalized_absolute_path(path);
        if (!relative_path_is_contained(candidate.project_root, absolute)
            || !canonical_ancestor_is_contained(candidate.project_root, absolute))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Indexed file resolves outside the project root."),
                absolute);
            return;
        }
        const auto key = path_key(absolute);
        if (indexed_paths.contains(key))
        {
            return;
        }
        indexed_paths.insert(key);
        const auto entry = parse_index_entry(absolute, candidate.project_root, result, expected);
        if (entry)
        {
            candidate.entries.push_back(*entry);
        }
    };

    for (const auto& root : candidate.roots)
    {
        QDirIterator iterator{
            root.absolute_path,
            QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories};
        while (iterator.hasNext())
        {
            const auto path = iterator.next();
            const auto suffix = QFileInfo{path}.suffix().toLower();
            if (suffix == QStringLiteral("dpescene") || suffix == QStringLiteral("dpeprefab")
                || suffix == QStringLiteral("dpeasset"))
            {
                inspect_path(path, expected_kind_for_path(path));
            }
            else if (suffix == QStringLiteral("json"))
            {
                inspect_path(path, std::nullopt);
            }
        }
    }
    if (!candidate.startup_scene_path.isEmpty() && QFileInfo{candidate.startup_scene_path}.isFile())
    {
        inspect_path(candidate.startup_scene_path, ProjectIndexEntryKind::scene);
    }

    build_id_index(candidate, result);
    if (!candidate.startup_scene_path.isEmpty())
    {
        const auto* startup = candidate.find_by_path(candidate.startup_scene_path);
        if (startup == nullptr || startup->kind != ProjectIndexEntryKind::scene)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::startup_scene_not_indexed,
                QStringLiteral("Startup scene is not a valid indexed dpe.scene document."),
                candidate.startup_scene_path);
        }
    }
    validate_dependencies(candidate, result);
    result.candidate = std::move(candidate);
    return result;
}
