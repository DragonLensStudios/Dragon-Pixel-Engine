#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

enum class AssetImportOwnership
{
    copy_into_project,
    link_external_read_only,
};

struct AssetDiagnostic final
{
    QString code;
    QString message;
    QString path;
};

struct AssetImportRequest final
{
    QString project_manifest_path;
    QStringList source_paths;
    QString destination_folder{QStringLiteral("Assets")};
    AssetImportOwnership ownership{AssetImportOwnership::copy_into_project};
};

struct TileAssetPublicationRequest final
{
    QString project_manifest_path;
    QString base_name;
    QString tilemap_asset_id;
    QString tileset_asset_id;
    QString texture_asset_id;
    QByteArray tilemap_bytes;
    QByteArray tileset_bytes;
    QByteArray texture_bytes;
    QString source_map_hash;
    double pixels_per_unit{32.0};
    QString palette_asset_id;
    QByteArray palette_bytes;
};

struct TilemapCreationRequest final
{
    QString project_manifest_path;
    QString tileset_asset_id;
    QString name;
    bool create_palette{true};
};

struct AssetOperationResult final
{
    bool succeeded{};
    QString operation_id;
    QStringList asset_ids;
    QStringList metadata_paths;
    QStringList affected_paths;
    QVector<AssetDiagnostic> diagnostics;
};

struct AssetDependencyImpact final
{
    QString asset_id;
    QStringList dependent_ids;
    QStringList dependent_paths;
};

struct RuntimeAssetBindingResult final
{
    bool succeeded{};
    QString asset_id;
    QString media_type;
    QString content_hash;
    QByteArray immutable_bytes;
    QVector<AssetDiagnostic> diagnostics;
};

class IAssetService
{
public:
    virtual ~IAssetService() = default;

    [[nodiscard]] virtual AssetOperationResult import_files(
        const AssetImportRequest& request) const = 0;
    [[nodiscard]] virtual AssetOperationResult publish_tile_import(
        const TileAssetPublicationRequest& request) const = 0;
    [[nodiscard]] virtual AssetOperationResult create_tilemap(
        const TilemapCreationRequest& request) const = 0;
    [[nodiscard]] virtual AssetOperationResult create_folder(
        const QString& project_manifest_path,
        const QString& project_relative_folder) const = 0;
    [[nodiscard]] virtual AssetOperationResult rename_asset(
        const QString& project_manifest_path,
        const QString& asset_id,
        const QString& new_base_name) const = 0;
    [[nodiscard]] virtual AssetOperationResult move_asset(
        const QString& project_manifest_path,
        const QString& asset_id,
        const QString& destination_folder) const = 0;
    [[nodiscard]] virtual AssetOperationResult move_folder(
        const QString& project_manifest_path,
        const QString& source_folder,
        const QString& destination_folder) const = 0;
    [[nodiscard]] virtual AssetOperationResult duplicate_asset(
        const QString& project_manifest_path,
        const QString& asset_id,
        const QString& destination_folder = {}) const = 0;
    [[nodiscard]] virtual AssetDependencyImpact dependency_impact(
        const QString& project_manifest_path,
        const QString& asset_id) const = 0;
    [[nodiscard]] virtual AssetOperationResult trash_asset(
        const QString& project_manifest_path,
        const QString& asset_id) const = 0;
    [[nodiscard]] virtual AssetOperationResult restore_trash(
        const QString& project_manifest_path,
        const QString& operation_id) const = 0;
    [[nodiscard]] virtual RuntimeAssetBindingResult runtime_binding(
        const QString& project_manifest_path,
        const QString& asset_id) const = 0;
};

class AssetService final : public IAssetService
{
public:
    using IdProvider = std::function<QString()>;

    explicit AssetService(IdProvider id_provider = {});

    [[nodiscard]] AssetOperationResult import_files(
        const AssetImportRequest& request) const override;
    [[nodiscard]] AssetOperationResult publish_tile_import(
        const TileAssetPublicationRequest& request) const override;
    [[nodiscard]] AssetOperationResult create_tilemap(
        const TilemapCreationRequest& request) const override;
    [[nodiscard]] AssetOperationResult create_folder(
        const QString& project_manifest_path,
        const QString& project_relative_folder) const override;
    [[nodiscard]] AssetOperationResult rename_asset(
        const QString& project_manifest_path,
        const QString& asset_id,
        const QString& new_base_name) const override;
    [[nodiscard]] AssetOperationResult move_asset(
        const QString& project_manifest_path,
        const QString& asset_id,
        const QString& destination_folder) const override;
    [[nodiscard]] AssetOperationResult move_folder(
        const QString& project_manifest_path,
        const QString& source_folder,
        const QString& destination_folder) const override;
    [[nodiscard]] AssetOperationResult duplicate_asset(
        const QString& project_manifest_path,
        const QString& asset_id,
        const QString& destination_folder = {}) const override;
    [[nodiscard]] AssetDependencyImpact dependency_impact(
        const QString& project_manifest_path,
        const QString& asset_id) const override;
    [[nodiscard]] AssetOperationResult trash_asset(
        const QString& project_manifest_path,
        const QString& asset_id) const override;
    [[nodiscard]] AssetOperationResult restore_trash(
        const QString& project_manifest_path,
        const QString& operation_id) const override;
    [[nodiscard]] RuntimeAssetBindingResult runtime_binding(
        const QString& project_manifest_path,
        const QString& asset_id) const override;

private:
    [[nodiscard]] QString next_id() const;

    IdProvider id_provider_;
};
