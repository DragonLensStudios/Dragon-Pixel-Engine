#include "AssetService.h"

#include "ProjectIndexService.h"

#include <dragonpixel/tiles/tile_documents.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace
{
struct ProjectContext final
{
    ProjectIndexCandidate candidate;
    QString project_root;
};

struct PendingFile final
{
    QString final_path;
    QByteArray bytes;
};

QString normalized(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo{path}.absoluteFilePath()));
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray json_bytes(const QJsonObject& object)
{
    return QJsonDocument{object}.toJson(QJsonDocument::Indented);
}

void diagnostic(
    AssetOperationResult& result,
    QString code,
    QString message,
    QString path = {})
{
    result.diagnostics.push_back({std::move(code), std::move(message), std::move(path)});
}

void diagnostic(
    RuntimeAssetBindingResult& result,
    QString code,
    QString message,
    QString path = {})
{
    result.diagnostics.push_back({std::move(code), std::move(message), std::move(path)});
}

std::optional<ProjectContext> load_project(
    const QString& manifest_path,
    AssetOperationResult& result)
{
    const auto indexed = ProjectIndexService{}.build_candidate(manifest_path);
    if (!indexed.succeeded())
    {
        for (const auto& item : indexed.diagnostics)
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-PROJECT"),
                item.message, item.document_path);
        }
        return std::nullopt;
    }
    return ProjectContext{*indexed.candidate, indexed.candidate->project_root};
}

std::optional<ProjectContext> load_project(
    const QString& manifest_path,
    RuntimeAssetBindingResult& result)
{
    const auto indexed = ProjectIndexService{}.build_candidate(manifest_path);
    if (!indexed.succeeded())
    {
        for (const auto& item : indexed.diagnostics)
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-PROJECT"),
                item.message, item.document_path);
        }
        return std::nullopt;
    }
    return ProjectContext{*indexed.candidate, indexed.candidate->project_root};
}

bool portable_relative(const QString& value)
{
    const auto path = QDir::fromNativeSeparators(value.trimmed());
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char{'\\'}))
    {
        return false;
    }
    const auto clean = QDir::cleanPath(path);
    return clean == path && clean != QStringLiteral(".")
        && clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"));
}

bool path_beneath(const QString& root, const QString& candidate)
{
#if defined(Q_OS_WIN)
    constexpr auto path_case = Qt::CaseInsensitive;
#else
    constexpr auto path_case = Qt::CaseSensitive;
#endif
    const auto normalized_root = QDir::fromNativeSeparators(QFileInfo{root}.absoluteFilePath());
    const auto normalized_candidate = QDir::fromNativeSeparators(QFileInfo{candidate}.absoluteFilePath());
    return normalized_candidate == normalized_root
        || normalized_candidate.startsWith(normalized_root + QLatin1Char{'/'}, path_case);
}

std::optional<QString> resolve_asset_folder(
    const ProjectContext& project,
    const QString& relative,
    AssetOperationResult& result)
{
    if (!portable_relative(relative))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-PATH"),
            QStringLiteral("Asset folder must be a portable project-relative path."), relative);
        return std::nullopt;
    }
    const auto destination = normalized(QDir{project.project_root}.filePath(relative));
    bool accepted = false;
    for (const auto& root : project.candidate.roots)
    {
        if (root.kind == ProjectIndexRootKind::assets && path_beneath(root.absolute_path, destination))
        {
            accepted = true;
            break;
        }
    }
    if (!accepted)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-ROOT"),
            QStringLiteral("Asset operations require a destination beneath a declared asset root."),
            destination);
        return std::nullopt;
    }
    return destination;
}

bool read_regular_file(const QString& path, QByteArray& bytes, QString& error)
{
    const QFileInfo info{path};
    if (!info.exists() || !info.isFile() || info.isSymLink())
    {
        error = QStringLiteral("Source must be an existing non-linked regular file.");
        return false;
    }
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
    {
        error = file.errorString();
        return false;
    }
    bytes = file.readAll();
    return true;
}

QString image_media_type(const QByteArray& bytes, const QString& suffix)
{
    const auto image = QImage::fromData(bytes);
    if (image.isNull())
    {
        return {};
    }
    if (suffix.compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0
        && bytes.startsWith("\x89PNG\r\n\x1a\n"))
    {
        return QStringLiteral("image/png");
    }
    if ((suffix.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0
            || suffix.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0)
        && bytes.size() >= 2
        && static_cast<unsigned char>(bytes[0]) == 0xffU
        && static_cast<unsigned char>(bytes[1]) == 0xd8U)
    {
        return QStringLiteral("image/jpeg");
    }
    return {};
}

