#include "TileDocumentService.h"

#include <dragonpixel/serialization/atomic_file.h>
#include <dragonpixel/tiles/tile_grid.h>

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <set>
#include <tuple>

namespace
{
int floor_div_32(int value)
{
    return value >= 0 ? value / 32 : -(((-value) + 31) / 32);
}

std::filesystem::path filesystem_path(const QString& value)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path{value.toStdWString()};
#else
    return std::filesystem::path{value.toStdString()};
#endif
}

std::vector<std::pair<int, int>> occupied_positions(const dragonpixel::tiles::tile_layer& layer)
{
    std::vector<std::pair<int, int>> result;
    for (const auto& chunk : layer.chunks)
        for (const auto& cell : chunk.cells)
            result.push_back({chunk.x * 32 + static_cast<int>(cell.index % 32U),
                chunk.y * 32 + static_cast<int>(cell.index / 32U)});
    return result;
}

dragonpixel::core::uuid stable_tile_id(
    const dragonpixel::core::uuid& tile_set_id,
    const dragonpixel::tiles::source_rectangle& source)
{
    QByteArray seed{reinterpret_cast<const char*>(tile_set_id.bytes().data()), 16};
    seed.append(':').append(QByteArray::number(source.x))
        .append(':').append(QByteArray::number(source.y))
        .append(':').append(QByteArray::number(source.width))
        .append(':').append(QByteArray::number(source.height));
    const auto digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    std::array<std::uint8_t, 16> bytes{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()),
        bytes.size(), bytes.begin());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x50U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return dragonpixel::core::uuid{bytes};
}

bool reference_matches(
    const dragonpixel::tiles::tile_reference& reference,
    const dragonpixel::core::uuid& tile_set_id,
    const dragonpixel::core::uuid& tile_id)
{
    return reference.tile_id == tile_id
        && (reference.tile_set_id.is_nil() || reference.tile_set_id == tile_set_id);
}
}

TileDocumentService::TileDocumentService(QObject* parent) : QObject(parent) {}

void TileDocumentService::clear()
{
    tilemap_.reset();
    tilesets_.clear();
    palette_.reset();
    stroke_before_.reset();
    undo_.clear();
    redo_.clear();
    tilemap_path_.clear();
    tileset_path_.clear();
    tileset_paths_.clear();
    palette_path_.clear();
    saved_encoding_.clear();
    saved_palette_encoding_.clear();
    saved_tileset_encodings_.clear();
    error_.clear();
    emit documentChanged();
    emit dirtyChanged(false);
}

bool TileDocumentService::load(const QString& tilemap_path, const QString& tileset_path)
{
    return load(tilemap_path, QStringList{tileset_path}, {});
}

bool TileDocumentService::load(
    const QString& tilemap_path,
    const QStringList& tileset_paths,
    const QString& palette_path)
{
    QFile tilemap_file{tilemap_path};
    if (!tilemap_file.open(QIODevice::ReadOnly) || tileset_paths.isEmpty())
    {
        error_ = QStringLiteral("Tilemap or TileSet source could not be opened.");
        emit diagnostic(error_);
        return false;
    }
    const auto map_result = dragonpixel::tiles::read_tilemap(tilemap_file.readAll().toStdString());
    std::vector<dragonpixel::tiles::tile_set_document> loaded_sets;
    for (const auto& path : tileset_paths)
    {
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly))
        {
            error_ = QStringLiteral("TileSet source could not be opened: %1").arg(path);
            emit diagnostic(error_);
            return false;
        }
        const auto parsed = dragonpixel::tiles::read_tile_set(file.readAll().toStdString());
        if (!parsed.succeeded())
        {
            error_ = QStringLiteral("TileSet validation failed: %1")
                .arg(QString::fromStdString(parsed.error));
            emit diagnostic(error_);
            return false;
        }
        const auto duplicate = std::any_of(loaded_sets.begin(), loaded_sets.end(), [&](const auto& set) {
            return set.asset_id == parsed.document->asset_id;
        });
        if (duplicate)
        {
            error_ = QStringLiteral("A TileSet dependency was supplied more than once.");
            emit diagnostic(error_);
            return false;
        }
        loaded_sets.push_back(*parsed.document);
    }
    if (!map_result.succeeded())
    {
        error_ = QStringLiteral("Tilemap validation failed: %1")
            .arg(QString::fromStdString(map_result.error));
        emit diagnostic(error_);
        return false;
    }
    for (const auto& dependency : map_result.document->tile_set_dependencies)
    {
        if (std::none_of(loaded_sets.begin(), loaded_sets.end(), [&](const auto& set) {
                return set.asset_id == dependency;
            }))
        {
            error_ = QStringLiteral("Every Tilemap TileSet dependency must be loaded.");
            emit diagnostic(error_);
            return false;
        }
    }
    std::optional<dragonpixel::tiles::tile_palette_document> loaded_palette;
    if (!palette_path.isEmpty())
    {
        QFile file{palette_path};
        if (!file.open(QIODevice::ReadOnly))
        {
            error_ = QStringLiteral("Tile Palette source could not be opened.");
            emit diagnostic(error_);
            return false;
        }
        const auto parsed = dragonpixel::tiles::read_tile_palette(file.readAll().toStdString());
        if (!parsed.succeeded())
        {
            error_ = QStringLiteral("Tile Palette validation failed: %1")
                .arg(QString::fromStdString(parsed.error));
            emit diagnostic(error_);
            return false;
        }
        for (const auto& dependency : parsed.document->tile_set_dependencies)
        {
            if (std::none_of(loaded_sets.begin(), loaded_sets.end(), [&](const auto& set) {
                    return set.asset_id == dependency;
                }))
            {
                error_ = QStringLiteral("Every Tile Palette TileSet dependency must be loaded.");
                emit diagnostic(error_);
                return false;
            }
        }
        loaded_palette = *parsed.document;
    }
    tilemap_ = *map_result.document;
    tilesets_ = std::move(loaded_sets);
    palette_ = std::move(loaded_palette);
    tilemap_path_ = tilemap_path;
    tileset_paths_ = tileset_paths;
    tileset_path_ = tileset_paths.constFirst();
    palette_path_ = palette_path;
    saved_encoding_ = QString::fromStdString(dragonpixel::tiles::write_tilemap(*tilemap_));
    saved_palette_encoding_ = palette_
        ? QString::fromStdString(dragonpixel::tiles::write_tile_palette(*palette_)) : QString{};
    saved_tileset_encodings_.clear();
    for (const auto& set : tilesets_)
        saved_tileset_encodings_.push_back(
            QString::fromStdString(dragonpixel::tiles::write_tile_set(set)));
    undo_.clear();
    redo_.clear();
    stroke_before_.reset();
    error_.clear();
    emit documentChanged();
    emit dirtyChanged(false);
    return true;
}

