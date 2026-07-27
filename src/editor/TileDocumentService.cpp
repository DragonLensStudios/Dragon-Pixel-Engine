#include "TileDocumentService.h"

#include <dragonpixel/serialization/atomic_file.h>

#include <QFile>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <set>

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
}

TileDocumentService::TileDocumentService(QObject* parent) : QObject(parent) {}

void TileDocumentService::clear()
{
    tilemap_.reset();
    tileset_.reset();
    stroke_before_.reset();
    undo_.clear();
    redo_.clear();
    tilemap_path_.clear();
    tileset_path_.clear();
    saved_encoding_.clear();
    error_.clear();
    emit documentChanged();
    emit dirtyChanged(false);
}

bool TileDocumentService::load(const QString& tilemap_path, const QString& tileset_path)
{
    QFile tilemap_file{tilemap_path};
    QFile tileset_file{tileset_path};
    if (!tilemap_file.open(QIODevice::ReadOnly) || !tileset_file.open(QIODevice::ReadOnly))
    {
        error_ = QStringLiteral("Tilemap or TileSet source could not be opened.");
        emit diagnostic(error_);
        return false;
    }
    const auto map_result = dragonpixel::tiles::read_tilemap(tilemap_file.readAll().toStdString());
    const auto set_result = dragonpixel::tiles::read_tile_set(tileset_file.readAll().toStdString());
    if (!map_result.succeeded() || !set_result.succeeded())
    {
        error_ = QStringLiteral("Tile document validation failed: %1 %2")
            .arg(QString::fromStdString(map_result.error), QString::fromStdString(set_result.error));
        emit diagnostic(error_);
        return false;
    }
    if (std::find(map_result.document->tile_set_dependencies.begin(),
            map_result.document->tile_set_dependencies.end(), set_result.document->asset_id)
        == map_result.document->tile_set_dependencies.end())
    {
        error_ = QStringLiteral("The selected TileSet is not a dependency of this tilemap.");
        emit diagnostic(error_);
        return false;
    }
    tilemap_ = *map_result.document;
    tileset_ = *set_result.document;
    tilemap_path_ = tilemap_path;
    tileset_path_ = tileset_path;
    saved_encoding_ = QString::fromStdString(dragonpixel::tiles::write_tilemap(*tilemap_));
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
    const auto encoded = prepare_save();
    if (!encoded)
    {
        return false;
    }
    const auto result = dragonpixel::serialization::save_utf8_atomic(filesystem_path(tilemap_path_), *encoded);
    if (!result.succeeded)
    {
        error_ = QStringLiteral("Atomic tilemap save failed: %1").arg(QString::fromStdString(result.error));
        emit diagnostic(error_);
        return false;
    }
    accept_save(*encoded);
    return true;
}

std::optional<std::string> TileDocumentService::prepare_save()
{
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

void TileDocumentService::accept_save(std::string_view encoded)
{
    saved_encoding_ = QString::fromUtf8(encoded.data(), static_cast<qsizetype>(encoded.size()));
    error_.clear();
    emit dirtyChanged(false);
}

bool TileDocumentService::is_dirty() const noexcept
{
    return tilemap_ && QString::fromStdString(dragonpixel::tiles::write_tilemap(*tilemap_)) != saved_encoding_;
}

const dragonpixel::tiles::tilemap_document* TileDocumentService::tilemap() const noexcept
{
    return tilemap_ ? &*tilemap_ : nullptr;
}

const dragonpixel::tiles::tile_set_document* TileDocumentService::tileset() const noexcept
{
    return tileset_ ? &*tileset_ : nullptr;
}

std::optional<dragonpixel::core::uuid> TileDocumentService::tile_at(int layer_index, int x, int y) const
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
    return cell == chunk->cells.end() ? std::nullopt : std::optional{cell->tile_id};
}

void TileDocumentService::begin_stroke()
{
    if (tilemap_ && !stroke_before_)
    {
        stroke_before_ = *tilemap_;
    }
}