bool collision_exists(const QString& path)
{
    const QFileInfo info{path};
    const QDir directory{info.absolutePath()};
    const auto target = info.fileName().toCaseFolded();
    for (const auto& existing : directory.entryList(
             QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
    {
        if (existing.toCaseFolded() == target)
        {
            return true;
        }
    }
    return false;
}

bool write_atomic(const QString& path, const QByteArray& bytes, QString& error)
{
    QSaveFile output{path};
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(bytes) != bytes.size()
        || !output.commit())
    {
        error = output.errorString();
        return false;
    }
    return true;
}

bool commit_files(
    const QString& project_root,
    const QString& operation_id,
    const QVector<PendingFile>& pending,
    const QStringList& replace_paths,
    AssetOperationResult& result)
{
    const auto staging = QDir{project_root}.filePath(
        QStringLiteral(".dragonpixel/Staging/%1").arg(operation_id));
    const auto files_root = QDir{staging}.filePath(QStringLiteral("files"));
    const auto backups_root = QDir{staging}.filePath(QStringLiteral("backups"));
    if (!QDir{}.mkpath(files_root) || !QDir{}.mkpath(backups_root))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-STAGING"),
            QStringLiteral("Could not create the asset-operation staging area."), staging);
        return false;
    }
    QJsonArray recovery_files;
    QString error;
    for (qsizetype index = 0; index < pending.size(); ++index)
    {
        const auto staged = QDir{files_root}.filePath(QString::number(index));
        if (!write_atomic(staged, pending.at(index).bytes, error))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-STAGE-WRITE"), error, staged);
            return false;
        }
        recovery_files.push_back(QJsonObject{
            {QStringLiteral("staged"), staged},
            {QStringLiteral("destination"), pending.at(index).final_path},
            {QStringLiteral("sha256"), sha256(pending.at(index).bytes)},
        });
    }
    const auto recovery = QJsonObject{
        {QStringLiteral("format"), QStringLiteral("dpe.asset-operation-recovery")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("operationId"), operation_id},
        {QStringLiteral("files"), recovery_files},
    };
    if (!write_atomic(QDir{staging}.filePath(QStringLiteral("recovery.json")),
            json_bytes(recovery), error))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RECOVERY-WRITE"), error, staging);
        return false;
    }

    QStringList backed_up;
    for (qsizetype index = 0; index < replace_paths.size(); ++index)
    {
        const auto source = replace_paths.at(index);
        if (!QFileInfo::exists(source))
        {
            continue;
        }
        const auto backup = QDir{backups_root}.filePath(QString::number(index));
        if (!QFile::rename(source, backup))
        {
            for (qsizetype rollback = backed_up.size(); rollback > 0; --rollback)
            {
                QFile::rename(QDir{backups_root}.filePath(QString::number(rollback - 1)),
                    backed_up.at(rollback - 1));
            }
            diagnostic(result, QStringLiteral("DPE-ASSET-BACKUP"),
                QStringLiteral("Could not stage an existing asset file for replacement."), source);
            return false;
        }
        backed_up.push_back(source);
    }

    QStringList published;
    for (qsizetype index = 0; index < pending.size(); ++index)
    {
        const auto& file = pending.at(index);
        if (!QDir{}.mkpath(QFileInfo{file.final_path}.absolutePath())
            || collision_exists(file.final_path)
            || !QFile::rename(QDir{files_root}.filePath(QString::number(index)), file.final_path))
        {
            for (const auto& path : published)
            {
                QFile::remove(path);
            }
            for (qsizetype rollback = 0; rollback < backed_up.size(); ++rollback)
            {
                QFile::rename(QDir{backups_root}.filePath(QString::number(rollback)),
                    backed_up.at(rollback));
            }
            diagnostic(result, QStringLiteral("DPE-ASSET-PUBLISH"),
                QStringLiteral("Could not publish the staged asset operation without a collision."),
                file.final_path);
            return false;
        }
        published.push_back(file.final_path);
    }
    QDir{staging}.removeRecursively();
    return true;
}

QJsonObject asset_document(
    const QString& asset_id,
    const QString& asset_type,
    const QString& source,
    const QString& ownership,
    const QString& hash,
    const QString& importer,
    bool importer_supported)
{
    QJsonArray importer_diagnostics;
    if (!importer_supported)
    {
        importer_diagnostics.push_back(QJsonObject{
            {QStringLiteral("severity"), QStringLiteral("warning")},
            {QStringLiteral("code"), QStringLiteral("no-compatible-importer")},
            {QStringLiteral("message"), QStringLiteral("No compatible runtime importer is registered for this file type.")},
        });
    }
    return QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v3/asset-metadata.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.asset")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("engineVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("assetType"), asset_type},
        {QStringLiteral("source"), source},
        {QStringLiteral("sourceOwnership"), ownership},
        {QStringLiteral("sourceHash"), hash},
        {QStringLiteral("importHash"), hash},
        {QStringLiteral("importer"), QJsonObject{{QStringLiteral("id"), importer}, {QStringLiteral("version"), 1}}},
        {QStringLiteral("cacheKey"), QStringLiteral("sha256:") + hash},
        {QStringLiteral("dependencies"), QJsonArray{}},
        {QStringLiteral("dependencyRevisions"), QJsonArray{}},
        {QStringLiteral("importSettings"), QJsonObject{}},
        {QStringLiteral("recoveryState"), QJsonObject{{QStringLiteral("state"), QStringLiteral("ready")}}},
        {QStringLiteral("importerDiagnostics"), importer_diagnostics},
        {QStringLiteral("previewDiagnostics"), QJsonArray{}},
    };
}

std::optional<QJsonObject> read_object(const QString& path)
{
    QFile input{path};
    if (!input.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    const auto document = QJsonDocument::fromJson(input.readAll());
    return document.isObject() ? std::optional<QJsonObject>{document.object()} : std::nullopt;
}

const ProjectIndexEntry* asset_entry(const ProjectContext& project, const QString& asset_id)
{
    const auto* entry = project.candidate.find_by_id(asset_id);
    return entry && entry->kind == ProjectIndexEntryKind::asset ? entry : nullptr;
}

QString imported_metadata_name(const QString& source_file_name)
{
    return source_file_name + QStringLiteral(".dpeasset");
}

bool valid_base_name(const QString& name)
{
    return !name.trimmed().isEmpty() && name == name.trimmed()
        && !name.contains(QLatin1Char{'/'}) && !name.contains(QLatin1Char{'\\'})
        && name != QStringLiteral(".") && name != QStringLiteral("..");
}

QJsonObject tile_asset_document(
    const QString& asset_id,
    const QString& asset_type,
    const QString& source,
    const QString& ownership,
    const QByteArray& source_bytes,
    const QStringList& dependencies,
    const QJsonArray& dependency_revisions,
    const QString& source_map_hash,
    double pixels_per_unit)
{
    const auto hash = sha256(source_bytes);
    QJsonArray dependency_array;
    for (const auto& dependency : dependencies)
    {
        dependency_array.push_back(dependency);
    }
    return QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v3/asset-metadata.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.asset")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("engineVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("assetType"), asset_type},
        {QStringLiteral("source"), source},
        {QStringLiteral("sourceOwnership"), ownership},
        {QStringLiteral("sourceHash"), hash},
        {QStringLiteral("importHash"), hash},
        {QStringLiteral("importer"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("dragonpixel.tiled-json")},
            {QStringLiteral("version"), 1}}},
        {QStringLiteral("cacheKey"), QStringLiteral("sha256:") + hash},
        {QStringLiteral("dependencies"), dependency_array},
        {QStringLiteral("dependencyRevisions"), dependency_revisions},
        {QStringLiteral("importSettings"), QJsonObject{
            {QStringLiteral("sourceMapHash"), source_map_hash},
            {QStringLiteral("pixelsPerUnit"), pixels_per_unit}}},
        {QStringLiteral("recoveryState"), QJsonObject{
            {QStringLiteral("state"), QStringLiteral("ready")}}},
        {QStringLiteral("importerDiagnostics"), QJsonArray{}},
        {QStringLiteral("previewDiagnostics"), QJsonArray{}},
    };
}