bool TileDocumentService::save()
{
    const auto map_encoded = prepare_save();
    const auto palette_encoded = palette_ ? prepare_palette_save() : std::optional<std::string>{};
    const auto tileset_saves = prepare_tileset_saves();
    if (!map_encoded || (palette_ && !palette_encoded) || !tileset_saves)
    {
        return false;
    }
    dragonpixel::serialization::save_result result;
    std::vector<dragonpixel::serialization::utf8_transaction_write> writes{
        {filesystem_path(tilemap_path_), *map_encoded},
    };
    if (palette_)
    {
        writes.push_back({filesystem_path(palette_path_), *palette_encoded});
    }
    for (const auto& save : *tileset_saves)
        writes.push_back({filesystem_path(save.path), save.encoded});
    if (writes.size() > 1)
    {
        result = dragonpixel::serialization::save_utf8_transaction(
            writes, filesystem_path(QFileInfo{tilemap_path_}.absolutePath()));
    }
    else
    {
        result = dragonpixel::serialization::save_utf8_atomic(
            filesystem_path(tilemap_path_), *map_encoded);
    }
    if (!result.succeeded)
    {
        error_ = QStringLiteral("Atomic tilemap save failed: %1").arg(QString::fromStdString(result.error));
        emit diagnostic(error_);
        return false;
    }
    accept_save(*map_encoded);
    if (palette_encoded) accept_palette_save(*palette_encoded);
    accept_tileset_saves(*tileset_saves);
    return true;
}

std::optional<std::string> TileDocumentService::prepare_save()
{
    if (stroke_before_)
    {
        error_ = QStringLiteral("Finish or cancel the active tile stroke before saving.");
        emit diagnostic(error_);
        return std::nullopt;
    }
    if (!tilemap_ || tilemap_path_.isEmpty())
    {
        error_ = QStringLiteral("No loaded tilemap is available to save.");
        emit diagnostic(error_);
        return std::nullopt;
    }
    const auto encoded = dragonpixel::tiles::write_tilemap(*tilemap_);
    const auto parsed = dragonpixel::tiles::read_tilemap(encoded);
    if (!parsed.succeeded())
    {
        error_ = QStringLiteral("Refusing to save an invalid tilemap: %1").arg(QString::fromStdString(parsed.error));
        emit diagnostic(error_);
        return std::nullopt;
    }
    return encoded;
}

std::optional<std::string> TileDocumentService::prepare_palette_save()
{
    if (stroke_before_)
    {
        error_ = QStringLiteral("Finish or cancel the active tile stroke before saving.");
        emit diagnostic(error_);
        return std::nullopt;
    }
    if (!palette_ || palette_path_.isEmpty())
    {
        error_ = QStringLiteral("No loaded Tile Palette is available to save.");
        emit diagnostic(error_);
        return std::nullopt;
    }
    const auto encoded = dragonpixel::tiles::write_tile_palette(*palette_);
    const auto parsed = dragonpixel::tiles::read_tile_palette(encoded);
    if (!parsed.succeeded())
    {
        error_ = QStringLiteral("Refusing to save an invalid Tile Palette: %1")
            .arg(QString::fromStdString(parsed.error));
        emit diagnostic(error_);
        return std::nullopt;
    }
    return encoded;
}

std::optional<std::vector<TileDocumentService::PreparedTileSetSave>>
TileDocumentService::prepare_tileset_saves()
{
    if (stroke_before_)
    {
        error_ = QStringLiteral("Finish or cancel the active tile stroke before saving.");
        emit diagnostic(error_);
        return std::nullopt;
    }
    if (tilesets_.size() != static_cast<std::size_t>(tileset_paths_.size())
        || tilesets_.size() != static_cast<std::size_t>(saved_tileset_encodings_.size()))
    {
        error_ = QStringLiteral("Loaded TileSet paths and savepoints are inconsistent.");
        emit diagnostic(error_);
        return std::nullopt;
    }
    std::vector<PreparedTileSetSave> result;
    for (std::size_t index = 0; index < tilesets_.size(); ++index)
    {
        if (!is_tileset_dirty(index)) continue;
        const auto encoded = dragonpixel::tiles::write_tile_set(tilesets_[index]);
        const auto parsed = dragonpixel::tiles::read_tile_set(encoded);
        if (!parsed.succeeded())
        {
            error_ = QStringLiteral("Refusing to save invalid TileSet %1: %2")
                .arg(tileset_paths_.at(static_cast<qsizetype>(index)),
                    QString::fromStdString(parsed.error));
            emit diagnostic(error_);
            return std::nullopt;
        }
        result.push_back({index, tileset_paths_.at(static_cast<qsizetype>(index)), encoded});
    }
    return result;
}

void TileDocumentService::accept_save(std::string_view encoded)
{
    saved_encoding_ = QString::fromUtf8(encoded.data(), static_cast<qsizetype>(encoded.size()));
    error_.clear();
    emit dirtyChanged(is_dirty());
}

