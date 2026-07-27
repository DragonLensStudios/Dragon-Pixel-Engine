#include "ProjectIndexService.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
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

#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif

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

enum class ExistingPathKind
{
    file,
    directory,
};

struct PathSnapshot final
{
    QString absolute_path;
    QString canonical_path;
    qint64 size{};
    qint64 last_modified_msecs{};
    ExistingPathKind kind{ExistingPathKind::file};
};

QString normalized_absolute_path(const QString& path)
{
    return QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
}

QString path_key(const QString& path)
{
    return QDir::fromNativeSeparators(normalized_absolute_path(path))
        .normalized(QString::NormalizationForm_C)
        .toCaseFolded();
}

QString relative_path_key(const QString& path)
{
    return path
        .normalized(QString::NormalizationForm_C)
        .toCaseFolded();
}

bool is_windows_reserved_component(const QString& component)
{
    const auto base = component.section(QLatin1Char('.'), 0, 0).toCaseFolded();
    if (base == QStringLiteral("con") || base == QStringLiteral("prn")
        || base == QStringLiteral("aux") || base == QStringLiteral("nul"))
    {
        return true;
    }
    static const QRegularExpression numbered_device{
        QStringLiteral("^(?:com|lpt)[1-9]$")};
    return numbered_device.match(base).hasMatch();
}

bool is_portable_relative_path(const QString& path)
{
    if (path.isEmpty() || path.contains(QLatin1Char('\\'))
        || path.startsWith(QLatin1Char('/')) || QDir::isAbsolutePath(path))
    {
        return false;
    }
    if (path == QStringLiteral("."))
    {
        return true;
    }

    const auto components = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const auto& component : components)
    {
        if (component.isEmpty() || component == QStringLiteral(".")
            || component == QStringLiteral("..")
            || component.endsWith(QLatin1Char('.'))
            || component.endsWith(QLatin1Char(' '))
            || is_windows_reserved_component(component))
        {
            return false;
        }
        for (const auto character : component)
        {
            if (character.unicode() < 0x20
                || QStringLiteral("<>:\"|?*").contains(character))
            {
                return false;
            }
        }
    }
    return true;
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

bool is_link_or_reparse_point(const QString& path)
{
    const QFileInfo info{path};
    if (info.isSymbolicLink())
    {
        return true;
    }
#ifdef Q_OS_WIN
    if (info.isJunction())
    {
        return true;
    }
    const auto native = QDir::toNativeSeparators(normalized_absolute_path(path));
    const auto attributes = GetFileAttributesW(
        reinterpret_cast<LPCWSTR>(native.utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

bool path_chain_has_link_or_reparse_point(const QString& base, const QString& path)
{
    const auto absolute_base = normalized_absolute_path(base);
    const auto absolute_path = normalized_absolute_path(path);
    if (!relative_path_is_contained(absolute_base, absolute_path))
    {
        return true;
    }

    if (is_link_or_reparse_point(absolute_base))
    {
        return true;
    }

    auto current = absolute_base;
    const auto relative = QDir::fromNativeSeparators(
        QDir{absolute_base}.relativeFilePath(absolute_path));
    for (const auto& segment : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        if (segment == QStringLiteral("."))
        {
            continue;
        }
        current = QDir{current}.filePath(segment);
        const QFileInfo info{current};
        if (is_link_or_reparse_point(current))
        {
            return true;
        }
        if (!info.exists())
        {
            break;
        }
    }
    return false;
}

std::optional<PathSnapshot> snapshot_existing_path(
    const QString& project_root,
    const QString& path,
    ExistingPathKind kind)
{
    const auto absolute = normalized_absolute_path(path);
    if (!relative_path_is_contained(project_root, absolute)
        || !canonical_ancestor_is_contained(project_root, absolute)
        || path_chain_has_link_or_reparse_point(project_root, absolute))
    {
        return std::nullopt;
    }

    QFileInfo info{absolute};
    info.refresh();
    if (!info.exists()
        || (kind == ExistingPathKind::file && !info.isFile())
        || (kind == ExistingPathKind::directory && !info.isDir()))
    {
        return std::nullopt;
    }
    const auto canonical = info.canonicalFilePath();
    if (canonical.isEmpty()
        || !relative_path_is_contained(project_root, canonical)
        || path_key(canonical) != path_key(absolute))
    {
        return std::nullopt;
    }

    return PathSnapshot{
        absolute,
        canonical,
        info.size(),
        info.lastModified().toMSecsSinceEpoch(),
        kind,
    };
}

bool snapshots_match(const PathSnapshot& left, const PathSnapshot& right)
{
    return left.kind == right.kind
        && path_key(left.absolute_path) == path_key(right.absolute_path)
        && path_key(left.canonical_path) == path_key(right.canonical_path)
        && left.size == right.size
        && left.last_modified_msecs == right.last_modified_msecs;
}

ContainedPath resolve_contained_path(
    const QString& project_root,
    const QString& declared_path,
    const QString& relative_base = {})
{
    if (declared_path.trimmed().isEmpty() || !is_portable_relative_path(declared_path))
    {
        return {};
    }

    const auto base = relative_base.isEmpty() ? project_root : relative_base;
    const auto absolute = QDir::cleanPath(QDir{base}.absoluteFilePath(declared_path));
    if (!relative_path_is_contained(project_root, absolute)
        || !canonical_ancestor_is_contained(project_root, absolute)
        || path_chain_has_link_or_reparse_point(project_root, absolute))
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

template <typename Result>
void add_diagnostic(
    Result& result,
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

std::optional<QByteArray> read_stable_bytes(
    const QString& path,
    const QString& project_root,
    ProjectIndexBuildResult& result,
    bool report_invalid)
{
    const auto before = snapshot_existing_path(
        project_root, path, ExistingPathKind::file);
    if (!before)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Document is not a stable, contained regular file: '%1'.").arg(path),
            path);
        return std::nullopt;
    }

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

    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
    {
        if (report_invalid)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unreadable_document,
                QStringLiteral("Cannot completely read '%1': %2").arg(path, file.errorString()),
                path);
        }
        return std::nullopt;
    }
    file.close();

    const auto after = snapshot_existing_path(
        project_root, path, ExistingPathKind::file);
    if (!after || !snapshots_match(*before, *after))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Document path changed while it was being indexed: '%1'.").arg(path),
            path);
        return std::nullopt;
    }
    return bytes;
}