QString canonical_uuid(const QString& value)
{
    const QUuid parsed{value};
    return parsed.isNull() ? QString{}
                           : parsed.toString(QUuid::WithoutBraces).toLower();
}

bool safe_tile_import_name(const QString& value)
{
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    return !value.trimmed().isEmpty() && value == value.trimmed()
        && value != QStringLiteral(".") && value != QStringLiteral("..")
        && !value.endsWith(QLatin1Char{'.'})
        && value.size() <= 128
        && std::none_of(value.cbegin(), value.cend(), [&](QChar character) {
               return invalid.contains(character) || character.unicode() < 0x20;
           });
}
} // namespace

AssetService::AssetService(IdProvider id_provider)
    : id_provider_(std::move(id_provider))
{
}

QString AssetService::next_id() const
{
    return id_provider_ ? id_provider_()
                        : QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

AssetOperationResult AssetService::import_files(const AssetImportRequest& request) const
{
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(request.project_manifest_path, result);
    if (!project || request.source_paths.isEmpty())
    {
        if (request.source_paths.isEmpty())
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-IMPORT-SOURCES"),
                QStringLiteral("At least one source file is required."));
        }
        return result;
    }
    const auto folder = resolve_asset_folder(*project, request.destination_folder, result);
    if (!folder || (!QFileInfo{*folder}.isDir() && !QDir{}.mkpath(*folder)))
    {
        return result;
    }

    QVector<PendingFile> pending;
    for (const auto& source_path : request.source_paths)
    {
        QByteArray bytes;
        QString error;
        const auto absolute_source = normalized(source_path);
        if (!read_regular_file(absolute_source, bytes, error))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-IMPORT-READ"), error, absolute_source);
            return result;
        }
        const QFileInfo source_info{absolute_source};
        const auto media_type = image_media_type(bytes, source_info.suffix());
        const auto is_image = !media_type.isEmpty();
        const auto asset_id = next_id();
        const auto hash = sha256(bytes);
        QString metadata_name = imported_metadata_name(source_info.fileName());
        const auto metadata_path = QDir{*folder}.filePath(metadata_name);
        if (collision_exists(metadata_path))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-IMPORT-COLLISION"),
                QStringLiteral("An asset sidecar with the same portable name already exists."),
                metadata_path);
            return result;
        }
        QString source;
        const auto ownership = request.ownership == AssetImportOwnership::copy_into_project
            ? QStringLiteral("copied") : QStringLiteral("linked");
        if (request.ownership == AssetImportOwnership::copy_into_project)
        {
            const auto copied_path = QDir{*folder}.filePath(source_info.fileName());
            if (collision_exists(copied_path))
            {
                diagnostic(result, QStringLiteral("DPE-ASSET-IMPORT-COLLISION"),
                    QStringLiteral("A project file with the same portable name already exists."),
                    copied_path);
                return result;
            }
            source = source_info.fileName();
            pending.push_back({copied_path, bytes});
            result.affected_paths.push_back(copied_path);
        }
        else
        {
            source = QUrl::fromLocalFile(absolute_source).toString(QUrl::FullyEncoded);
        }
        const auto document = asset_document(
            asset_id,
            is_image ? QStringLiteral("sprite") : QStringLiteral("generic"),
            source,
            ownership,
            hash,
            is_image ? QStringLiteral("dragonpixel.image") : QStringLiteral("dragonpixel.generic"),
            is_image);
        pending.push_back({metadata_path, json_bytes(document)});
        result.asset_ids.push_back(asset_id);
        result.metadata_paths.push_back(metadata_path);
        result.affected_paths.push_back(metadata_path);
    }
    if (!commit_files(project->project_root, result.operation_id, pending, {}, result))
    {
        return result;
    }
    const auto validated = ProjectIndexService{}.build_candidate(request.project_manifest_path);
    if (!validated.succeeded())
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-IMPORT-VALIDATION"),
            QStringLiteral("Imported asset publication did not produce a valid project index."));
        return result;
    }
    result.succeeded = true;
    return result;
}

