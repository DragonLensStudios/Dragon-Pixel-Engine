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
    struct Brush final
    {
        dragonpixel::core::uuid tile_id;
        bool flip_x{};
        bool flip_y{};
        unsigned rotation_quarter_turns{};

        friend bool operator==(const Brush&, const Brush&) = default;
    };

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
    [[nodiscard]] std::optional<Brush> brush_at(int layer_index, int x, int y) const;

    [[nodiscard]] bool add_layer(
        const QString& name,
        std::optional<dragonpixel::core::uuid> layer_id = std::nullopt);
    [[nodiscard]] bool rename_layer(int layer_index, const QString& name);
    [[nodiscard]] bool set_layer_visible(int layer_index, bool visible);
    [[nodiscard]] bool move_layer(int layer_index, int destination_index);
    [[nodiscard]] bool remove_layer(int layer_index);

    void begin_stroke();
    [[nodiscard]] bool paint_cell(int layer_index, int x, int y, const dragonpixel::core::uuid& tile_id);
    [[nodiscard]] bool paint_cell(int layer_index, int x, int y, const Brush& brush);
    [[nodiscard]] bool erase_cell(int layer_index, int x, int y);
    [[nodiscard]] bool preview_rectangle(
        int layer_index,
        int start_x,
        int start_y,
        int end_x,
        int end_y,
        const dragonpixel::core::uuid& tile_id,
        bool erase,
        bool flip_x = false,
        bool flip_y = false,
        unsigned rotation_quarter_turns = 0);
    [[nodiscard]] bool flood_fill(
        int layer_index,
        int x,
        int y,
        const dragonpixel::core::uuid& tile_id,
        std::size_t limit = 4096);
    [[nodiscard]] bool flood_fill(
        int layer_index,
        int x,
        int y,
        const Brush& brush,
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
        const std::optional<Brush>& brush);
    [[nodiscard]] bool commit_document_edit(
        dragonpixel::tiles::tilemap_document before,
        bool previous_dirty);
    void normalize_layer_order();
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