std::optional<QJsonObject> read_json_object(
    const QString& path,
    const QString& project_root,
    ProjectIndexBuildResult& result,
    bool report_invalid,
    QByteArray* source_bytes = nullptr)
{
    const auto bytes = read_stable_bytes(path, project_root, result, report_invalid);
    if (!bytes)
    {
        return std::nullopt;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(*bytes, &error);
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
    if (source_bytes != nullptr)
    {
        *source_bytes = *bytes;
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
            if (uuids)
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

QJsonValue canonicalize_json(const QJsonValue& value)
{
    if (value.isObject())
    {
        const auto object = value.toObject();
        QJsonObject canonical;
        for (const auto& key : object.keys())
        {
            canonical.insert(key, canonicalize_json(object.value(key)));
        }
        return canonical;
    }
    if (value.isArray())
    {
        QJsonArray canonical;
        for (const auto& item : value.toArray())
        {
            canonical.push_back(canonicalize_json(item));
        }
        return canonical;
    }
    return value;
}

std::optional<QStringList> infer_v2_component_roots(
    const QJsonObject& manifest,
    const QString& project_root,
    const QString& manifest_path,
    ProjectIndexBuildResult& result)
{
    constexpr auto components_name = "Components";
    if (manifest.contains(QStringLiteral("componentRoots")))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::invalid_project_format,
            QStringLiteral("Project version 2 contains the version-3 'componentRoots' field; migration is ambiguous."),
            manifest_path,
            QStringLiteral("/componentRoots"));
        return std::nullopt;
    }

    const auto root_before = snapshot_existing_path(
        project_root, project_root, ExistingPathKind::directory);
    if (!root_before)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project root is not stable while inferring the version-2 Components root."),
            manifest_path,
            QStringLiteral("/componentRoots"));
        return std::nullopt;
    }

    QList<QFileInfo> matches;
    const QDir root{project_root};
    const auto entries = root.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const auto& entry : entries)
    {
        if (entry.fileName().normalized(QString::NormalizationForm_C).compare(
                QString::fromLatin1(components_name), Qt::CaseInsensitive)
            == 0)
        {
            matches.push_back(entry);
        }
    }

    const auto root_after = snapshot_existing_path(
        project_root, project_root, ExistingPathKind::directory);
    if (!root_after || !snapshots_match(*root_before, *root_after))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project root changed while inferring the version-2 Components root."),
            manifest_path,
            QStringLiteral("/componentRoots"));
        return std::nullopt;
    }

    if (matches.isEmpty())
    {
        return QStringList{};
    }
    if (matches.size() != 1
        || matches.constFirst().fileName() != QString::fromLatin1(components_name))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project version 2 has an ambiguous case-folding match for the inferred 'Components' root."),
            manifest_path,
            QStringLiteral("/componentRoots/0"));
        return std::nullopt;
    }

    const auto& components = matches.constFirst();
    const auto resolved = resolve_contained_path(project_root, QString::fromLatin1(components_name));
    const auto component_snapshot = resolved.valid
        ? snapshot_existing_path(
              project_root, resolved.absolute_path, ExistingPathKind::directory)
        : std::nullopt;
    if (is_link_or_reparse_point(components.absoluteFilePath())
        || !components.isDir() || !component_snapshot)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("The inferred 'Components' root is not an unambiguous contained directory."),
            manifest_path,
            QStringLiteral("/componentRoots/0"));
        return std::nullopt;
    }
    return QStringList{QString::fromLatin1(components_name)};
}