AssetOperationResult AssetService::publish_tile_import(
    const TileAssetPublicationRequest& request) const
{
    constexpr qsizetype max_document_bytes = 64 * 1024 * 1024;
    constexpr qsizetype max_texture_bytes = 64 * 1024 * 1024;
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(request.project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    if (!safe_tile_import_name(request.base_name))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILE-NAME"),
            QStringLiteral("Tile imports require a portable base name of at most 128 characters."),
            request.base_name);
        return result;
    }
    const auto tilemap_id = canonical_uuid(request.tilemap_asset_id);
    const auto tileset_id = canonical_uuid(request.tileset_asset_id);
    const auto texture_id = canonical_uuid(request.texture_asset_id);
    if (tilemap_id.isEmpty() || tileset_id.isEmpty() || texture_id.isEmpty()
        || tilemap_id == tileset_id || tilemap_id == texture_id || tileset_id == texture_id)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILE-IDS"),
            QStringLiteral("Tile import asset identifiers must be three distinct canonical UUIDs."));
        return result;
    }
    if (request.tilemap_bytes.isEmpty() || request.tileset_bytes.isEmpty()
        || request.texture_bytes.isEmpty()
        || request.tilemap_bytes.size() > max_document_bytes
        || request.tileset_bytes.size() > max_document_bytes
        || request.texture_bytes.size() > max_texture_bytes)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILE-SIZE"),
            QStringLiteral("A staged tile import output is empty or exceeds its publication limit."));
        return result;
    }
    const auto parsed_map = dragonpixel::tiles::read_tilemap(
        std::string_view{request.tilemap_bytes.constData(),
            static_cast<std::size_t>(request.tilemap_bytes.size())});
    const auto parsed_set = dragonpixel::tiles::read_tile_set(
        std::string_view{request.tileset_bytes.constData(),
            static_cast<std::size_t>(request.tileset_bytes.size())});
    const auto texture = QImage::fromData(request.texture_bytes, "PNG");
    if (!parsed_map.succeeded() || !parsed_set.succeeded() || texture.isNull())
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILE-DOCUMENT"),
            QStringLiteral("The staged tile documents or atlas PNG failed editor validation."));
        return result;
    }
    const auto& map = *parsed_map.document;
    const auto& set = *parsed_set.document;
    if (QString::fromStdString(map.asset_id.to_string()) != tilemap_id
        || QString::fromStdString(set.asset_id.to_string()) != tileset_id
        || QString::fromStdString(set.texture_asset_id.to_string()) != texture_id
        || map.tile_set_dependencies.size() != 1
        || QString::fromStdString(map.tile_set_dependencies.front().to_string()) != tileset_id
        || !std::isfinite(request.pixels_per_unit) || request.pixels_per_unit <= 0.0
        || std::abs(set.pixels_per_unit - request.pixels_per_unit) > 0.000001)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILE-CONTRACT"),
            QStringLiteral("The staged tile documents do not match the assigned asset contract."));
        return result;
    }
    for (const auto& tile : set.tiles)
    {
        const auto& source = tile.source;
        const auto right = static_cast<long long>(source.x) + source.width;
        const auto bottom = static_cast<long long>(source.y) + source.height;
        if (source.x < 0 || source.y < 0 || source.width <= 0 || source.height <= 0
            || right > texture.width() || bottom > texture.height())
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-TILE-ATLAS"),
                QStringLiteral("A TileSet source rectangle falls outside the staged atlas PNG."));
            return result;
        }
    }

    const auto assets = resolve_asset_folder(*project, QStringLiteral("Assets"), result);
    if (!assets)
    {
        return result;
    }
    const auto texture_path = QDir{*assets}.filePath(
        QStringLiteral("Textures/%1.png").arg(request.base_name));
    const auto tileset_path = QDir{*assets}.filePath(
        QStringLiteral("Tiles/%1.dpetileset").arg(request.base_name));
    const auto tilemap_path = QDir{*assets}.filePath(
        QStringLiteral("Tiles/%1.dpetilemap").arg(request.base_name));
    const auto texture_metadata = QDir{*assets}.filePath(
        request.base_name + QStringLiteral(".texture.dpeasset"));
    const auto tileset_metadata = QDir{*assets}.filePath(
        request.base_name + QStringLiteral(".tileset.dpeasset"));
    const auto tilemap_metadata = QDir{*assets}.filePath(
        request.base_name + QStringLiteral(".tilemap.dpeasset"));
    const QStringList destinations{texture_path, tileset_path, tilemap_path,
        texture_metadata, tileset_metadata, tilemap_metadata};
    for (const auto& path : destinations)
    {
        if (collision_exists(path))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-TILE-COLLISION"),
                QStringLiteral("A tile import destination already exists."), path);
            return result;
        }
    }

    const auto texture_hash = sha256(request.texture_bytes);
    const auto tileset_hash = sha256(request.tileset_bytes);
    const QJsonArray tileset_revisions{QJsonObject{
        {QStringLiteral("assetId"), texture_id},
        {QStringLiteral("sourceHash"), texture_hash}}};
    const QJsonArray tilemap_revisions{QJsonObject{
        {QStringLiteral("assetId"), tileset_id},
        {QStringLiteral("sourceHash"), tileset_hash}}};
    const auto texture_document = tile_asset_document(texture_id,
        QStringLiteral("sprite"), QStringLiteral("Textures/%1.png").arg(request.base_name),
        QStringLiteral("copied"), request.texture_bytes, {}, {}, request.source_map_hash,
        request.pixels_per_unit);
    const auto tileset_document = tile_asset_document(tileset_id,
        QStringLiteral("tileset"), QStringLiteral("Tiles/%1.dpetileset").arg(request.base_name),
        QStringLiteral("generated"), request.tileset_bytes, {texture_id},
        tileset_revisions, request.source_map_hash, request.pixels_per_unit);
    const auto tilemap_document = tile_asset_document(tilemap_id,
        QStringLiteral("tilemap"), QStringLiteral("Tiles/%1.dpetilemap").arg(request.base_name),
        QStringLiteral("generated"), request.tilemap_bytes, {tileset_id},
        tilemap_revisions, request.source_map_hash, request.pixels_per_unit);
    const QVector<PendingFile> pending{
        {texture_path, request.texture_bytes},
        {tileset_path, request.tileset_bytes},
        {tilemap_path, request.tilemap_bytes},
        {texture_metadata, json_bytes(texture_document)},
        {tileset_metadata, json_bytes(tileset_document)},
        {tilemap_metadata, json_bytes(tilemap_document)},
    };
    result.asset_ids = {texture_id, tileset_id, tilemap_id};
    result.metadata_paths = {texture_metadata, tileset_metadata, tilemap_metadata};
    result.affected_paths = destinations;
    if (!commit_files(project->project_root, result.operation_id, pending, {}, result))
    {
        result.asset_ids.clear();
        result.metadata_paths.clear();
        result.affected_paths.clear();
        return result;
    }
    const auto validated = ProjectIndexService{}.build_candidate(request.project_manifest_path);
    if (!validated.succeeded())
    {
        for (const auto& path : destinations)
        {
            QFile::remove(path);
        }
        diagnostic(result, QStringLiteral("DPE-ASSET-TILE-VALIDATION"),
            QStringLiteral("Published tile assets failed project-index validation and were removed."));
        result.asset_ids.clear();
        result.metadata_paths.clear();
        result.affected_paths.clear();
        return result;
    }
    result.succeeded = true;
    return result;
}

