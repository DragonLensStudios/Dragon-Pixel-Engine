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
    return cell == chunk->cells.end()
        ? std::nullopt
        : std::optional{Brush{cell->tile_id, cell->flip_x, cell->flip_y,
              cell->rotation_quarter_turns}};
}

bool TileDocumentService::commit_document_edit(
    dragonpixel::tiles::tilemap_document before,
    bool previous_dirty)
{
    if (!tilemap_ || before == *tilemap_)
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
    auto before = *tilemap_;
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
    auto before = *tilemap_;
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
    auto before = *tilemap_;
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
    auto before = *tilemap_;
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
    auto before = *tilemap_;
    const auto dirty = is_dirty();
    tilemap_->layers.erase(tilemap_->layers.begin() + layer_index);
    normalize_layer_order();
    return commit_document_edit(std::move(before), dirty);
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
    const std::optional<Brush>& brush)
{
    if (layer_index < 0 || layer_index >= static_cast<int>(document.layers.size())) return false;
    if (brush && (brush->rotation_quarter_turns > 3 || !tileset_
        || std::none_of(tileset_->tiles.begin(), tileset_->tiles.end(), [&](const auto& tile) {
            return tile.tile_id == brush->tile_id;
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
        chunk->cells.push_back({index, brush->tile_id, brush->flip_x,
            brush->flip_y, brush->rotation_quarter_turns});
        return true;
    }
    if (cell->tile_id == brush->tile_id && cell->flip_x == brush->flip_x
        && cell->flip_y == brush->flip_y
        && cell->rotation_quarter_turns == brush->rotation_quarter_turns)
    {
        return false;
    }
    cell->tile_id = brush->tile_id;
    cell->flip_x = brush->flip_x;
    cell->flip_y = brush->flip_y;
    cell->rotation_quarter_turns = brush->rotation_quarter_turns;
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
    if (!tilemap_ || !stroke_before_) return false;
    const auto dirty = is_dirty();
    *tilemap_ = *stroke_before_;
    bool changed = false;
    for (int y = std::min(start_y, end_y); y <= std::max(start_y, end_y); ++y)
    {
        for (int x = std::min(start_x, end_x); x <= std::max(start_x, end_x); ++x)
        {
            changed = set_cell(*tilemap_, layer_index, x, y,
                erase ? std::nullopt
                      : std::optional{Brush{tile_id, flip_x, flip_y,
                            rotation_quarter_turns}}) || changed;
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
