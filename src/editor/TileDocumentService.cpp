#include "TileDocumentService.h"

#include <dragonpixel/serialization/atomic_file.h>

#include <QFile>
#include <QFileInfo>

#include <algorithm>
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
    if (!map_encoded || (palette_ && !palette_encoded))
    {
        return false;
    }
    dragonpixel::serialization::save_result result;
    if (palette_)
    {
        const std::vector<dragonpixel::serialization::utf8_transaction_write> writes{
            {filesystem_path(tilemap_path_), *map_encoded},
            {filesystem_path(palette_path_), *palette_encoded},
        };
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

bool TileDocumentService::is_dirty() const noexcept
{
    const auto map_dirty = tilemap_
        && QString::fromStdString(dragonpixel::tiles::write_tilemap(*tilemap_)) != saved_encoding_;
    return map_dirty || is_palette_dirty();
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
    return {*tilemap_, palette_};
}

void TileDocumentService::restore(WorkspaceState state)
{
    tilemap_ = std::move(state.tilemap);
    palette_ = std::move(state.palette);
}

bool TileDocumentService::commit_document_edit(
    WorkspaceState before,
    bool previous_dirty)
{
    if (!tilemap_ || (before.tilemap == *tilemap_ && before.palette == palette_))
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
    tilemap_->layers.push_back({id, normalized.toStdString(), true,
        static_cast<unsigned>(tilemap_->layers.size()), {}});
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
    return paint_cell(layer_index, x, y, Brush{tile_id});
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
    return preview_rectangle(layer_index, start_x, start_y, end_x, end_y,
        Brush{tile_id, flip_x, flip_y, rotation_quarter_turns}, erase);
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

bool TileDocumentService::flood_fill(
    int layer_index,
    int x,
    int y,
    const dragonpixel::core::uuid& tile_id,
    std::size_t limit)
{
    return flood_fill(layer_index, x, y, Brush{tile_id}, limit);
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