AssetOperationResult AssetService::create_tilemap(
    const TilemapCreationRequest& request) const
{
    constexpr qsizetype max_tileset_bytes = 64 * 1024 * 1024;
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(request.project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    if (!safe_tile_import_name(request.name))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-NAME"),
            QStringLiteral("Tilemap names must be portable and contain at most 128 characters."),
            request.name);
        return result;
    }
    const auto tileset_id = canonical_uuid(request.tileset_asset_id);
    const auto* tileset_entry = asset_entry(*project, tileset_id);
    if (tileset_id.isEmpty() || tileset_entry == nullptr
        || !tileset_entry->structurally_valid
        || !tileset_entry->asset_type.contains(QStringLiteral("tileset"), Qt::CaseInsensitive))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-TILESET"),
            QStringLiteral("Create Tilemap requires one structurally valid indexed TileSet."),
            request.tileset_asset_id);
        return result;
    }
    QByteArray tileset_bytes;
    QString read_error;
    if (!read_regular_file(tileset_entry->resolved_source_path, tileset_bytes, read_error)
        || tileset_bytes.isEmpty() || tileset_bytes.size() > max_tileset_bytes)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-TILESET-READ"),
            read_error.isEmpty()
                ? QStringLiteral("The selected TileSet is empty or exceeds its validation limit.")
                : read_error,
            tileset_entry->resolved_source_path);
        return result;
    }
    const auto parsed_set = dragonpixel::tiles::read_tile_set(
        std::string_view{tileset_bytes.constData(),
            static_cast<std::size_t>(tileset_bytes.size())});
    if (!parsed_set.succeeded()
        || QString::fromStdString(parsed_set.document->asset_id.to_string()) != tileset_id)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-TILESET-DOCUMENT"),
            QStringLiteral("The selected TileSet document is invalid or does not match its asset identity."),
            tileset_entry->resolved_source_path);
        return result;
    }

    const auto tilemap_id = canonical_uuid(next_id());
    const auto layer_id = canonical_uuid(next_id());
    if (tilemap_id.isEmpty() || layer_id.isEmpty() || tilemap_id == layer_id
        || project->candidate.find_by_id(tilemap_id) != nullptr)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-IDS"),
            QStringLiteral("The Tilemap operation did not produce distinct canonical identities."));
        return result;
    }
    dragonpixel::tiles::tilemap_document map{
        *dragonpixel::core::uuid::parse(tilemap_id.toStdString()),
        request.name.toStdString(),
        {parsed_set.document->asset_id},
        {{*dragonpixel::core::uuid::parse(layer_id.toStdString()), "Layer 1", true, 0, {}}},
    };
    const auto map_bytes = QByteArray::fromStdString(
        dragonpixel::tiles::write_tilemap(map));
    const auto parsed_map = dragonpixel::tiles::read_tilemap(
        std::string_view{map_bytes.constData(), static_cast<std::size_t>(map_bytes.size())});
    if (!parsed_map.succeeded())
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-DOCUMENT"),
            QStringLiteral("The generated empty Tilemap failed native validation."));
        return result;
    }

    const auto assets = resolve_asset_folder(*project, QStringLiteral("Assets"), result);
    if (!assets)
    {
        return result;
    }
    const auto tilemap_path = QDir{*assets}.filePath(
        QStringLiteral("Tiles/%1.dpetilemap").arg(request.name));
    const auto metadata_path = QDir{*assets}.filePath(
        request.name + QStringLiteral(".tilemap.dpeasset"));
    for (const auto& path : {tilemap_path, metadata_path})
    {
        if (collision_exists(path))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-COLLISION"),
                QStringLiteral("A Tilemap destination already exists."), path);
            return result;
        }
    }

    const auto map_hash = sha256(map_bytes);
    const auto tileset_hash = sha256(tileset_bytes);
    const auto source = QStringLiteral("Tiles/%1.dpetilemap").arg(request.name);
    const QJsonObject metadata{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v3/asset-metadata.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.asset")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("engineVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("assetId"), tilemap_id},
        {QStringLiteral("assetType"), QStringLiteral("tilemap")},
        {QStringLiteral("source"), source},
        {QStringLiteral("sourceOwnership"), QStringLiteral("generated")},
        {QStringLiteral("sourceHash"), map_hash},
        {QStringLiteral("importHash"), map_hash},
        {QStringLiteral("importer"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("dragonpixel.tilemap-editor")},
            {QStringLiteral("version"), 1}}},
        {QStringLiteral("cacheKey"), QStringLiteral("sha256:") + map_hash},
        {QStringLiteral("dependencies"), QJsonArray{tileset_id}},
        {QStringLiteral("dependencyRevisions"), QJsonArray{QJsonObject{
            {QStringLiteral("assetId"), tileset_id},
            {QStringLiteral("sourceHash"), tileset_hash}}}},
        {QStringLiteral("importSettings"), QJsonObject{
            {QStringLiteral("grid"), QStringLiteral("orthogonal")}}},
        {QStringLiteral("recoveryState"), QJsonObject{
            {QStringLiteral("state"), QStringLiteral("ready")}}},
        {QStringLiteral("importerDiagnostics"), QJsonArray{}},
        {QStringLiteral("previewDiagnostics"), QJsonArray{}},
    };
    const QVector<PendingFile> pending{
        {tilemap_path, map_bytes},
        {metadata_path, json_bytes(metadata)},
    };
    result.asset_ids = {tilemap_id};
    result.metadata_paths = {metadata_path};
    result.affected_paths = {tilemap_path, metadata_path};
    if (!commit_files(project->project_root, result.operation_id, pending, {}, result))
    {
        result.asset_ids.clear();
        result.metadata_paths.clear();
        result.affected_paths.clear();
        return result;
    }
    const auto validated = ProjectIndexService{}.build_candidate(request.project_manifest_path);
    const auto* validated_map = validated.candidate
        ? validated.candidate->find_by_id(tilemap_id) : nullptr;
    if (!validated.succeeded() || validated_map == nullptr
        || validated_map->dependencies != QStringList{tileset_id})
    {
        QFile::remove(tilemap_path);
        QFile::remove(metadata_path);
        diagnostic(result, QStringLiteral("DPE-ASSET-TILEMAP-VALIDATION"),
            QStringLiteral("The created Tilemap failed project-index validation and was removed."));
        result.asset_ids.clear();
        result.metadata_paths.clear();
        result.affected_paths.clear();
        return result;
    }
    result.succeeded = true;
    return result;
}