void TileDocumentService::accept_palette_save(std::string_view encoded)
{
    saved_palette_encoding_ = QString::fromUtf8(encoded.data(), static_cast<qsizetype>(encoded.size()));
    error_.clear();
    emit dirtyChanged(is_dirty());
}

void TileDocumentService::accept_tileset_saves(
    const std::vector<PreparedTileSetSave>& saves)
{
    for (const auto& save : saves)
    {
        if (save.index >= static_cast<std::size_t>(saved_tileset_encodings_.size())) continue;
        saved_tileset_encodings_[static_cast<qsizetype>(save.index)] = QString::fromUtf8(
            save.encoded.data(), static_cast<qsizetype>(save.encoded.size()));
    }
    error_.clear();
    emit dirtyChanged(is_dirty());
}

bool TileDocumentService::is_dirty() const noexcept
{
    const auto map_dirty = tilemap_
        && QString::fromStdString(dragonpixel::tiles::write_tilemap(*tilemap_)) != saved_encoding_;
    const auto set_dirty = std::any_of(tilesets_.begin(), tilesets_.end(),
        [this, index = std::size_t{}](const auto& set) mutable {
            const auto current = index++;
            return current >= static_cast<std::size_t>(saved_tileset_encodings_.size())
                || QString::fromStdString(dragonpixel::tiles::write_tile_set(set))
                    != saved_tileset_encodings_.at(static_cast<qsizetype>(current));
        });
    return map_dirty || is_palette_dirty() || set_dirty;
}

bool TileDocumentService::is_palette_dirty() const noexcept
{
    return palette_
        && QString::fromStdString(dragonpixel::tiles::write_tile_palette(*palette_))
            != saved_palette_encoding_;
}

const dragonpixel::tiles::tilemap_document* TileDocumentService::tilemap() const noexcept
{
    return tilemap_ ? &*tilemap_ : nullptr;
}

const dragonpixel::tiles::tile_set_document* TileDocumentService::tileset() const noexcept
{
    return tilesets_.empty() ? nullptr : &tilesets_.front();
}

bool TileDocumentService::is_tileset_dirty(std::size_t index) const noexcept
{
    return index < tilesets_.size()
        && (index >= static_cast<std::size_t>(saved_tileset_encodings_.size())
            || QString::fromStdString(dragonpixel::tiles::write_tile_set(tilesets_[index]))
                != saved_tileset_encodings_.at(static_cast<qsizetype>(index)));
}

const dragonpixel::tiles::tile_palette_document* TileDocumentService::palette() const noexcept
{
    return palette_ ? &*palette_ : nullptr;
}

std::optional<dragonpixel::core::uuid> TileDocumentService::tile_at(int layer_index, int x, int y) const
{
    const auto brush = brush_at(layer_index, x, y);
    return brush ? std::optional{brush->tile_id} : std::nullopt;
}

std::optional<TileDocumentService::Brush> TileDocumentService::brush_at(
    int layer_index,
    int x,
    int y) const
{
    if (!tilemap_ || layer_index < 0 || layer_index >= static_cast<int>(tilemap_->layers.size()))
    {
        return std::nullopt;
    }
    const auto chunk_x = floor_div_32(x);
    const auto chunk_y = floor_div_32(y);
    const auto local_x = x - (chunk_x * 32);
    const auto local_y = y - (chunk_y * 32);
    const auto index = static_cast<unsigned>((local_y * 32) + local_x);
    const auto& layer = tilemap_->layers.at(static_cast<std::size_t>(layer_index));
    const auto chunk = std::find_if(layer.chunks.begin(), layer.chunks.end(), [&](const auto& value) {
        return value.x == chunk_x && value.y == chunk_y;
    });
    if (chunk == layer.chunks.end()) return std::nullopt;
    const auto cell = std::find_if(chunk->cells.begin(), chunk->cells.end(), [&](const auto& value) {
        return value.index == index;
    });
    if (cell == chunk->cells.end()) return std::nullopt;
    auto tile_set_id = cell->tile_set_id;
    if (tile_set_id.is_nil())
    {
        const auto owner = std::find_if(tilesets_.begin(), tilesets_.end(), [&](const auto& set) {
            return std::any_of(set.tiles.begin(), set.tiles.end(), [&](const auto& tile) {
                return tile.tile_id == cell->tile_id;
            });
        });
        if (owner != tilesets_.end()) tile_set_id = owner->asset_id;
    }
    return Brush{cell->tile_id, cell->flip_x, cell->flip_y,
        cell->rotation_quarter_turns, tile_set_id, cell->tint, cell->offset,
        cell->rotation_degrees, cell->scale, cell->elevation,
        cell->lock_color, cell->lock_transform};
}

TileDocumentService::WorkspaceState TileDocumentService::snapshot() const
{
    return {*tilemap_, tilesets_, palette_};
}

void TileDocumentService::restore(WorkspaceState state)
{
    tilemap_ = std::move(state.tilemap);
    tilesets_ = std::move(state.tilesets);
    palette_ = std::move(state.palette);
}

bool TileDocumentService::commit_document_edit(
    WorkspaceState before,
    bool previous_dirty)
{
    if (!tilemap_ || (before.tilemap == *tilemap_ && before.tilesets == tilesets_
        && before.palette == palette_))
    {
        return false;
    }
    undo_.push_back(std::move(before));
    redo_.clear();
    publish_change(previous_dirty);
    return true;
}

void TileDocumentService::normalize_layer_order()
{
    if (!tilemap_) return;
    for (std::size_t index = 0; index < tilemap_->layers.size(); ++index)
    {
        tilemap_->layers[index].order = static_cast<unsigned>(index);
    }
}

