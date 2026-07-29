#pragma once

#include "AssetService.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

struct TileImportRequest final
{
    QString project_manifest_path;
    QString source_map_path;
    QString base_name;
    double pixels_per_unit{32.0};
    int timeout_milliseconds{30'000};
    std::function<bool()> cancellation_requested;
    bool import_isometric_as_z_as_y{};
};

enum class TileImportWorkerStatus
{
    completed,
    failed_to_start,
    crashed,
    timed_out,
    cancelled,
};

struct TileImportWorkerOutcome final
{
    TileImportWorkerStatus status{TileImportWorkerStatus::failed_to_start};
    int exit_code{-1};
    QString standard_error;
};

struct TileImportResult final
{
    bool succeeded{};
    QString operation_id;
    QString tilemap_asset_id;
    QString tileset_asset_id;
    QString texture_asset_id;
    QString palette_asset_id;
    QString tilemap_path;
    QString tileset_path;
    QString texture_path;
    QString palette_path;
    QStringList tileset_asset_ids;
    QStringList texture_asset_ids;
    QStringList tileset_paths;
    QStringList texture_paths;
    qsizetype tile_count{};
    qsizetype layer_count{};
    qsizetype cell_count{};
    QVector<AssetDiagnostic> diagnostics;
};

class TileImportService final
{
public:
    using IdProvider = std::function<QString()>;
    using WorkerRunner = std::function<TileImportWorkerOutcome(
        const QString& program,
        const QStringList& arguments,
        int timeout_milliseconds,
        const std::function<bool()>& cancellation_requested)>;

    explicit TileImportService(
        IAssetService& asset_service,
        IdProvider id_provider = {},
        WorkerRunner worker_runner = {});

    [[nodiscard]] TileImportResult import_tiled_json(
        const TileImportRequest& request) const;

private:
    [[nodiscard]] QString next_id() const;

    IAssetService& asset_service_;
    IdProvider id_provider_;
    WorkerRunner worker_runner_;
};