AssetOperationResult AssetService::create_folder(
    const QString& project_manifest_path,
    const QString& project_relative_folder) const
{
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    const auto folder = resolve_asset_folder(*project, project_relative_folder, result);
    if (!folder || QFileInfo::exists(*folder) || !QDir{}.mkpath(*folder))
    {
        if (folder)
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-CREATE"),
                QStringLiteral("The asset folder already exists or could not be created."), *folder);
        }
        return result;
    }
    result.affected_paths.push_back(*folder);
    result.succeeded = true;
    return result;
}

AssetOperationResult AssetService::rename_asset(
    const QString& project_manifest_path,
    const QString& asset_id,
    const QString& new_base_name) const
{
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(project_manifest_path, result);
    if (!project || !valid_base_name(new_base_name))
    {
        if (!valid_base_name(new_base_name))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-RENAME-NAME"),
                QStringLiteral("Asset name is empty or contains a path separator."));
        }
        return result;
    }
    const auto* entry = asset_entry(*project, asset_id);
    if (!entry || entry->source_ownership == QStringLiteral("generated"))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RENAME-TARGET"),
            QStringLiteral("Asset is missing or is generated and cannot be renamed through the generic asset service."));
        return result;
    }
    auto document = entry->document;
    const auto suffix = QFileInfo{entry->resolved_source_path}.suffix();
    const auto source_name = suffix.isEmpty()
        ? new_base_name : new_base_name + QLatin1Char{'.'} + suffix;
    const auto metadata_path = QDir{QFileInfo{entry->absolute_path}.absolutePath()}
                                   .filePath(imported_metadata_name(source_name));
    QVector<PendingFile> pending;
    QStringList replace{entry->absolute_path};
    if (entry->source_ownership == QStringLiteral("copied"))
    {
        QByteArray source_bytes;
        QString error;
        if (!read_regular_file(entry->resolved_source_path, source_bytes, error))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-RENAME-READ"), error, entry->resolved_source_path);
            return result;
        }
        const auto source_path = QDir{QFileInfo{entry->absolute_path}.absolutePath()}.filePath(source_name);
        pending.push_back({source_path, source_bytes});
        replace.push_back(entry->resolved_source_path);
        document.insert(QStringLiteral("source"), source_name);
        result.affected_paths.push_back(source_path);
    }
    pending.push_back({metadata_path, json_bytes(document)});
    result.affected_paths.push_back(metadata_path);
    if (!commit_files(project->project_root, result.operation_id, pending, replace, result))
    {
        return result;
    }
    result.asset_ids.push_back(entry->id);
    result.metadata_paths.push_back(metadata_path);
    result.succeeded = true;
    return result;
}

AssetOperationResult AssetService::move_asset(
    const QString& project_manifest_path,
    const QString& asset_id,
    const QString& destination_folder) const
{
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    const auto folder = resolve_asset_folder(*project, destination_folder, result);
    const auto* entry = asset_entry(*project, asset_id);
    if (!folder || !entry || entry->source_ownership == QStringLiteral("generated"))
    {
        if (!entry)
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-MOVE-TARGET"),
                QStringLiteral("Asset was not found."), asset_id);
        }
        return result;
    }
    if (!QFileInfo{*folder}.isDir() && !QDir{}.mkpath(*folder))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-MOVE-FOLDER"),
            QStringLiteral("Destination asset folder does not exist."), *folder);
        return result;
    }
    auto document = entry->document;
    QVector<PendingFile> pending;
    QStringList replace{entry->absolute_path};
    const auto metadata_path = QDir{*folder}.filePath(QFileInfo{entry->absolute_path}.fileName());
    if (entry->source_ownership == QStringLiteral("copied"))
    {
        QByteArray source_bytes;
        QString error;
        if (!read_regular_file(entry->resolved_source_path, source_bytes, error))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-MOVE-READ"), error, entry->resolved_source_path);
            return result;
        }
        const auto source_path = QDir{*folder}.filePath(QFileInfo{entry->resolved_source_path}.fileName());
        pending.push_back({source_path, source_bytes});
        replace.push_back(entry->resolved_source_path);
        document.insert(QStringLiteral("source"), QFileInfo{source_path}.fileName());
        result.affected_paths.push_back(source_path);
    }
    pending.push_back({metadata_path, json_bytes(document)});
    result.affected_paths.push_back(metadata_path);
    if (!commit_files(project->project_root, result.operation_id, pending, replace, result))
    {
        return result;
    }
    result.succeeded = true;
    result.asset_ids.push_back(entry->id);
    result.metadata_paths.push_back(metadata_path);
    return result;
}