std::optional<QJsonObject> migrate_project_v2_to_v3(
    const QJsonObject& source,
    const QString& project_root,
    const QString& manifest_path,
    ProjectIndexBuildResult& result)
{
    const auto component_roots = infer_v2_component_roots(
        source, project_root, manifest_path, result);
    if (!component_roots)
    {
        return std::nullopt;
    }

    QJsonArray component_root_array;
    for (const auto& root : *component_roots)
    {
        component_root_array.push_back(root);
    }
    auto migrated = source;
    migrated.insert(
        QStringLiteral("$schema"),
        QStringLiteral("https://dragonpixel.dev/schemas/v3/project.schema.json"));
    migrated.insert(QStringLiteral("formatVersion"), 3);
    migrated.insert(QStringLiteral("componentRoots"), component_root_array);
    return canonicalize_json(migrated).toObject();
}

QJsonObject migrate_project_v3_to_v4(const QJsonObject& source)
{
    auto migrated = source;
    migrated.insert(
        QStringLiteral("$schema"),
        QStringLiteral("https://dragonpixel.dev/schemas/v4/project.schema.json"));
    migrated.insert(QStringLiteral("formatVersion"), 4);
    if (!migrated.value(QStringLiteral("engineRange")).isString())
    {
        migrated.insert(QStringLiteral("engineRange"), QStringLiteral(">=0.1.0 <2.0.0"));
    }
    for (const auto& field : {
             QStringLiteral("requiredSdks"),
             QStringLiteral("buildTargets"),
             QStringLiteral("packageTargets"),
             QStringLiteral("pluginRequirements")})
    {
        if (!migrated.value(field).isArray())
        {
            migrated.insert(field, QJsonArray{});
        }
    }
    return canonicalize_json(migrated).toObject();
}

std::optional<QJsonObject> migrate_project_to_v4(
    const QJsonObject& source,
    int source_version,
    const QString& project_root,
    const QString& manifest_path,
    ProjectIndexBuildResult& result,
    QVector<ProjectIndexMigration>& migrations)
{
    auto current = source;
    auto version = source_version;
    if (version == 1)
    {
        const auto scene_directory = QFileInfo{current.value(QStringLiteral("startupScene")).toString()}.path();
        current.insert(
            QStringLiteral("$schema"),
            QStringLiteral("https://dragonpixel.dev/schemas/v2/project.schema.json"));
        current.insert(QStringLiteral("formatVersion"), 2);
        current.insert(QStringLiteral("sceneRoots"), QJsonArray{
            scene_directory.isEmpty() ? QStringLiteral(".") : scene_directory});
        if (!current.value(QStringLiteral("assetRoots")).isArray())
        {
            current.insert(QStringLiteral("assetRoots"), QJsonArray{});
        }
        migrations.push_back(ProjectIndexMigration{
            1,
            2,
            QStringLiteral("Derived deterministic scene roots for project format version 2."),
        });
        version = 2;
    }
    if (version == 2)
    {
        const auto migrated = migrate_project_v2_to_v3(
            current, project_root, manifest_path, result);
        if (!migrated)
        {
            return std::nullopt;
        }
        current = *migrated;
        migrations.push_back(ProjectIndexMigration{
            2,
            3,
            QStringLiteral("Added deterministic component roots for project format version 3."),
        });
        version = 3;
    }
    if (version == 3)
    {
        current = migrate_project_v3_to_v4(current);
        migrations.push_back(ProjectIndexMigration{
            3,
            4,
            QStringLiteral("Added explicit lifecycle fields for project format version 4."),
        });
        version = 4;
    }
    return version == 4 ? std::optional<QJsonObject>{canonicalize_json(current).toObject()}
                        : std::nullopt;
}

QString logical_path(const QString& root, const QString& path)
{
    return QDir::fromNativeSeparators(QDir{root}.relativeFilePath(path));
}

