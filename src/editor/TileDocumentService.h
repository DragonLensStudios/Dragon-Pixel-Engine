#pragma once

#include <dragonpixel/tiles/tile_documents.h>

#include <QObject>
#include <QString>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class TileDocumentService final : public QObject
{
    Q_OBJECT

public:
    explicit TileDocumentService(QObject* parent = nullptr);

    [[nodiscard]] bool load(const QString& tilemap_path, const QString& tileset_path);
    void clear();
    [[nodiscard]] bool save();
    [[nodiscard]] std::optional<std::string> prepare_save();
    void accept_save(std::string_view encoded);
    [[nodiscard]] bool is_loaded() const noexcept { return tilemap_.has_value() && tileset_.has_value(); }
    [[nodiscard]] bool is_dirty() const noexcept;
    [[nodiscard]] const QString& tilemap_path() const noexcept { return tilemap_path_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }
    [[nodiscard]] const dragonpixel::tiles::tilemap_document* tilemap() const noexcept;
    [[nodiscard]] const dragonpixel::tiles::tile_set_document* tileset() const noexcept;
    [[nodiscard]] std::optional<dragonpixel::core::uuid> tile_at(int layer_index, int x, int y) const;

    void begin_stroke();
    [[nodiscard]] bool paint_cell(int layer_index, int x, int y, const dragonpixel::core::uuid& tile_id);
    [[nodiscard]] bool erase_cell(int layer_index, int x, int y);
    [[nodiscard]] bool preview_rectangle(
        int layer_index,
        int start_x,
        int start_y,
        int end_x,
        int end_y,
        const dragonpixel::core::uuid& tile_id,
        bool erase);
    [[nodiscard]] bool flood_fill(
        int layer_index,
        int x,
        int y,
        const dragonpixel::core::uuid& tile_id,
        std::size_t limit = 4096);
    void commit_stroke();
    void cancel_stroke();
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

signals:
    void documentChanged();
    void dirtyChanged(bool dirty);
    void diagnostic(const QString& message);

private:
    [[nodiscard]] bool set_cell(
        dragonpixel::tiles::tilemap_document& document,
        int layer_index,
        int x,
        int y,
        const std::optional<dragonpixel::core::uuid>& tile_id);
    void publish_change(bool previous_dirty);

    std::optional<dragonpixel::tiles::tilemap_document> tilemap_;
    std::optional<dragonpixel::tiles::tile_set_document> tileset_;
    std::optional<dragonpixel::tiles::tilemap_document> stroke_before_;
    std::vector<dragonpixel::tiles::tilemap_document> undo_;
    std::vector<dragonpixel::tiles::tilemap_document> redo_;
    QString tilemap_path_;
    QString tileset_path_;
    QString saved_encoding_;
    QString error_;
};