AssetOperationResult AssetService::move_folder(
    const QString& project_manifest_path,
    const QString& source_folder,
    const QString& destination_folder) const
{
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    const auto source = resolve_asset_folder(*project, source_folder, result);
    const auto destination = resolve_asset_folder(*project, destination_folder, result);
    if (!source || !destination)
    {
        return result;
    }
    const QFileInfo source_info{*source};
    const QFileInfo destination_info{*destination};
    if (!source_info.isDir() || source_info.isSymLink()
        || !destination_info.isDir() || destination_info.isSymLink())
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-TARGET"),
            QStringLiteral("Folder moves require existing, non-linked source and destination folders."),
            !source_info.isDir() ? *source : *destination);
        return result;
    }
    const auto source_is_root = std::any_of(
        project->candidate.roots.cbegin(), project->candidate.roots.cend(),
        [&](const auto& root) {
            return root.kind == ProjectIndexRootKind::assets
                && normalized(root.absolute_path).compare(*source,
#if defined(Q_OS_WIN)
                    Qt::CaseInsensitive
#else
                    Qt::CaseSensitive
#endif
                    ) == 0;
        });
    if (source_is_root)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-ROOT"),
            QStringLiteral("A declared asset root cannot be moved."), *source);
        return result;
    }
    const auto target = normalized(QDir{*destination}.filePath(source_info.fileName()));
    if (path_beneath(*source, target))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-CYCLE"),
            QStringLiteral("A folder cannot be moved into itself or one of its descendants."), target);
        return result;
    }
    if (collision_exists(target))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-COLLISION"),
            QStringLiteral("The destination already contains an entry with the same portable name."), target);
        return result;
    }
    QDirIterator scan{*source,
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories};
    while (scan.hasNext())
    {
        scan.next();
        if (scan.fileInfo().isSymLink())
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-LINK"),
                QStringLiteral("Folder moves reject linked descendants."), scan.filePath());
            return result;
        }
    }

    const auto staging = QDir{project->project_root}.filePath(
        QStringLiteral(".dragonpixel/Staging/%1").arg(result.operation_id));
    QString error;
    if (!QDir{}.mkpath(staging)
        || !write_atomic(QDir{staging}.filePath(QStringLiteral("recovery.json")),
            json_bytes(QJsonObject{
                {QStringLiteral("format"), QStringLiteral("dpe.asset-folder-move-recovery")},
                {QStringLiteral("formatVersion"), 1},
                {QStringLiteral("operationId"), result.operation_id},
                {QStringLiteral("source"), *source},
                {QStringLiteral("destination"), target},
            }), error))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-RECOVERY"),
            error.isEmpty() ? QStringLiteral("Could not create folder-move recovery evidence.") : error,
            staging);
        return result;
    }
    if (!QDir{}.rename(*source, target))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-COMMIT"),
            QStringLiteral("The atomic folder rename could not be committed."), *source);
        QDir{staging}.removeRecursively();
        return result;
    }
    const auto validated = ProjectIndexService{}.build_candidate(project_manifest_path);
    if (!validated.succeeded())
    {
        if (!QDir{}.rename(target, *source))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-ROLLBACK"),
                QStringLiteral("Validation failed and automatic rollback could not restore the source folder; recovery evidence was retained."),
                staging);
        }
        else
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-FOLDER-MOVE-VALIDATION"),
                QStringLiteral("The moved folder did not produce a valid project index; the move was rolled back."),
                target);
            QDir{staging}.removeRecursively();
        }
        return result;
    }
    QDir{staging}.removeRecursively();
    result.affected_paths = {*source, target};
    result.succeeded = true;
    return result;
}

AssetOperationResult AssetService::duplicate_asset(
    const QString& project_manifest_path,
    const QString& asset_id,
    const QString& destination_folder) const
{
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    const auto* entry = asset_entry(*project, asset_id);
    if (!entry || entry->source_ownership == QStringLiteral("generated"))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-DUPLICATE-TARGET"),
            QStringLiteral("Asset is missing or generated."), asset_id);
        return result;
    }
    const auto relative_folder = destination_folder.isEmpty()
        ? QDir{project->project_root}.relativeFilePath(QFileInfo{entry->absolute_path}.absolutePath())
        : destination_folder;
    const auto folder = resolve_asset_folder(*project, QDir::fromNativeSeparators(relative_folder), result);
    if (!folder)
    {
        return result;
    }
    const auto source_info = QFileInfo{entry->resolved_source_path};
    const auto stem = source_info.completeBaseName() + QStringLiteral(" Copy");
    const auto source_name = source_info.suffix().isEmpty()
        ? stem : stem + QLatin1Char{'.'} + source_info.suffix();
    const auto metadata_path = QDir{*folder}.filePath(imported_metadata_name(source_name));
    auto document = entry->document;
    const auto new_id = next_id();
    document.insert(QStringLiteral("assetId"), new_id);
    QVector<PendingFile> pending;
    if (entry->source_ownership == QStringLiteral("copied"))
    {
        QByteArray source_bytes;
        QString error;
        if (!read_regular_file(entry->resolved_source_path, source_bytes, error))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-DUPLICATE-READ"), error, entry->resolved_source_path);
            return result;
        }
        const auto source_path = QDir{*folder}.filePath(source_name);
        document.insert(QStringLiteral("source"), source_name);
        pending.push_back({source_path, source_bytes});
        result.affected_paths.push_back(source_path);
    }
    pending.push_back({metadata_path, json_bytes(document)});
    result.affected_paths.push_back(metadata_path);
    if (!commit_files(project->project_root, result.operation_id, pending, {}, result))
    {
        return result;
    }
    result.succeeded = true;
    result.asset_ids.push_back(new_id);
    result.metadata_paths.push_back(metadata_path);
    return result;
}

AssetDependencyImpact AssetService::dependency_impact(
    const QString& project_manifest_path,
    const QString& asset_id) const
{
    AssetDependencyImpact impact;
    impact.asset_id = asset_id;
    const auto indexed = ProjectIndexService{}.build_candidate(project_manifest_path);
    if (!indexed.candidate)
    {
        return impact;
    }
    for (const auto& entry : indexed.candidate->entries)
    {
        if (entry.dependencies.contains(asset_id, Qt::CaseInsensitive))
        {
            impact.dependent_ids.push_back(entry.id);
            impact.dependent_paths.push_back(entry.absolute_path);
        }
    }
    return impact;
}

