#include "TileImportService.h"

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/tiles/tile_documents.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace
{
constexpr qsizetype max_request_source_bytes = 16 * 1024 * 1024;
constexpr qsizetype max_result_bytes = 1024 * 1024;
constexpr qsizetype max_document_bytes = 64 * 1024 * 1024;
constexpr qsizetype max_texture_bytes = 64 * 1024 * 1024;

void diagnostic(TileImportResult& result, QString code, QString message, QString path = {})
{
    result.diagnostics.push_back({std::move(code), std::move(message), std::move(path)});
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool read_regular_file(
    const QString& path,
    qsizetype maximum,
    QByteArray& bytes,
    QString& error)
{
    const QFileInfo info{path};
    if (!info.exists() || !info.isFile() || info.isSymLink())
    {
        error = QStringLiteral("Path must be an existing non-linked regular file.");
        return false;
    }
    if (info.size() < 0 || info.size() > maximum)
    {
        error = QStringLiteral("File exceeds the supported size limit.");
        return false;
    }
    QFile input{path};
    if (!input.open(QIODevice::ReadOnly))
    {
        error = input.errorString();
        return false;
    }
    bytes = input.readAll();
    if (input.error() != QFileDevice::NoError || bytes.size() != info.size())
    {
        error = input.errorString().isEmpty()
            ? QStringLiteral("File changed or could not be completely read.")
            : input.errorString();
        return false;
    }
    return true;
}

bool write_request(const QString& path, const QJsonObject& object, QString& error)
{
    const auto bytes = QJsonDocument{object}.toJson(QJsonDocument::Indented);
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

bool contained_output(const QString& staging, const QString& name, QString& path)
{
    if (name.isEmpty() || name != QFileInfo{name}.fileName()
        || QDir::isAbsolutePath(name) || name.contains(QLatin1Char{'/'})
        || name.contains(QLatin1Char{'\\'}))
    {
        return false;
    }
    const QFileInfo root_info{staging};
    const QFileInfo output_info{QDir{staging}.filePath(name)};
    const auto root = QDir::fromNativeSeparators(root_info.canonicalFilePath());
    const auto candidate = QDir::fromNativeSeparators(output_info.canonicalFilePath());
#if defined(Q_OS_WIN)
    constexpr auto path_case = Qt::CaseInsensitive;
#else
    constexpr auto path_case = Qt::CaseSensitive;
#endif
    if (root.isEmpty() || candidate.isEmpty() || output_info.isSymLink()
        || !output_info.isFile()
        || !candidate.startsWith(root + QLatin1Char{'/'}, path_case))
    {
        return false;
    }
    path = candidate;
    return true;
}

TileImportWorkerOutcome run_worker(
    const QString& program,
    const QStringList& arguments,
    int timeout_milliseconds,
    const std::function<bool()>& cancellation_requested)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(std::min(timeout_milliseconds, 10'000)))
    {
        return {TileImportWorkerStatus::failed_to_start, -1, process.errorString()};
    }
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (cancellation_requested && cancellation_requested())
        {
            process.terminate();
            if (!process.waitForFinished(1'000))
            {
                process.kill();
                process.waitForFinished(1'000);
            }
            return {TileImportWorkerStatus::cancelled, -1,
                QString::fromUtf8(process.readAllStandardError())};
        }
        if (elapsed.elapsed() >= timeout_milliseconds)
        {
            process.terminate();
            if (!process.waitForFinished(1'000))
            {
                process.kill();
                process.waitForFinished(1'000);
            }
            return {TileImportWorkerStatus::timed_out, -1,
                QString::fromUtf8(process.readAllStandardError())};
        }
        process.waitForFinished(25);
    }
    return {
        process.exitStatus() == QProcess::NormalExit
            ? TileImportWorkerStatus::completed : TileImportWorkerStatus::crashed,
        process.exitCode(),
        QString::fromUtf8(process.readAllStandardError()),
    };
}

void append_worker_diagnostics(const QJsonArray& values, TileImportResult& result)
{
    for (const auto& value : values)
    {
        const auto item = value.toObject();
        const auto code = item.value(QStringLiteral("code")).toString();
        const auto message = item.value(QStringLiteral("message")).toString();
        if (!code.isEmpty() && !message.isEmpty())
        {
            diagnostic(result, code, message, item.value(QStringLiteral("path")).toString());
        }
    }
}

QString worker_program()
{
    const auto configured = QString::fromUtf8(DPE_TILED_IMPORTER_WORKER);
    return QDir::isAbsolutePath(configured)
        ? configured
        : QDir{QCoreApplication::applicationDirPath()}.filePath(configured);
}
} // namespace

TileImportService::TileImportService(
    IAssetService& asset_service,
    IdProvider id_provider,
    WorkerRunner worker_runner)
    : asset_service_(asset_service),
      id_provider_(std::move(id_provider)),
      worker_runner_(worker_runner ? std::move(worker_runner) : WorkerRunner{run_worker})
{
}

QString TileImportService::next_id() const
{
    return id_provider_ ? id_provider_()
                        : QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

TileImportResult TileImportService::import_tiled_json(const TileImportRequest& request) const
{
    TileImportResult result;
    if (request.timeout_milliseconds <= 0 || !std::isfinite(request.pixels_per_unit)
        || request.pixels_per_unit <= 0.0)
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-REQUEST"),
            QStringLiteral("Tile import requires positive timeout and pixels-per-unit values."));
        return result;
    }
    if (request.cancellation_requested && request.cancellation_requested())
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-CANCELLED"),
            QStringLiteral("Tile import was cancelled before the worker started."));
        return result;
    }
    QByteArray source_bytes;
    QString error;
    const QFileInfo source_info{request.source_map_path};
    const auto source_path = source_info.canonicalFilePath();
    if (source_path.isEmpty()
        || !read_regular_file(source_path, max_request_source_bytes, source_bytes, error))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-SOURCE"),
            error.isEmpty() ? QStringLiteral("The selected Tiled JSON map is unavailable.") : error,
            request.source_map_path);
        return result;
    }

    result.tilemap_asset_id = next_id();
    result.tileset_asset_id = next_id();
    result.texture_asset_id = next_id();
    result.palette_asset_id = next_id();
    const auto canonical_id = [](const QString& value) {
        const QUuid parsed{value};
        return parsed.isNull() ? QString{}
                               : parsed.toString(QUuid::WithoutBraces).toLower();
    };
    result.tilemap_asset_id = canonical_id(result.tilemap_asset_id);
    result.tileset_asset_id = canonical_id(result.tileset_asset_id);
    result.texture_asset_id = canonical_id(result.texture_asset_id);
    result.palette_asset_id = canonical_id(result.palette_asset_id);
    if (result.tilemap_asset_id.isEmpty() || result.tileset_asset_id.isEmpty()
        || result.texture_asset_id.isEmpty() || result.palette_asset_id.isEmpty()
        || result.tilemap_asset_id == result.tileset_asset_id
        || result.tilemap_asset_id == result.texture_asset_id
        || result.tilemap_asset_id == result.palette_asset_id
        || result.tileset_asset_id == result.texture_asset_id
        || result.tileset_asset_id == result.palette_asset_id
        || result.texture_asset_id == result.palette_asset_id)
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-IDS"),
            QStringLiteral("The editor could not allocate four distinct tile asset identifiers."));
        return result;
    }

    QTemporaryDir operation{QDir::temp().filePath(QStringLiteral("dpe-tile-import-XXXXXX"))};
    if (!operation.isValid())
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-STAGING"),
            QStringLiteral("The editor could not create an isolated tile-import staging directory."));
        return result;
    }
    const auto staging = QDir{operation.path()}.filePath(QStringLiteral("outputs"));
    if (!QDir{}.mkdir(staging))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-STAGING"),
            QStringLiteral("The editor could not create the worker output directory."), staging);
        return result;
    }
    const auto request_path = QDir{operation.path()}.filePath(QStringLiteral("request.json"));
    const auto envelope = QJsonObject{
        {QStringLiteral("format"), QStringLiteral("dpe.tile-import.request")},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("importer"), QStringLiteral("dragonpixel.tiled-json")},
        {QStringLiteral("sourceMap"), QDir::toNativeSeparators(source_path)},
        {QStringLiteral("stagingDirectory"), QDir::toNativeSeparators(staging)},
        {QStringLiteral("name"), request.base_name},
        {QStringLiteral("assetIds"), QJsonObject{
            {QStringLiteral("tilemap"), result.tilemap_asset_id},
            {QStringLiteral("tileset"), result.tileset_asset_id},
            {QStringLiteral("texture"), result.texture_asset_id}}},
        {QStringLiteral("pixelsPerUnit"), request.pixels_per_unit},
        {QStringLiteral("importIsometricAsZAsY"), request.import_isometric_as_z_as_y},
    };
    if (!write_request(request_path, envelope, error))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-REQUEST-WRITE"), error, request_path);
        return result;
    }

    const auto worker = worker_runner_(worker_program(),
        {QStringLiteral("--request"), request_path}, request.timeout_milliseconds,
        request.cancellation_requested);
    switch (worker.status)
    {
    case TileImportWorkerStatus::failed_to_start:
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-WORKER-START"),
            QStringLiteral("The Tiled importer worker could not start: %1").arg(worker.standard_error));
        return result;
    case TileImportWorkerStatus::crashed:
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-WORKER-CRASH"),
            QStringLiteral("The Tiled importer worker crashed: %1").arg(worker.standard_error));
        return result;
    case TileImportWorkerStatus::timed_out:
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-TIMEOUT"),
            QStringLiteral("The Tiled importer worker exceeded its time limit."));
        return result;
    case TileImportWorkerStatus::cancelled:
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-CANCELLED"),
            QStringLiteral("Tile import was cancelled; staged outputs were discarded."));
        return result;
    case TileImportWorkerStatus::completed:
        break;
    }

    const auto result_path = QDir{staging}.filePath(QStringLiteral("result.json"));
    QByteArray result_bytes;
    if (!read_regular_file(result_path, max_result_bytes, result_bytes, error))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-RESULT"),
            QStringLiteral("The importer did not produce a readable result envelope: %1").arg(error),
            result_path);
        return result;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(result_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-RESULT"),
            QStringLiteral("The importer result envelope is not a JSON object."), result_path);
        return result;
    }
    const auto object = document.object();
    append_worker_diagnostics(object.value(QStringLiteral("diagnostics")).toArray(), result);
    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.tile-import.result")
        || (object.value(QStringLiteral("formatVersion")).toInt() != 1
            && object.value(QStringLiteral("formatVersion")).toInt() != 2)
        || object.value(QStringLiteral("importer")).toString() != QStringLiteral("dragonpixel.tiled-json"))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-RESULT-CONTRACT"),
            QStringLiteral("The importer result format, version, or identity is unsupported."));
        return result;
    }
    if (worker.exit_code != 0 || !object.value(QStringLiteral("succeeded")).toBool())
    {
        if (result.diagnostics.isEmpty())
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-WORKER-FAILED"),
                QStringLiteral("The Tiled importer rejected the selected map."));
        }
        return result;
    }

    QString tilemap_output_path;
    QMap<int, QPair<QString, QString>> tileset_outputs;
    QMap<int, QPair<QString, QString>> texture_outputs;
    QStringList returned_ids;
    const auto outputs = object.value(QStringLiteral("outputs")).toArray();
    for (const auto& value : outputs)
    {
        const auto output = value.toObject();
        const auto role = output.value(QStringLiteral("role")).toString();
        const auto name = output.value(QStringLiteral("path")).toString();
        const auto id = canonical_id(output.value(QStringLiteral("assetId")).toString());
        QString output_path;
        if (id.isEmpty() || returned_ids.contains(id)
            || !contained_output(staging, name, output_path))
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"),
                QStringLiteral("The importer returned an unexpected, duplicated, or uncontained output."));
            return result;
        }
        returned_ids.push_back(id);
        if (role == QStringLiteral("tilemap"))
        {
            if (!tilemap_output_path.isEmpty() || id != result.tilemap_asset_id
                || name != QStringLiteral("tilemap.dpetilemap"))
            {
                diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"),
                    QStringLiteral("The importer returned an invalid Tilemap output."));
                return result;
            }
            tilemap_output_path = output_path;
            continue;
        }
        if (role != QStringLiteral("tileset") && role != QStringLiteral("texture"))
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"),
                QStringLiteral("The importer returned an unknown output role."));
            return result;
        }
        const auto index_value = output.value(QStringLiteral("index"));
        if (!index_value.isDouble() || index_value.toDouble() < 0
            || index_value.toDouble() > 255 || index_value.toDouble() != std::floor(index_value.toDouble()))
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"),
                QStringLiteral("A TileSet or texture output has an invalid index."));
            return result;
        }
        const auto index = static_cast<int>(index_value.toDouble());
        const auto expected_name = role == QStringLiteral("tileset")
            ? (outputs.size() == 3 ? QStringLiteral("tileset.dpetileset")
                                   : QStringLiteral("tileset-%1.dpetileset").arg(index))
            : (outputs.size() == 3 ? QStringLiteral("texture.png")
                                   : QStringLiteral("texture-%1.png").arg(index));
        auto& indexed = role == QStringLiteral("tileset") ? tileset_outputs : texture_outputs;
        const auto primary_id = role == QStringLiteral("tileset")
            ? result.tileset_asset_id : result.texture_asset_id;
        if (indexed.contains(index) || name != expected_name || (index == 0 && id != primary_id))
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"),
                QStringLiteral("A TileSet or texture output is duplicated or does not match its assigned identity."));
            return result;
        }
        indexed.insert(index, {id, output_path});
    }
    if (tilemap_output_path.isEmpty() || tileset_outputs.isEmpty()
        || tileset_outputs.size() != texture_outputs.size()
        || outputs.size() != 1 + (2 * tileset_outputs.size()))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"),
            QStringLiteral("The importer must return one Tilemap and matching TileSet/atlas batches."));
        return result;
    }
    for (int index = 0; index < tileset_outputs.size(); ++index)
    {
        if (!tileset_outputs.contains(index) || !texture_outputs.contains(index))
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-CONTRACT"),
                QStringLiteral("TileSet and atlas output indices must be contiguous."));
            return result;
        }
    }

    QByteArray tilemap_bytes;
    QVector<QByteArray> tileset_documents;
    QVector<QByteArray> texture_documents;
    if (!read_regular_file(tilemap_output_path, max_document_bytes, tilemap_bytes, error))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-READ"), error);
        return result;
    }
    for (int index = 0; index < tileset_outputs.size(); ++index)
    {
        QByteArray set_bytes;
        QByteArray texture_bytes;
        if (!read_regular_file(tileset_outputs[index].second, max_document_bytes, set_bytes, error)
            || !read_regular_file(texture_outputs[index].second, max_texture_bytes, texture_bytes, error))
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-OUTPUT-READ"), error);
            return result;
        }
        tileset_documents.push_back(std::move(set_bytes));
        texture_documents.push_back(std::move(texture_bytes));
        result.tileset_asset_ids.push_back(tileset_outputs[index].first);
        result.texture_asset_ids.push_back(texture_outputs[index].first);
    }
    const auto stats = object.value(QStringLiteral("statistics")).toObject();
    const auto count = [](const QJsonObject& values, const QString& key) -> std::optional<qsizetype> {
        const auto value = values.value(key);
        if (!value.isDouble() || value.toDouble() < 0.0
            || value.toDouble() != std::floor(value.toDouble())
            || value.toDouble() > static_cast<double>(std::numeric_limits<qsizetype>::max()))
        {
            return std::nullopt;
        }
        return static_cast<qsizetype>(value.toDouble());
    };
    const auto tile_count = count(stats, QStringLiteral("tiles"));
    const auto layer_count = count(stats, QStringLiteral("layers"));
    const auto cell_count = count(stats, QStringLiteral("cells"));
    if (!tile_count || !layer_count || !cell_count)
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-STATISTICS"),
            QStringLiteral("The importer result has invalid statistics."));
        return result;
    }
    if (*tile_count > 65'536 || *layer_count > 256 || *cell_count > 4 * 1024 * 1024)
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-STATISTICS"),
            QStringLiteral("The importer result exceeds the accepted tile-data limits."));
        return result;
    }
    QByteArray source_after;
    if (!read_regular_file(source_path, max_request_source_bytes, source_after, error)
        || sha256(source_after) != sha256(source_bytes))
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-SOURCE-CHANGED"),
            QStringLiteral("The Tiled map changed while it was being imported; no assets were published."),
            source_path);
        return result;
    }

    const auto palette_uuid = dragonpixel::core::uuid::parse(
        result.palette_asset_id.toStdString());
    std::vector<dragonpixel::tiles::tile_set_document> parsed_sets;
    parsed_sets.reserve(static_cast<std::size_t>(tileset_documents.size()));
    for (const auto& set_bytes : tileset_documents)
    {
        const auto parsed = dragonpixel::tiles::read_tile_set(
            std::string_view{set_bytes.constData(), static_cast<std::size_t>(set_bytes.size())});
        if (!parsed.succeeded())
        {
            diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-PALETTE"),
                QStringLiteral("An imported TileSet could not be converted into a Tile Palette."));
            return result;
        }
        parsed_sets.push_back(*parsed.document);
    }
    if (!palette_uuid)
    {
        diagnostic(result, QStringLiteral("DPE-TILE-IMPORT-PALETTE"),
            QStringLiteral("The imported TileSet could not be converted into a Tile Palette."));
        return result;
    }
    dragonpixel::tiles::tile_palette_document palette;
    palette.asset_id = *palette_uuid;
    palette.name = (request.base_name + QStringLiteral(" Palette")).toStdString();
    std::size_t palette_tile_count{};
    for (const auto& set : parsed_sets)
    {
        palette.tile_set_dependencies.push_back(set.asset_id);
        palette_tile_count += set.tiles.size();
    }
    const auto palette_columns = std::max(1, static_cast<int>(
        std::ceil(std::sqrt(static_cast<double>(palette_tile_count)))));
    std::size_t palette_index{};
    for (const auto& set : parsed_sets)
    {
        for (const auto& tile : set.tiles)
        {
            dragonpixel::tiles::tile_palette_cell palette_cell{};
            palette_cell.u = static_cast<int>(palette_index) % palette_columns;
            palette_cell.v = static_cast<int>(palette_index) / palette_columns;
            palette_cell.tile = {set.asset_id, tile.tile_id};
            palette.cells.push_back(std::move(palette_cell));
            ++palette_index;
        }
    }
    const auto palette_bytes = QByteArray::fromStdString(
        dragonpixel::tiles::write_tile_palette(palette));

    TileAssetPublicationRequest publication{
        request.project_manifest_path,
        request.base_name,
        result.tilemap_asset_id,
        result.tileset_asset_id,
        result.texture_asset_id,
        tilemap_bytes,
        tileset_documents.front(),
        texture_documents.front(),
        sha256(source_bytes),
        request.pixels_per_unit,
    };
    if (tileset_documents.size() > 1)
    {
        for (int index = 0; index < tileset_documents.size(); ++index)
        {
            publication.tilesets.push_back({result.tileset_asset_ids[index],
                tileset_documents[index], QString::number(index + 1)});
            publication.textures.push_back({result.texture_asset_ids[index],
                texture_documents[index], QString::number(index + 1)});
        }
    }
    publication.palette_asset_id = result.palette_asset_id;
    publication.palette_bytes = palette_bytes;
    const auto published = asset_service_.publish_tile_import(publication);
    if (!published.succeeded)
    {
        result.operation_id = published.operation_id;
        result.diagnostics += published.diagnostics;
        return result;
    }
    result.operation_id = published.operation_id;
    result.tile_count = *tile_count;
    result.layer_count = *layer_count;
    result.cell_count = *cell_count;
    for (const auto& path : published.affected_paths)
    {
        if (path.endsWith(QStringLiteral(".dpetilemap"), Qt::CaseInsensitive))
            result.tilemap_path = path;
        else if (path.endsWith(QStringLiteral(".dpetileset"), Qt::CaseInsensitive))
            result.tileset_path = path;
        else if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            result.texture_path = path;
        else if (path.endsWith(QStringLiteral(".dpetilepalette"), Qt::CaseInsensitive))
            result.palette_path = path;
    }
    result.tileset_paths.clear();
    result.texture_paths.clear();
    for (const auto& path : published.affected_paths)
    {
        if (path.endsWith(QStringLiteral(".dpetileset"), Qt::CaseInsensitive))
            result.tileset_paths.push_back(path);
        else if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            result.texture_paths.push_back(path);
    }
    if (!result.tileset_paths.isEmpty()) result.tileset_path = result.tileset_paths.front();
    if (!result.texture_paths.isEmpty()) result.texture_path = result.texture_paths.front();
    result.succeeded = true;
    return result;
}