bool TileDocumentService::add_layer(
    const QString& name,
    std::optional<dragonpixel::core::uuid> layer_id)
{
    const auto normalized = name.trimmed();
    if (!tilemap_ || stroke_before_ || normalized.isEmpty() || normalized.size() > 128
        || tilemap_->layers.size() >= 256)
    {
        emit diagnostic(QStringLiteral("Layers require a non-empty name of at most 128 characters; Tilemaps support at most 256 layers."));
        return false;
    }
    const auto duplicate_name = std::any_of(
        tilemap_->layers.begin(), tilemap_->layers.end(), [&](const auto& layer) {
            return QString::fromStdString(layer.name).compare(
                normalized, Qt::CaseInsensitive) == 0;
        });
    const auto id = layer_id.value_or(dragonpixel::core::uuid::random_v4());
    const auto duplicate_id = std::any_of(
        tilemap_->layers.begin(), tilemap_->layers.end(), [&](const auto& layer) {
            return layer.layer_id == id;
        });
    if (duplicate_name || duplicate_id)
    {
        emit diagnostic(QStringLiteral("Tilemap layer names and stable IDs must be unique."));
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    dragonpixel::tiles::tile_layer layer{};
    layer.layer_id = id;
    layer.name = normalized.toStdString();
    layer.order = static_cast<unsigned>(tilemap_->layers.size());
    tilemap_->layers.push_back(std::move(layer));
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::rename_layer(int layer_index, const QString& name)
{
    const auto normalized = name.trimmed();
    if (!tilemap_ || stroke_before_ || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())
        || normalized.isEmpty() || normalized.size() > 128)
    {
        return false;
    }
    const auto duplicate = std::any_of(
        tilemap_->layers.begin(), tilemap_->layers.end(), [&](const auto& layer) {
            return &layer != &tilemap_->layers.at(static_cast<std::size_t>(layer_index))
                && QString::fromStdString(layer.name).compare(
                    normalized, Qt::CaseInsensitive) == 0;
        });
    if (duplicate)
    {
        emit diagnostic(QStringLiteral("Tilemap layer names must be unique."));
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    tilemap_->layers.at(static_cast<std::size_t>(layer_index)).name = normalized.toStdString();
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::set_layer_visible(int layer_index, bool visible)
{
    if (!tilemap_ || stroke_before_ || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size()))
    {
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    tilemap_->layers.at(static_cast<std::size_t>(layer_index)).visible = visible;
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::move_layer(int layer_index, int destination_index)
{
    if (!tilemap_ || stroke_before_ || layer_index < 0 || destination_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())
        || destination_index >= static_cast<int>(tilemap_->layers.size())
        || layer_index == destination_index)
    {
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    auto layer = std::move(tilemap_->layers.at(static_cast<std::size_t>(layer_index)));
    tilemap_->layers.erase(tilemap_->layers.begin() + layer_index);
    tilemap_->layers.insert(tilemap_->layers.begin() + destination_index, std::move(layer));
    normalize_layer_order();
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::remove_layer(int layer_index)
{
    if (!tilemap_ || stroke_before_ || tilemap_->layers.size() <= 1 || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size()))
    {
        emit diagnostic(QStringLiteral("A Tilemap must retain at least one layer."));
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    tilemap_->layers.erase(tilemap_->layers.begin() + layer_index);
    normalize_layer_order();
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::update_tile_definition(
    const dragonpixel::core::uuid& tile_set_id,
    const dragonpixel::tiles::tile_definition& tile)
{
    if (stroke_before_ || tile.name.empty() || tile.name.size() > 256) return false;
    const auto set = std::find_if(tilesets_.begin(), tilesets_.end(),
        [&](const auto& candidate) { return candidate.asset_id == tile_set_id; });
    if (set == tilesets_.end()) return false;
    const auto existing = std::find_if(set->tiles.begin(), set->tiles.end(),
        [&](const auto& candidate) { return candidate.tile_id == tile.tile_id; });
    if (existing == set->tiles.end() || *existing == tile) return false;
    auto candidate = *set;
    candidate.tiles[static_cast<std::size_t>(std::distance(set->tiles.begin(), existing))] = tile;
    const auto encoded = dragonpixel::tiles::write_tile_set(candidate);
    const auto parsed = dragonpixel::tiles::read_tile_set(encoded);
    if (!parsed.succeeded())
    {
        error_ = QStringLiteral("Tile definition edit was rejected: %1")
            .arg(QString::fromStdString(parsed.error));
        emit diagnostic(error_);
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    *set = std::move(candidate);
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::reslice_tileset(const ResliceRequest& request)
{
    if (!tilemap_ || stroke_before_ || request.cell_size.x <= 0 || request.cell_size.y <= 0
        || request.regions.empty() || request.regions.size() > 65'536)
    {
        error_ = QStringLiteral("TileSet re-slicing requires a loaded workspace and 1 to 65,536 positive-size regions.");
        emit diagnostic(error_);
        return false;
    }
    const auto set = std::find_if(tilesets_.begin(), tilesets_.end(),
        [&](const auto& candidate) { return candidate.asset_id == request.tile_set_id; });
    if (set == tilesets_.end())
    {
        error_ = QStringLiteral("The TileSet selected for re-slicing is not loaded.");
        emit diagnostic(error_);
        return false;
    }

    std::vector<dragonpixel::core::uuid> next_ids;
    next_ids.reserve(request.regions.size());
    for (const auto& region : request.regions)
    {
        if (region.source.x < 0 || region.source.y < 0
            || region.source.width <= 0 || region.source.height <= 0)
        {
            error_ = QStringLiteral("TileSet re-slicing rejected an invalid sprite rectangle.");
            emit diagnostic(error_);
            return false;
        }
        const auto id = stable_tile_id(request.tile_set_id, region.source);
        if (std::find(next_ids.begin(), next_ids.end(), id) != next_ids.end())
        {
            error_ = QStringLiteral("TileSet re-slicing produced duplicate sprite rectangles.");
            emit diagnostic(error_);
            return false;
        }
        next_ids.push_back(id);
    }
    std::vector<dragonpixel::core::uuid> removed_ids;
    for (const auto& tile : set->tiles)
        if (std::find(next_ids.begin(), next_ids.end(), tile.tile_id) == next_ids.end())
            removed_ids.push_back(tile.tile_id);

    const auto is_removed = [&removed_ids](const dragonpixel::core::uuid& id) {
        return std::find(removed_ids.begin(), removed_ids.end(), id) != removed_ids.end();
    };
    const auto is_referenced = [&](const dragonpixel::core::uuid& removed_id) {
        for (const auto& layer : tilemap_->layers)
            for (const auto& chunk : layer.chunks)
                for (const auto& cell : chunk.cells)
                    if (cell.tile_id == removed_id
                        && (cell.tile_set_id.is_nil() || cell.tile_set_id == request.tile_set_id))
                        return true;
        if (palette_)
            for (const auto& cell : palette_->cells)
                if (reference_matches(cell.tile, request.tile_set_id, removed_id)) return true;
        for (const auto& owner : tilesets_)
            for (const auto& tile : owner.tiles)
            {
                if (owner.asset_id == request.tile_set_id && is_removed(tile.tile_id)) continue;
                if (tile.override_source
                    && reference_matches(*tile.override_source, request.tile_set_id, removed_id))
                    return true;
                for (const auto& override_entry : tile.overrides)
                    if (reference_matches(override_entry.replacement,
                            request.tile_set_id, removed_id)) return true;
                for (const auto& rule : tile.rules)
                {
                    for (const auto& neighbor : rule.neighbors)
                        if (neighbor.tile && reference_matches(*neighbor.tile,
                                request.tile_set_id, removed_id)) return true;
                    for (const auto& output : rule.outputs)
                        if (reference_matches(output.tile,
                                request.tile_set_id, removed_id)) return true;
                }
            }
        return false;
    };
    const auto referenced = std::find_if(removed_ids.begin(), removed_ids.end(), is_referenced);
    if (referenced != removed_ids.end())
    {
        error_ = QStringLiteral(
            "TileSet re-slicing would remove referenced tile %1. Erase or replace its map, palette, rule, and override uses first.")
            .arg(QString::fromStdString(referenced->to_string()));
        emit diagnostic(error_);
        return false;
    }

    auto candidate = *set;
    candidate.cell_size = request.cell_size;
    candidate.margin = request.margin;
    candidate.spacing = request.spacing;
    candidate.slicing = request.slicing;
    std::vector<dragonpixel::tiles::tile_definition> next_tiles;
    next_tiles.reserve(request.regions.size());
    for (std::size_t index = 0; index < request.regions.size(); ++index)
    {
        const auto& region = request.regions[index];
        const auto id = next_ids[index];
        const auto existing = std::find_if(set->tiles.begin(), set->tiles.end(),
            [&](const auto& tile) { return tile.tile_id == id; });
        dragonpixel::tiles::tile_definition tile;
        if (existing != set->tiles.end()) tile = *existing;
        else
        {
            tile.tile_id = id;
            tile.name = QStringLiteral("%1 %2,%3")
                .arg(QString::fromStdString(set->name))
                .arg(region.column).arg(region.row).toStdString();
            tile.texture_asset_id = set->texture_asset_id;
            if (request.grid_collision_for_new_tiles)
            {
                tile.collider_mode = dragonpixel::tiles::tile_collider_mode::grid;
                tile.collision = dragonpixel::tiles::collision_rectangle{0.0, 0.0, 1.0, 1.0};
            }
        }
        tile.source = region.source;
        tile.pivot = request.slicing.pivot;
        next_tiles.push_back(std::move(tile));
    }
    candidate.tiles = std::move(next_tiles);
    const auto parsed = dragonpixel::tiles::read_tile_set(
        dragonpixel::tiles::write_tile_set(candidate));
    if (!parsed.succeeded())
    {
        error_ = QStringLiteral("TileSet re-slicing was rejected: %1")
            .arg(QString::fromStdString(parsed.error));
        emit diagnostic(error_);
        return false;
    }
    if (candidate == *set) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    *set = std::move(candidate);
    error_.clear();
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::update_grid(const dragonpixel::tiles::tile_grid_settings& grid)
{
    if (!tilemap_ || stroke_before_ || tilemap_->grid == grid) return false;
    auto candidate = *tilemap_;
    candidate.grid = grid;
    const auto parsed = dragonpixel::tiles::read_tilemap(
        dragonpixel::tiles::write_tilemap(candidate));
    if (!parsed.succeeded())
    {
        error_ = QStringLiteral("Grid settings were rejected: %1")
            .arg(QString::fromStdString(parsed.error));
        emit diagnostic(error_);
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    tilemap_->grid = grid;
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::update_layer_settings(
    int layer_index,
    const dragonpixel::tiles::tile_layer& layer)
{
    if (!tilemap_ || stroke_before_ || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())
        || layer.layer_id != tilemap_->layers[static_cast<std::size_t>(layer_index)].layer_id
        || layer == tilemap_->layers[static_cast<std::size_t>(layer_index)]) return false;
    auto candidate = *tilemap_;
    candidate.layers[static_cast<std::size_t>(layer_index)] = layer;
    const auto parsed = dragonpixel::tiles::read_tilemap(
        dragonpixel::tiles::write_tilemap(candidate));
    if (!parsed.succeeded())
    {
        error_ = QStringLiteral("Layer renderer settings were rejected: %1")
            .arg(QString::fromStdString(parsed.error));
        emit diagnostic(error_);
        return false;
    }
    auto before = snapshot();
    const auto dirty = is_dirty();
    tilemap_->layers[static_cast<std::size_t>(layer_index)] = layer;
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::add_palette_cell(int u, int v, const Brush& brush)
{
    if (!palette_ || stroke_before_ || brush.rotation_quarter_turns > 3) return false;
    const auto owner = std::find_if(tilesets_.begin(), tilesets_.end(), [&](const auto& set) {
        return (brush.tile_set_id.is_nil() || set.asset_id == brush.tile_set_id)
            && std::any_of(set.tiles.begin(), set.tiles.end(), [&](const auto& tile) {
                return tile.tile_id == brush.tile_id;
            });
    });
    if (owner == tilesets_.end()) return false;
    const auto occupied = std::any_of(palette_->cells.begin(), palette_->cells.end(), [&](const auto& cell) {
        return cell.u == u && cell.v == v;
    });
    if (occupied) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    if (std::find(palette_->tile_set_dependencies.begin(), palette_->tile_set_dependencies.end(),
            owner->asset_id) == palette_->tile_set_dependencies.end())
        palette_->tile_set_dependencies.push_back(owner->asset_id);
    palette_->cells.push_back({u, v, {owner->asset_id, brush.tile_id},
        brush.flip_x, brush.flip_y, brush.rotation_quarter_turns, brush.tint});
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::remove_palette_cell(int u, int v)
{
    if (!palette_ || stroke_before_) return false;
    const auto found = std::find_if(palette_->cells.begin(), palette_->cells.end(), [&](const auto& cell) {
        return cell.u == u && cell.v == v;
    });
    if (found == palette_->cells.end()) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    palette_->cells.erase(found);
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::move_palette_cell(int from_u, int from_v, int to_u, int to_v)
{
    if (!palette_ || stroke_before_ || (from_u == to_u && from_v == to_v)) return false;
    const auto source = std::find_if(palette_->cells.begin(), palette_->cells.end(), [&](const auto& cell) {
        return cell.u == from_u && cell.v == from_v;
    });
    const auto destination = std::find_if(palette_->cells.begin(), palette_->cells.end(), [&](const auto& cell) {
        return cell.u == to_u && cell.v == to_v;
    });
    if (source == palette_->cells.end() || destination != palette_->cells.end()) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    source->u = to_u;
    source->v = to_v;
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::delete_selection(
    int layer_index, int min_x, int min_y, int max_x, int max_y)
{
    if (!tilemap_ || stroke_before_ || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    bool changed = false;
    for (int y = std::min(min_y, max_y); y <= std::max(min_y, max_y); ++y)
        for (int x = std::min(min_x, max_x); x <= std::max(min_x, max_x); ++x)
            changed = set_cell(*tilemap_, layer_index, x, y, std::nullopt) || changed;
    return changed && commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::move_selection(int layer_index, int min_x, int min_y,
    int max_x, int max_y, int delta_x, int delta_y)
{
    if (!tilemap_ || stroke_before_ || (delta_x == 0 && delta_y == 0)
        || layer_index < 0 || layer_index >= static_cast<int>(tilemap_->layers.size())) return false;
    struct selected_cell { int x; int y; Brush brush; };
    std::vector<selected_cell> selected;
    for (int y = std::min(min_y, max_y); y <= std::max(min_y, max_y); ++y)
        for (int x = std::min(min_x, max_x); x <= std::max(min_x, max_x); ++x)
            if (const auto brush = brush_at(layer_index, x, y)) selected.push_back({x, y, *brush});
    if (selected.empty()) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    for (const auto& cell : selected)
        static_cast<void>(set_cell(*tilemap_, layer_index, cell.x, cell.y, std::nullopt));
    for (const auto& cell : selected)
        static_cast<void>(set_cell(*tilemap_, layer_index,
            cell.x + delta_x, cell.y + delta_y, cell.brush));
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::edit_selection(int layer_index, int min_x, int min_y,
    int max_x, int max_y, const Brush& properties)
{
    if (!tilemap_ || stroke_before_ || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    bool changed = false;
    for (int y = std::min(min_y, max_y); y <= std::max(min_y, max_y); ++y)
        for (int x = std::min(min_x, max_x); x <= std::max(min_x, max_x); ++x)
            if (brush_at(layer_index, x, y))
                changed = set_cell(*tilemap_, layer_index, x, y, properties) || changed;
    return changed && commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::insert_rows(int layer_index, int before_y, int count)
{
    if (!tilemap_ || stroke_before_ || count <= 0 || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())) return false;
    const auto positions = occupied_positions(tilemap_->layers[static_cast<std::size_t>(layer_index)]);
    std::vector<std::tuple<int, int, Brush>> moving;
    for (const auto& [x, y] : positions)
        if (y >= before_y)
            if (const auto brush = brush_at(layer_index, x, y)) moving.push_back({x, y, *brush});
    if (moving.empty()) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    for (const auto& [x, y, brush] : moving) static_cast<void>(set_cell(*tilemap_, layer_index, x, y, std::nullopt));
    for (const auto& [x, y, brush] : moving) static_cast<void>(set_cell(*tilemap_, layer_index, x, y + count, brush));
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::delete_rows(int layer_index, int first_y, int count)
{
    if (!tilemap_ || stroke_before_ || count <= 0 || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())) return false;
    const auto positions = occupied_positions(tilemap_->layers[static_cast<std::size_t>(layer_index)]);
    if (positions.empty()) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    std::vector<std::tuple<int, int, Brush>> moving;
    for (const auto& [x, y] : positions)
    {
        if (const auto brush = brush_at(layer_index, x, y)) moving.push_back({x, y, *brush});
        static_cast<void>(set_cell(*tilemap_, layer_index, x, y, std::nullopt));
    }
    for (const auto& [x, y, brush] : moving)
        if (y < first_y || y >= first_y + count)
            static_cast<void>(set_cell(*tilemap_, layer_index, x,
                y >= first_y + count ? y - count : y, brush));
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::insert_columns(int layer_index, int before_x, int count)
{
    if (!tilemap_ || stroke_before_ || count <= 0 || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())) return false;
    const auto positions = occupied_positions(tilemap_->layers[static_cast<std::size_t>(layer_index)]);
    std::vector<std::tuple<int, int, Brush>> moving;
    for (const auto& [x, y] : positions)
        if (x >= before_x)
            if (const auto brush = brush_at(layer_index, x, y)) moving.push_back({x, y, *brush});
    if (moving.empty()) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    for (const auto& [x, y, brush] : moving) static_cast<void>(set_cell(*tilemap_, layer_index, x, y, std::nullopt));
    for (const auto& [x, y, brush] : moving) static_cast<void>(set_cell(*tilemap_, layer_index, x + count, y, brush));
    return commit_document_edit(std::move(before), dirty);
}

bool TileDocumentService::delete_columns(int layer_index, int first_x, int count)
{
    if (!tilemap_ || stroke_before_ || count <= 0 || layer_index < 0
        || layer_index >= static_cast<int>(tilemap_->layers.size())) return false;
    const auto positions = occupied_positions(tilemap_->layers[static_cast<std::size_t>(layer_index)]);
    if (positions.empty()) return false;
    auto before = snapshot();
    const auto dirty = is_dirty();
    std::vector<std::tuple<int, int, Brush>> moving;
    for (const auto& [x, y] : positions)
    {
        if (const auto brush = brush_at(layer_index, x, y)) moving.push_back({x, y, *brush});
        static_cast<void>(set_cell(*tilemap_, layer_index, x, y, std::nullopt));
    }
    for (const auto& [x, y, brush] : moving)
        if (x < first_x || x >= first_x + count)
            static_cast<void>(set_cell(*tilemap_, layer_index,
                x >= first_x + count ? x - count : x, y, brush));
    return commit_document_edit(std::move(before), dirty);
}

void TileDocumentService::begin_stroke()
{
    if (tilemap_ && !stroke_before_)
    {
        stroke_before_ = snapshot();
    }
}

bool TileDocumentService::set_cell(
    dragonpixel::tiles::tilemap_document& document,
    int layer_index,
    int x,
    int y,
    const std::optional<Brush>& brush)
{
    if (layer_index < 0 || layer_index >= static_cast<int>(document.layers.size())) return false;
    dragonpixel::core::uuid resolved_tile_set_id;
    if (brush)
    {
        if (brush->rotation_quarter_turns > 3 || !std::isfinite(brush->rotation_degrees)
            || !std::isfinite(brush->scale.x) || !std::isfinite(brush->scale.y)
            || !std::isfinite(brush->offset.x) || !std::isfinite(brush->offset.y))
            return false;
        const auto owner = std::find_if(tilesets_.begin(), tilesets_.end(), [&](const auto& set) {
            return (brush->tile_set_id.is_nil() || set.asset_id == brush->tile_set_id)
                && std::any_of(set.tiles.begin(), set.tiles.end(), [&](const auto& tile) {
                    return tile.tile_id == brush->tile_id;
                });
        });
        if (owner == tilesets_.end()) return false;
        resolved_tile_set_id = owner->asset_id;
    }
    const auto chunk_x = floor_div_32(x);
    const auto chunk_y = floor_div_32(y);
    const auto local_x = x - (chunk_x * 32);
    const auto local_y = y - (chunk_y * 32);
    const auto index = static_cast<unsigned>((local_y * 32) + local_x);
    auto& chunks = document.layers.at(static_cast<std::size_t>(layer_index)).chunks;
    auto chunk = std::find_if(chunks.begin(), chunks.end(), [&](const auto& value) {
        return value.x == chunk_x && value.y == chunk_y;
    });
    if (chunk == chunks.end())
    {
        if (!brush) return false;
        chunks.push_back({chunk_x, chunk_y, {}});
        chunk = std::prev(chunks.end());
    }
    auto cell = std::find_if(chunk->cells.begin(), chunk->cells.end(), [&](const auto& value) {
        return value.index == index;
    });
    if (!brush)
    {
        if (cell == chunk->cells.end()) return false;
        chunk->cells.erase(cell);
        if (chunk->cells.empty()) chunks.erase(chunk);
        return true;
    }
    if (cell == chunk->cells.end())
    {
        dragonpixel::tiles::tile_cell created;
        created.index = index;
        created.tile_id = brush->tile_id;
        created.flip_x = brush->flip_x;
        created.flip_y = brush->flip_y;
        created.rotation_quarter_turns = brush->rotation_quarter_turns;
        created.tile_set_id = resolved_tile_set_id;
        created.tint = brush->tint;
        created.offset = brush->offset;
        created.rotation_degrees = brush->rotation_degrees;
        created.scale = brush->scale;
        created.elevation = brush->elevation;
        created.lock_color = brush->lock_color;
        created.lock_transform = brush->lock_transform;
        chunk->cells.push_back(std::move(created));
        return true;
    }
    if (cell->tile_id == brush->tile_id && cell->flip_x == brush->flip_x
        && cell->flip_y == brush->flip_y
        && cell->rotation_quarter_turns == brush->rotation_quarter_turns
        && cell->tile_set_id == resolved_tile_set_id && cell->tint == brush->tint
        && cell->offset == brush->offset && cell->rotation_degrees == brush->rotation_degrees
        && cell->scale == brush->scale && cell->elevation == brush->elevation
        && cell->lock_color == brush->lock_color
        && cell->lock_transform == brush->lock_transform)
    {
        return false;
    }
    cell->tile_id = brush->tile_id;
    cell->flip_x = brush->flip_x;
    cell->flip_y = brush->flip_y;
    cell->rotation_quarter_turns = brush->rotation_quarter_turns;
    cell->tile_set_id = resolved_tile_set_id;
    cell->tint = brush->tint;
    cell->offset = brush->offset;
    cell->rotation_degrees = brush->rotation_degrees;
    cell->scale = brush->scale;
    cell->elevation = brush->elevation;
    cell->lock_color = brush->lock_color;
    cell->lock_transform = brush->lock_transform;
    return true;
}

void TileDocumentService::publish_change(bool previous_dirty)
{
    emit documentChanged();
    if (previous_dirty != is_dirty()) emit dirtyChanged(is_dirty());
}

bool TileDocumentService::paint_cell(int layer_index, int x, int y, const dragonpixel::core::uuid& tile_id)
{
    Brush brush;
    brush.tile_id = tile_id;
    return paint_cell(layer_index, x, y, brush);
}

bool TileDocumentService::paint_cell(int layer_index, int x, int y, const Brush& brush)
{
    if (!tilemap_) return false;
    const auto dirty = is_dirty();
    const auto changed = set_cell(*tilemap_, layer_index, x, y, brush);
    if (changed) publish_change(dirty);
    return changed;
}

bool TileDocumentService::erase_cell(int layer_index, int x, int y)
{
    if (!tilemap_) return false;
    const auto dirty = is_dirty();
    const auto changed = set_cell(*tilemap_, layer_index, x, y, std::nullopt);
    if (changed) publish_change(dirty);
    return changed;
}

bool TileDocumentService::preview_rectangle(
    int layer_index,
    int start_x,
    int start_y,
    int end_x,
    int end_y,
    const dragonpixel::core::uuid& tile_id,
    bool erase,
    bool flip_x,
    bool flip_y,
    unsigned rotation_quarter_turns)
{
    Brush brush;
    brush.tile_id = tile_id;
    brush.flip_x = flip_x;
    brush.flip_y = flip_y;
    brush.rotation_quarter_turns = rotation_quarter_turns;
    return preview_rectangle(layer_index, start_x, start_y, end_x, end_y, brush, erase);
}

bool TileDocumentService::preview_rectangle(
    int layer_index, int start_x, int start_y, int end_x, int end_y,
    const Brush& brush, bool erase)
{
    if (!tilemap_ || !stroke_before_) return false;
    const auto dirty = is_dirty();
    *tilemap_ = stroke_before_->tilemap;
    bool changed = false;
    for (int y = std::min(start_y, end_y); y <= std::max(start_y, end_y); ++y)
    {
        for (int x = std::min(start_x, end_x); x <= std::max(start_x, end_x); ++x)
        {
            changed = set_cell(*tilemap_, layer_index, x, y,
                erase ? std::nullopt
                      : std::optional{brush}) || changed;
        }
    }
    publish_change(dirty);
    return changed;
}

bool TileDocumentService::preview_line(
    int layer_index, int start_x, int start_y, int end_x, int end_y,
    const Brush& brush, bool erase)
{
    if (!tilemap_ || !stroke_before_) return false;
    const auto dirty = is_dirty();
    *tilemap_ = stroke_before_->tilemap;
    const auto points = dragonpixel::tiles::grid_line(tilemap_->grid.layout,
        {start_x, start_y}, {end_x, end_y});
    if (points.size() > 4096U)
    {
        emit diagnostic(QStringLiteral("Line Brush stopped at the 4096-cell safety limit."));
        return false;
    }
    bool changed = false;
    for (const auto point : points)
        changed = set_cell(*tilemap_, layer_index, point.x, point.y,
            erase ? std::nullopt : std::optional{brush}) || changed;
    publish_change(dirty);
    return changed;
}

bool TileDocumentService::flood_fill(
    int layer_index,
    int x,
    int y,
    const dragonpixel::core::uuid& tile_id,
    std::size_t limit)
{
    Brush brush;
    brush.tile_id = tile_id;
    return flood_fill(layer_index, x, y, brush, limit);
}

bool TileDocumentService::flood_fill(
    int layer_index,
    int x,
    int y,
    const Brush& brush,
    std::size_t limit)
{
    if (!tilemap_ || limit == 0) return false;
    const auto source_brush = brush_at(layer_index, x, y);
    if (source_brush && *source_brush == brush) return false;
    const auto dirty = is_dirty();
    std::deque<std::pair<int, int>> pending{{x, y}};
    std::set<std::pair<int, int>> visited;
    bool changed = false;
    while (!pending.empty() && visited.size() < limit)
    {
        const auto current = pending.front();
        pending.pop_front();
        if (!visited.insert(current).second
            || brush_at(layer_index, current.first, current.second) != source_brush)
        {
            continue;
        }
        changed = set_cell(*tilemap_, layer_index, current.first, current.second, brush) || changed;
        pending.push_back({current.first + 1, current.second});
        pending.push_back({current.first - 1, current.second});
        pending.push_back({current.first, current.second + 1});
        pending.push_back({current.first, current.second - 1});
        // Filling an unbounded empty plane is intentionally constrained to a
        // deterministic brush-sized neighborhood.
        if (!source_brush
            && (std::abs(current.first - x) > 31
                || std::abs(current.second - y) > 31))
        {
            break;
        }
    }
    if (visited.size() >= limit) emit diagnostic(QStringLiteral("Flood fill stopped at the 4096-cell safety limit."));
    if (changed) publish_change(dirty);
    return changed;
}

void TileDocumentService::commit_stroke()
{
    if (!tilemap_ || !stroke_before_) return;
    if (stroke_before_->tilemap != *tilemap_ || stroke_before_->palette != palette_)
    {
        undo_.push_back(std::move(*stroke_before_));
        redo_.clear();
    }
    stroke_before_.reset();
}

void TileDocumentService::cancel_stroke()
{
    if (!tilemap_ || !stroke_before_) return;
    const auto dirty = is_dirty();
    restore(std::move(*stroke_before_));
    stroke_before_.reset();
    publish_change(dirty);
}

bool TileDocumentService::undo()
{
    if (!tilemap_ || undo_.empty()) return false;
    const auto dirty = is_dirty();
    redo_.push_back(snapshot());
    restore(std::move(undo_.back()));
    undo_.pop_back();
    publish_change(dirty);
    return true;
}

bool TileDocumentService::redo()
{
    if (!tilemap_ || redo_.empty()) return false;
    const auto dirty = is_dirty();
    undo_.push_back(snapshot());
    restore(std::move(redo_.back()));
    redo_.pop_back();
    publish_change(dirty);
    return true;
}