AssetOperationResult AssetService::trash_asset(
    const QString& project_manifest_path,
    const QString& asset_id) const
{
    AssetOperationResult result;
    result.operation_id = next_id();
    const auto project = load_project(project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    const auto* entry = asset_entry(*project, asset_id);
    if (!entry || entry->source_ownership == QStringLiteral("generated"))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TRASH-TARGET"),
            QStringLiteral("Asset is missing or generated."), asset_id);
        return result;
    }
    const auto trash_root = QDir{project->project_root}.filePath(
        QStringLiteral(".dragonpixel/Trash/%1").arg(result.operation_id));
    const auto files_root = QDir{trash_root}.filePath(QStringLiteral("files"));
    if (!QDir{}.mkpath(files_root))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-TRASH-DIRECTORY"),
            QStringLiteral("Could not create recoverable project trash."), trash_root);
        return result;
    }
    QStringList originals{entry->absolute_path};
    if (entry->source_ownership == QStringLiteral("copied"))
    {
        originals.push_back(entry->resolved_source_path);
    }
    QJsonArray records;
    QStringList moved;
    for (qsizetype index = 0; index < originals.size(); ++index)
    {
        const auto original = originals.at(index);
        QFile input{original};
        if (!input.open(QIODevice::ReadOnly))
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-TRASH-READ"), input.errorString(), original);
            return result;
        }
        const auto bytes = input.readAll();
        input.close();
        const auto trash_path = QDir{files_root}.filePath(QString::number(index));
        if (!QFile::rename(original, trash_path))
        {
            for (qsizetype rollback = 0; rollback < moved.size(); ++rollback)
            {
                QFile::rename(QDir{files_root}.filePath(QString::number(rollback)), moved.at(rollback));
            }
            diagnostic(result, QStringLiteral("DPE-ASSET-TRASH-MOVE"),
                QStringLiteral("Could not move an owned asset file into recoverable trash."), original);
            return result;
        }
        moved.push_back(original);
        records.push_back(QJsonObject{
            {QStringLiteral("originalPath"), original},
            {QStringLiteral("trashPath"), trash_path},
            {QStringLiteral("sha256"), sha256(bytes)},
        });
    }
    const auto manifest = QJsonObject{
        {QStringLiteral("format"), QStringLiteral("dpe.asset-trash")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("operationId"), result.operation_id},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("files"), records},
    };
    QString error;
    const auto recovery_path = QDir{trash_root}.filePath(QStringLiteral("recovery.json"));
    if (!write_atomic(recovery_path, json_bytes(manifest), error))
    {
        for (qsizetype rollback = 0; rollback < moved.size(); ++rollback)
        {
            QFile::rename(QDir{files_root}.filePath(QString::number(rollback)), moved.at(rollback));
        }
        diagnostic(result, QStringLiteral("DPE-ASSET-TRASH-RECOVERY"), error, recovery_path);
        return result;
    }
    result.succeeded = true;
    result.asset_ids.push_back(asset_id);
    result.affected_paths = originals;
    return result;
}

AssetOperationResult AssetService::restore_trash(
    const QString& project_manifest_path,
    const QString& operation_id) const
{
    AssetOperationResult result;
    result.operation_id = operation_id;
    const auto indexed = ProjectIndexService{}.build_candidate(project_manifest_path);
    if (!indexed.candidate)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RESTORE-PROJECT"),
            QStringLiteral("Project location could not be indexed for recovery."), project_manifest_path);
        return result;
    }
    const ProjectContext project{*indexed.candidate, indexed.candidate->project_root};
    const auto trash_root = QDir{project.project_root}.filePath(
        QStringLiteral(".dragonpixel/Trash/%1").arg(operation_id));
    const auto manifest = read_object(QDir{trash_root}.filePath(QStringLiteral("recovery.json")));
    if (!manifest || manifest->value(QStringLiteral("format")).toString()
            != QStringLiteral("dpe.asset-trash"))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RESTORE-MANIFEST"),
            QStringLiteral("Trash recovery manifest was not found or was invalid."), trash_root);
        return result;
    }
    const auto records = manifest->value(QStringLiteral("files")).toArray();
    for (const auto& value : records)
    {
        const auto record = value.toObject();
        const auto original = record.value(QStringLiteral("originalPath")).toString();
        const auto trash_path = record.value(QStringLiteral("trashPath")).toString();
        if (QFileInfo::exists(original) || !QFileInfo{trash_path}.isFile())
        {
            diagnostic(result, QStringLiteral("DPE-ASSET-RESTORE-COLLISION"),
                QStringLiteral("Restore target is occupied or trash bytes are missing."), original);
            return result;
        }
    }
    QStringList restored;
    for (const auto& value : records)
    {
        const auto record = value.toObject();
        const auto original = record.value(QStringLiteral("originalPath")).toString();
        const auto trash_path = record.value(QStringLiteral("trashPath")).toString();
        if (!QDir{}.mkpath(QFileInfo{original}.absolutePath()) || !QFile::rename(trash_path, original))
        {
            for (const auto& rollback : restored)
            {
                QFile::rename(rollback, QDir{trash_root}.filePath(
                    QStringLiteral("files/%1").arg(restored.indexOf(rollback))));
            }
            diagnostic(result, QStringLiteral("DPE-ASSET-RESTORE-PUBLISH"),
                QStringLiteral("Could not restore exact asset bytes."), original);
            return result;
        }
        restored.push_back(original);
    }
    QDir{trash_root}.removeRecursively();
    result.succeeded = true;
    result.asset_ids.push_back(manifest->value(QStringLiteral("assetId")).toString());
    result.affected_paths = restored;
    return result;
}

RuntimeAssetBindingResult AssetService::runtime_binding(
    const QString& project_manifest_path,
    const QString& asset_id) const
{
    RuntimeAssetBindingResult result;
    result.asset_id = asset_id;
    const auto project = load_project(project_manifest_path, result);
    if (!project)
    {
        return result;
    }
    const auto* entry = asset_entry(*project, asset_id);
    if (!entry || entry->asset_type != QStringLiteral("sprite"))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RUNTIME-TYPE"),
            QStringLiteral("Only validated PNG/JPEG sprite assets currently produce runtime bindings."),
            asset_id);
        return result;
    }
    QByteArray bytes;
    QString error;
    if (!read_regular_file(entry->resolved_source_path, bytes, error))
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RUNTIME-READ"), error, entry->resolved_source_path);
        return result;
    }
    const auto hash = sha256(bytes);
    if (entry->document.value(QStringLiteral("sourceHash")).toString() != hash)
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RUNTIME-HASH"),
            QStringLiteral("Asset source bytes changed after import; reimport is required."),
            entry->resolved_source_path);
        return result;
    }
    const auto media_type = image_media_type(bytes, QFileInfo{entry->resolved_source_path}.suffix());
    if (media_type.isEmpty())
    {
        diagnostic(result, QStringLiteral("DPE-ASSET-RUNTIME-DECODE"),
            QStringLiteral("Asset source is not a validated PNG or JPEG."), entry->resolved_source_path);
        return result;
    }
    result.media_type = media_type;
    result.content_hash = hash;
    result.immutable_bytes = bytes;
    result.succeeded = true;
    return result;
}