bool register_portable_path(
    const QString& project_root,
    const QString& absolute_path,
    QHash<QString, QString>& spellings,
    ProjectIndexBuildResult& result,
    const QString& owner)
{
    const auto relative = logical_path(project_root, absolute_path);
    if (!is_portable_relative_path(relative))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("%1 does not use the portable project-relative path grammar: '%2'.")
                .arg(owner, relative),
            absolute_path);
        return false;
    }

    const auto key = relative_path_key(relative);
    const auto existing = spellings.constFind(key);
    if (existing != spellings.cend() && existing.value() != relative)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Portable path alias collision: '%1' and '%2'.")
                .arg(existing.value(), relative),
            absolute_path);
        return false;
    }
    if (existing == spellings.cend())
    {
        spellings.insert(key, relative);
    }
    return true;
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
    case ProjectIndexEntryKind::component_source:
        return QStringLiteral("component source");
    case ProjectIndexEntryKind::component_manifest:
        return QStringLiteral("component metadata");
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
    ProjectIndexBuildResult& result,
    QVector<PathSnapshot>& asset_source_snapshots)
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
        if (scheme == QStringLiteral("file")
            && entry.format_version == 3
            && entry.source_ownership == QStringLiteral("linked"))
        {
            const auto local_path = source_url.toLocalFile();
            const QFileInfo source_info{local_path};
            if (local_path.isEmpty() || !source_info.exists() || !source_info.isFile()
                || is_link_or_reparse_point(source_info.absoluteFilePath()))
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::unsafe_path,
                    QStringLiteral("Linked external asset source is not a stable regular file: '%1'.")
                        .arg(entry.source),
                    entry.absolute_path,
                    QStringLiteral("/source"));
                entry.structurally_valid = false;
                return;
            }
            entry.resolved_source_path = source_info.canonicalFilePath();
            if (entry.resolved_source_path.isEmpty())
            {
                entry.resolved_source_path = source_info.absoluteFilePath();
            }
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
    const QFileInfo source_info{resolved.absolute_path};
    if (!source_info.exists() || !source_info.isFile())
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::missing_asset_source,
            QStringLiteral("Asset source does not exist: '%1'.").arg(entry.source),
            entry.absolute_path,
            QStringLiteral("/source"));
        entry.structurally_valid = false;
        return;
    }
    const auto source_snapshot = snapshot_existing_path(
        project_root, resolved.absolute_path, ExistingPathKind::file);
    if (!source_snapshot)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Asset source is not a stable, contained regular file: '%1'.")
                .arg(entry.source),
            entry.absolute_path,
            QStringLiteral("/source"));
        entry.structurally_valid = false;
        return;
    }
    entry.resolved_source_path = source_snapshot->canonical_path;
    asset_source_snapshots.push_back(*source_snapshot);
}

std::optional<ProjectIndexEntry> parse_index_entry(
    const QString& path,
    const QString& project_root,
    ProjectIndexBuildResult& result,
    std::optional<ProjectIndexEntryKind> expected_kind,
    QVector<PathSnapshot>& asset_source_snapshots,
    QByteArray* source_bytes = nullptr)
{
    const auto object = read_json_object(
        path, project_root, result, expected_kind.has_value(), source_bytes);
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
        if (*version < 1 || *version > 3)
        {
            supported_version = false;
            entry.structurally_valid = false;
        }
        entry.asset_type = required_string(
            *object, QStringLiteral("assetType"), path, result);
        entry.source = required_string(
            *object, QStringLiteral("source"), path, result);
        entry.source_ownership = object->value(QStringLiteral("sourceOwnership")).toString();
        if (entry.asset_type.isEmpty() || entry.source.isEmpty())
        {
            entry.structurally_valid = false;
        }
        entry.dependencies = string_array(
            *object,
            QStringLiteral("dependencies"),
            path,
            result,
            *version >= 2,
            true);
        if (*version >= 2 && !object->value(QStringLiteral("importSettings")).isObject())
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::missing_field,
                QStringLiteral("Version 2 asset requires object 'importSettings'."),
                path,
                QStringLiteral("/importSettings"));
            entry.structurally_valid = false;
        }
        if (*version == 3)
        {
            if (entry.source_ownership != QStringLiteral("copied")
                && entry.source_ownership != QStringLiteral("linked")
                && entry.source_ownership != QStringLiteral("generated"))
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_field,
                    QStringLiteral("Version 3 asset requires copied, linked, or generated sourceOwnership."),
                    path,
                    QStringLiteral("/sourceOwnership"));
                entry.structurally_valid = false;
            }
            for (const auto& field : {
                     QStringLiteral("sourceHash"),
                     QStringLiteral("importHash"),
                     QStringLiteral("cacheKey")})
            {
                if (!object->value(field).isString())
                {
                    add_diagnostic(
                        result,
                        ProjectIndexDiagnosticCode::missing_field,
                        QStringLiteral("Version 3 asset requires string '%1'.").arg(field),
                        path,
                        QStringLiteral("/%1").arg(field));
                    entry.structurally_valid = false;
                }
            }
            if (!object->value(QStringLiteral("importer")).isObject()
                || !object->value(QStringLiteral("dependencyRevisions")).isArray()
                || !object->value(QStringLiteral("recoveryState")).isObject())
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_field,
                    QStringLiteral("Version 3 asset requires importer, dependencyRevisions, and recoveryState records."),
                    path);
                entry.structurally_valid = false;
            }
        }
        validate_asset_source(entry, project_root, result, asset_source_snapshots);
        break;
    case ProjectIndexEntryKind::component_source:
    case ProjectIndexEntryKind::component_manifest:
        return std::nullopt;
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
    QVector<PathSnapshot>& root_snapshots,
    const QString& pointer)
{
    const auto declared_key = QStringLiteral("declared:%1")
                                  .arg(relative_path_key(declared));
    if (declared_roots.contains(declared_key))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::duplicate_root,
            QStringLiteral("Project root is declared more than once: '%1'.").arg(declared),
            candidate.manifest_path,
            pointer);
        return;
    }
    // Record declarations before resolving them so repeated invalid/missing
    // declarations cannot evade duplicate diagnostics.
    declared_roots.insert(declared_key);

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
    const auto snapshot = snapshot_existing_path(
        candidate.project_root, resolved.absolute_path, ExistingPathKind::directory);
    if (!snapshot)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project root is a link/reparse point, escaped, or changed during validation: '%1'.")
                .arg(declared),
            candidate.manifest_path,
            pointer);
        return;
    }
    const auto resolved_key = QStringLiteral("resolved:%1")
                                  .arg(path_key(snapshot->canonical_path));
    if (declared_roots.contains(resolved_key))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::duplicate_root,
            QStringLiteral("Project root aliases another declared root: '%1'.").arg(declared),
            candidate.manifest_path,
            pointer);
        return;
    }
    declared_roots.insert(resolved_key);
    root_snapshots.push_back(*snapshot);
    candidate.roots.push_back(
        ProjectIndexRoot{kind, declared, snapshot->canonical_path});
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