bool TileDocumentService::set_cell(
    dragonpixel::tiles::tilemap_document& document,
    int layer_index,
    int x,
    int y,
    const std::optional<dragonpixel::core::uuid>& tile_id)
{
    if (layer_index < 0 || layer_index >= static_cast<int>(document.layers.size())) return false;
    if (tile_id && (!tileset_ || std::none_of(tileset_->tiles.begin(), tileset_->tiles.end(), [&](const auto& tile) {
            return tile.tile_id == *tile_id;
        })))
    {
        return false;
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
        if (!tile_id) return false;
        chunks.push_back({chunk_x, chunk_y, {}});
        chunk = std::prev(chunks.end());
    }
    auto cell = std::find_if(chunk->cells.begin(), chunk->cells.end(), [&](const auto& value) {
        return value.index == index;
    });
    if (!tile_id)
    {
        if (cell == chunk->cells.end()) return false;
        chunk->cells.erase(cell);
        if (chunk->cells.empty()) chunks.erase(chunk);
        return true;
    }
    if (cell == chunk->cells.end())
    {
        chunk->cells.push_back({index, *tile_id, false, false, 0});
        return true;
    }
    if (cell->tile_id == *tile_id) return false;
    cell->tile_id = *tile_id;
    cell->flip_x = false;
    cell->flip_y = false;
    cell->rotation_quarter_turns = 0;
    return true;
}

void TileDocumentService::publish_change(bool previous_dirty)
{
    emit documentChanged();
    if (previous_dirty != is_dirty()) emit dirtyChanged(is_dirty());
}

bool TileDocumentService::paint_cell(int layer_index, int x, int y, const dragonpixel::core::uuid& tile_id)
{
    if (!tilemap_) return false;
    const auto dirty = is_dirty();
    const auto changed = set_cell(*tilemap_, layer_index, x, y, tile_id);
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
    bool erase)
{
    if (!tilemap_ || !stroke_before_) return false;
    const auto dirty = is_dirty();
    *tilemap_ = *stroke_before_;
    bool changed = false;
    for (int y = std::min(start_y, end_y); y <= std::max(start_y, end_y); ++y)
    {
        for (int x = std::min(start_x, end_x); x <= std::max(start_x, end_x); ++x)
        {
            changed = set_cell(*tilemap_, layer_index, x, y, erase ? std::nullopt : std::optional{tile_id}) || changed;
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
    if (!tilemap_ || limit == 0) return false;
    const auto source = tile_at(layer_index, x, y);
    if (source && *source == tile_id) return false;
    const auto dirty = is_dirty();
    std::deque<std::pair<int, int>> pending{{x, y}};
    std::set<std::pair<int, int>> visited;
    bool changed = false;
    while (!pending.empty() && visited.size() < limit)
    {
        const auto current = pending.front();
        pending.pop_front();
        if (!visited.insert(current).second || tile_at(layer_index, current.first, current.second) != source) continue;
        changed = set_cell(*tilemap_, layer_index, current.first, current.second, tile_id) || changed;
        pending.push_back({current.first + 1, current.second});
        pending.push_back({current.first - 1, current.second});
        pending.push_back({current.first, current.second + 1});
        pending.push_back({current.first, current.second - 1});
        // Filling an unbounded empty plane is intentionally constrained to a
        // deterministic brush-sized neighborhood.
        if (!source && (std::abs(current.first - x) > 31 || std::abs(current.second - y) > 31)) break;
    }
    if (visited.size() >= limit) emit diagnostic(QStringLiteral("Flood fill stopped at the 4096-cell safety limit."));
    if (changed) publish_change(dirty);
    return changed;
}

void TileDocumentService::commit_stroke()
{
    if (!tilemap_ || !stroke_before_) return;
    if (*stroke_before_ != *tilemap_)
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
    *tilemap_ = std::move(*stroke_before_);
    stroke_before_.reset();
    publish_change(dirty);
}

bool TileDocumentService::undo()
{
    if (!tilemap_ || undo_.empty()) return false;
    const auto dirty = is_dirty();
    redo_.push_back(*tilemap_);
    *tilemap_ = std::move(undo_.back());
    undo_.pop_back();
    publish_change(dirty);
    return true;
}

bool TileDocumentService::redo()
{
    if (!tilemap_ || redo_.empty()) return false;
    const auto dirty = is_dirty();
    undo_.push_back(*tilemap_);
    *tilemap_ = std::move(redo_.back());
    redo_.pop_back();
    publish_change(dirty);
    return true;
}