QByteArray ProjectIndexCandidate::canonical_manifest_json() const
{
    auto encoded = QJsonDocument{canonicalize_json(manifest).toObject()}
                       .toJson(QJsonDocument::Compact);
    encoded.push_back('\n');
    return encoded;
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

bool ProjectIndexLocationValidation::succeeded() const noexcept
{
    return location.has_value()
        && std::none_of(diagnostics.cbegin(), diagnostics.cend(), [](const auto& diagnostic) {
               return diagnostic.severity == ProjectIndexDiagnosticSeverity::error;
           });
}

ProjectIndexLocationValidation ProjectIndexService::validate_location(
    const QString& manifest_path) const
{
    ProjectIndexLocationValidation result;
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

    const auto requested_project_root = normalized_absolute_path(manifest_info.absolutePath());
    const auto canonical_project_root = QFileInfo{requested_project_root}.canonicalFilePath();
    const auto canonical_manifest = manifest_info.canonicalFilePath();
    if (canonical_project_root.isEmpty()
        || canonical_manifest.isEmpty()
        || is_link_or_reparse_point(requested_project_root)
        || is_link_or_reparse_point(absolute_manifest)
        || path_key(QFileInfo{canonical_manifest}.absolutePath()) != path_key(canonical_project_root))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project manifest must be reached through a stable, non-link project root."),
            absolute_manifest);
        return result;
    }

    const auto root_before = snapshot_existing_path(
        canonical_project_root, canonical_project_root, ExistingPathKind::directory);
    const auto manifest_snapshot = root_before
        ? snapshot_existing_path(
              canonical_project_root, canonical_manifest, ExistingPathKind::file)
        : std::nullopt;
    const auto root_after = root_before
        ? snapshot_existing_path(
              canonical_project_root, canonical_project_root, ExistingPathKind::directory)
        : std::nullopt;
    if (!root_before || !manifest_snapshot || !root_after
        || !snapshots_match(*root_before, *root_after))
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project manifest/root must be stable, contained regular paths without links or reparse points."),
            absolute_manifest);
        return result;
    }

    result.location = ProjectIndexLocation{
        manifest_snapshot->canonical_path,
        root_before->canonical_path,
    };
    return result;
}

ProjectIndexBuildResult ProjectIndexService::build_candidate(const QString& manifest_path) const
{
    ProjectIndexBuildResult result;
    const auto validated_location = validate_location(manifest_path);
    result.diagnostics = validated_location.diagnostics;
    if (!validated_location.succeeded())
    {
        return result;
    }
    const auto absolute_manifest = validated_location.location->manifest_path;
    const auto canonical_project_root = validated_location.location->project_root;
    const auto project_root_snapshot = snapshot_existing_path(
        canonical_project_root, canonical_project_root, ExistingPathKind::directory);
    if (!project_root_snapshot)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project root is not a stable regular directory."),
            absolute_manifest);
        return result;
    }

    QByteArray manifest_source_bytes;
    const auto manifest_object = read_json_object(
        absolute_manifest,
        canonical_project_root,
        result,
        true,
        &manifest_source_bytes);
    if (!manifest_object)
    {
        return result;
    }

    ProjectIndexCandidate candidate;
    candidate.manifest_path = absolute_manifest;
    candidate.project_root = canonical_project_root;
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
        candidate.source_format_version = *version;
        candidate.format_version = *version;
        if (*version != 1 && *version != 2 && *version != 3 && *version != 4)
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

    QJsonObject effective_manifest = *manifest_object;
    if (version && *version >= 1 && *version <= 4 && !result.has_errors())
    {
        const auto migrated = migrate_project_to_v4(
            *manifest_object,
            *version,
            candidate.project_root,
            absolute_manifest,
            result,
            candidate.migrations);
        if (migrated)
        {
            effective_manifest = *migrated;
            candidate.format_version = 4;
        }
    }
    candidate.manifest = canonicalize_json(effective_manifest).toObject();

    QStringList scene_roots;
    QStringList asset_roots;
    QStringList component_roots;
    if (version && *version >= 1 && *version <= 4)
    {
        scene_roots = string_array(
            candidate.manifest,
            QStringLiteral("sceneRoots"),
            absolute_manifest,
            result,
            true,
            false);
        asset_roots = string_array(
            candidate.manifest,
            QStringLiteral("assetRoots"),
            absolute_manifest,
            result,
            true,
            false);
        if (candidate.format_version >= 3)
        {
            component_roots = string_array(
                candidate.manifest,
                QStringLiteral("componentRoots"),
                absolute_manifest,
                result,
                true,
                false);
        }
        if (!is_sorted(scene_roots))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsorted_roots,
                QStringLiteral("Project sceneRoots must be sorted."),
                absolute_manifest,
                QStringLiteral("/sceneRoots"));
        }
        if (!is_sorted(asset_roots))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsorted_roots,
                QStringLiteral("Project assetRoots must be sorted."),
                absolute_manifest,
                QStringLiteral("/assetRoots"));
        }
        if (!is_sorted(component_roots))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsorted_roots,
                QStringLiteral("Project componentRoots must be sorted."),
                absolute_manifest,
                QStringLiteral("/componentRoots"));
        }
        if (candidate.format_version == 4)
        {
            if (candidate.manifest.value(QStringLiteral("engineRange")).toString().trimmed().isEmpty())
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_field,
                    QStringLiteral("Project version 4 requires non-empty 'engineRange'."),
                    absolute_manifest,
                    QStringLiteral("/engineRange"));
            }
            for (const auto& field : {
                     QStringLiteral("requiredSdks"),
                     QStringLiteral("buildTargets"),
                     QStringLiteral("packageTargets"),
                     QStringLiteral("pluginRequirements")})
            {
                if (!candidate.manifest.value(field).isArray())
                {
                    add_diagnostic(
                        result,
                        ProjectIndexDiagnosticCode::missing_field,
                        QStringLiteral("Project version 4 requires array '%1'.").arg(field),
                        absolute_manifest,
                        QStringLiteral("/%1").arg(field));
                }
            }
        }
    }

    QSet<QString> declared_roots;
    QVector<PathSnapshot> root_snapshots;
    for (qsizetype index = 0; index < scene_roots.size(); ++index)
    {
        add_root(
            candidate,
            result,
            ProjectIndexRootKind::scenes,
            scene_roots.at(index),
            declared_roots,
            root_snapshots,
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
            root_snapshots,
            QStringLiteral("/assetRoots/%1").arg(index));
    }
    for (qsizetype index = 0; index < component_roots.size(); ++index)
    {
        add_root(
            candidate,
            result,
            ProjectIndexRootKind::components,
            component_roots.at(index),
            declared_roots,
            root_snapshots,
            QStringLiteral("/componentRoots/%1").arg(index));
    }

    if (!candidate.startup_scene.isEmpty())
    {
        const auto startup = resolve_contained_path(candidate.project_root, candidate.startup_scene);
        if (!startup.valid)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Startup scene is not a portable contained project-relative path."),
                absolute_manifest,
                QStringLiteral("/startupScene"));
        }
        else
        {
            const QFileInfo startup_info{startup.absolute_path};
            if (!startup_info.exists() || !startup_info.isFile())
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::missing_startup_scene,
                    QStringLiteral("Startup scene does not exist: '%1'.").arg(candidate.startup_scene),
                    absolute_manifest,
                    QStringLiteral("/startupScene"));
            }
            else
            {
                const auto startup_snapshot = snapshot_existing_path(
                    candidate.project_root,
                    startup.absolute_path,
                    ExistingPathKind::file);
                if (!startup_snapshot)
                {
                    add_diagnostic(
                        result,
                        ProjectIndexDiagnosticCode::unsafe_path,
                        QStringLiteral("Startup scene is a link/reparse point or changed during validation."),
                        absolute_manifest,
                        QStringLiteral("/startupScene"));
                }
                else
                {
                    const auto under_scene_root = std::any_of(
                        candidate.roots.cbegin(),
                        candidate.roots.cend(),
                        [&](const auto& root) {
                            return root.kind == ProjectIndexRootKind::scenes
                                && relative_path_is_contained(
                                    root.absolute_path, startup_snapshot->canonical_path);
                        });
                    if (!under_scene_root)
                    {
                        add_diagnostic(
                            result,
                            ProjectIndexDiagnosticCode::startup_scene_not_indexed,
                            QStringLiteral("Startup scene must be beneath a declared sceneRoot."),
                            absolute_manifest,
                            QStringLiteral("/startupScene"));
                    }
                    else
                    {
                        candidate.startup_scene_path = startup_snapshot->canonical_path;
                    }
                }
            }
        }
    }

    QHash<QString, PathSnapshot> indexed_paths;
    QHash<QString, QByteArray> indexed_source_bytes;
    QHash<QString, QString> portable_path_spellings;
    QVector<PathSnapshot> asset_source_snapshots;
    const auto inspect_path = [&](
        const QString& path,
        std::optional<ProjectIndexEntryKind> expected,
        std::optional<ProjectIndexEntryKind> direct_kind) {
        const auto absolute = normalized_absolute_path(path);
        const auto snapshot = snapshot_existing_path(
            candidate.project_root, absolute, ExistingPathKind::file);
        if (!snapshot)
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Indexed file resolves outside the project root."),
                absolute);
            return;
        }
        if (!register_portable_path(
                candidate.project_root,
                snapshot->absolute_path,
                portable_path_spellings,
                result,
                QStringLiteral("Indexed document")))
        {
            return;
        }
        const auto key = path_key(absolute);
        const auto existing = indexed_paths.constFind(key);
        if (existing != indexed_paths.cend())
        {
            const auto first_spelling = QDir::fromNativeSeparators(
                normalized_absolute_path(existing->absolute_path));
            const auto current_spelling = QDir::fromNativeSeparators(
                normalized_absolute_path(snapshot->absolute_path));
            if (first_spelling != current_spelling
                || path_key(existing->canonical_path) != path_key(snapshot->canonical_path))
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::unsafe_path,
                    QStringLiteral("Indexed path has a duplicate case/Unicode alias: '%1' and '%2'.")
                        .arg(existing->absolute_path, snapshot->absolute_path),
                    snapshot->absolute_path);
            }
            return;
        }
        indexed_paths.insert(key, *snapshot);
        if (direct_kind)
        {
            ProjectIndexEntry entry;
            entry.kind = *direct_kind;
            entry.absolute_path = snapshot->canonical_path;
            entry.logical_path = logical_path(candidate.project_root, snapshot->canonical_path);
            entry.display_name = QFileInfo{snapshot->canonical_path}.fileName();
            const auto suffix = QFileInfo{snapshot->canonical_path}.suffix().toLower();
            if (*direct_kind == ProjectIndexEntryKind::component_source)
            {
                entry.asset_type = suffix == QStringLiteral("cs")
                    ? QStringLiteral("C# Script")
                    : suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp")
                        ? QStringLiteral("C++ Header")
                        : QStringLiteral("C++ Source");
            }
            candidate.entries.push_back(std::move(entry));
            return;
        }

        QByteArray source_bytes;
        const auto entry = parse_index_entry(
            snapshot->canonical_path,
            candidate.project_root,
            result,
            expected,
            asset_source_snapshots,
            &source_bytes);
        if (!source_bytes.isNull()) indexed_source_bytes.insert(key, source_bytes);
        if (entry)
        {
            if (!entry->resolved_source_path.isEmpty()
                && entry->source_ownership != QStringLiteral("linked"))
            {
                static_cast<void>(register_portable_path(
                    candidate.project_root,
                    entry->resolved_source_path,
                    portable_path_spellings,
                    result,
                    QStringLiteral("Asset source")));
            }
            candidate.entries.push_back(*entry);
        }
    };

    for (const auto& root : candidate.roots)
    {
        QDirIterator iterator{
            root.absolute_path,
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDirIterator::Subdirectories};
        while (iterator.hasNext())
        {
            const auto path = iterator.next();
            if (is_link_or_reparse_point(path))
            {
                add_diagnostic(
                    result,
                    ProjectIndexDiagnosticCode::unsafe_path,
                    QStringLiteral("Project index refuses linked/reparse-point entries: '%1'.")
                        .arg(path),
                    path);
                continue;
            }
            const auto info = iterator.fileInfo();
            if (info.isDir())
            {
                candidate.discovered_folders.push_back(
                    logical_path(candidate.project_root, info.absoluteFilePath()));
                continue;
            }
            if (!info.isFile())
            {
                continue;
            }
            const auto suffix = QFileInfo{path}.suffix().toLower();
            const auto component_source = root.kind == ProjectIndexRootKind::components
                && (suffix == QStringLiteral("cs") || suffix == QStringLiteral("cpp")
                    || suffix == QStringLiteral("cc") || suffix == QStringLiteral("cxx")
                    || suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp"));
            if (component_source)
            {
                inspect_path(path, std::nullopt, ProjectIndexEntryKind::component_source);
            }
            else if (root.kind == ProjectIndexRootKind::components
                && suffix == QStringLiteral("dpecomponents"))
            {
                inspect_path(path, std::nullopt, ProjectIndexEntryKind::component_manifest);
            }
            else if (suffix == QStringLiteral("dpescene") || suffix == QStringLiteral("dpeprefab")
                || suffix == QStringLiteral("dpeasset"))
            {
                inspect_path(path, expected_kind_for_path(path), std::nullopt);
            }
            else if (suffix == QStringLiteral("json"))
            {
                inspect_path(path, std::nullopt, std::nullopt);
            }
        }
    }
    candidate.discovered_folders.removeDuplicates();
    candidate.discovered_folders.sort();
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

    QStringList indexed_keys = indexed_paths.keys();
    indexed_keys.sort();
    for (const auto& key : indexed_keys)
    {
        const auto& before = indexed_paths.value(key);
        const auto after = snapshot_existing_path(
            candidate.project_root, before.absolute_path, ExistingPathKind::file);
        if (!after || !snapshots_match(before, *after))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Indexed document changed while the candidate was being built: '%1'.")
                    .arg(before.absolute_path),
                before.absolute_path);
            continue;
        }
        const auto source = indexed_source_bytes.constFind(key);
        if (source == indexed_source_bytes.cend())
        {
            continue;
        }
        const auto bytes_after = read_stable_bytes(
            before.absolute_path, candidate.project_root, result, true);
        if (bytes_after && *bytes_after != source.value())
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Indexed document contents changed while the candidate was being built: '%1'.")
                    .arg(before.absolute_path),
                before.absolute_path);
        }
    }

    QSet<QString> revalidated_asset_sources;
    for (const auto& before : asset_source_snapshots)
    {
        const auto key = path_key(before.absolute_path);
        if (revalidated_asset_sources.contains(key))
        {
            continue;
        }
        revalidated_asset_sources.insert(key);
        const auto after = snapshot_existing_path(
            candidate.project_root, before.absolute_path, ExistingPathKind::file);
        if (!after || !snapshots_match(before, *after))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Asset source changed while the candidate index was being built: '%1'.")
                    .arg(before.absolute_path),
                before.absolute_path);
        }
    }

    QSet<QString> revalidated_roots;
    const auto revalidate_root = [&](const PathSnapshot& before) {
        const auto key = path_key(before.absolute_path);
        if (revalidated_roots.contains(key))
        {
            return;
        }
        revalidated_roots.insert(key);
        const auto after = snapshot_existing_path(
            candidate.project_root, before.absolute_path, ExistingPathKind::directory);
        if (!after || !snapshots_match(before, *after))
        {
            add_diagnostic(
                result,
                ProjectIndexDiagnosticCode::unsafe_path,
                QStringLiteral("Project root changed while the candidate index was being built: '%1'.")
                    .arg(before.absolute_path),
                before.absolute_path);
        }
    };
    revalidate_root(*project_root_snapshot);
    for (const auto& snapshot : root_snapshots)
    {
        revalidate_root(snapshot);
    }

    const auto manifest_after = read_stable_bytes(
        absolute_manifest, candidate.project_root, result, true);
    if (manifest_after && *manifest_after != manifest_source_bytes)
    {
        add_diagnostic(
            result,
            ProjectIndexDiagnosticCode::unsafe_path,
            QStringLiteral("Project manifest changed while the candidate index was being built."),
            absolute_manifest);
    }
    result.candidate = std::move(candidate);
    return result;
}
