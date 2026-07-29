#pragma once

#include <dragonpixel/tiles/tile_documents.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class TileDocumentService final : public QObject
{
    Q_OBJECT

public:
    struct ResliceRegion final
    {
        dragonpixel::tiles::source_rectangle source;
        int column{};
        int row{};
    };

    struct ResliceRequest final
    {
        dragonpixel::core::uuid tile_set_id;
        dragonpixel::tiles::integer_point cell_size{32, 32};
        dragonpixel::tiles::integer_point margin;
        dragonpixel::tiles::integer_point spacing;
        dragonpixel::tiles::slicing_settings slicing;
        std::vector<ResliceRegion> regions;
        bool grid_collision_for_new_tiles{};
    };

    struct PreparedTileSetSave final
    {
        std::size_t index{};
        QString path;
        std::string encoded;
    };

    struct Brush final
    {
        dragonpixel::core::uuid tile_id;
        bool flip_x{};
        bool flip_y{};
        unsigned rotation_quarter_turns{};
        dragonpixel::core::uuid tile_set_id;
        dragonpixel::tiles::color_rgba tint;
        dragonpixel::tiles::double_point offset;
        double rotation_degrees{};
        dragonpixel::tiles::double_point scale{1.0, 1.0};
        int elevation{};
        bool lock_color{};
        bool lock_transform{};

        friend bool operator==(const Brush& left, const Brush& right)
        {
            return left.tile_id == right.tile_id
                && (left.tile_set_id == right.tile_set_id
                    || left.tile_set_id.is_nil() || right.tile_set_id.is_nil())
                && left.flip_x == right.flip_x && left.flip_y == right.flip_y
                && left.rotation_quarter_turns == right.rotation_quarter_turns
                && left.tint == right.tint && left.offset == right.offset
                && left.rotation_degrees == right.rotation_degrees
                && left.scale == right.scale && left.elevation == right.elevation
                && left.lock_color == right.lock_color
                && left.lock_transform == right.lock_transform;
        }
    };

    explicit TileDocumentService(QObject* parent = nullptr);

    [[nodiscard]] bool load(const QString& tilemap_path, const QString& tileset_path);
    [[nodiscard]] bool load(
        const QString& tilemap_path,
        const QStringList& tileset_paths,
        const QString& palette_path = {});
    void clear();
    [[nodiscard]] bool save();
    [[nodiscard]] std::optional<std::string> prepare_save();
    [[nodiscard]] std::optional<std::string> prepare_palette_save();
    [[nodiscard]] std::optional<std::vector<PreparedTileSetSave>> prepare_tileset_saves();
    void accept_save(std::string_view encoded);
    void accept_palette_save(std::string_view encoded);
    void accept_tileset_saves(const std::vector<PreparedTileSetSave>& saves);
    [[nodiscard]] bool is_palette_dirty() const noexcept;
    [[nodiscard]] bool is_tileset_dirty(std::size_t index) const noexcept;
    [[nodiscard]] bool is_loaded() const noexcept { return tilemap_.has_value() && !tilesets_.empty(); }
    [[nodiscard]] bool is_dirty() const noexcept;
    [[nodiscard]] bool has_active_stroke() const noexcept { return stroke_before_.has_value(); }
    [[nodiscard]] const QString& tilemap_path() const noexcept { return tilemap_path_; }
    [[nodiscard]] const QString& error() const noexcept { return error_; }
    [[nodiscard]] const dragonpixel::tiles::tilemap_document* tilemap() const noexcept;
    [[nodiscard]] const dragonpixel::tiles::tile_set_document* tileset() const noexcept;
    [[nodiscard]] const std::vector<dragonpixel::tiles::tile_set_document>& tilesets() const noexcept
    {
        return tilesets_;
    }
    [[nodiscard]] const dragonpixel::tiles::tile_palette_document* palette() const noexcept;
    [[nodiscard]] const QString& palette_path() const noexcept { return palette_path_; }
    [[nodiscard]] const QStringList& tileset_paths() const noexcept { return tileset_paths_; }
    [[nodiscard]] std::optional<dragonpixel::core::uuid> tile_at(int layer_index, int x, int y) const;
    [[nodiscard]] std::optional<Brush> brush_at(int layer_index, int x, int y) const;

    [[nodiscard]] bool add_layer(
        const QString& name,
        std::optional<dragonpixel::core::uuid> layer_id = std::nullopt);
    [[nodiscard]] bool rename_layer(int layer_index, const QString& name);
    [[nodiscard]] bool set_layer_visible(int layer_index, bool visible);
    [[nodiscard]] bool move_layer(int layer_index, int destination_index);
    [[nodiscard]] bool remove_layer(int layer_index);
    [[nodiscard]] bool update_tile_definition(
        const dragonpixel::core::uuid& tile_set_id,
        const dragonpixel::tiles::tile_definition& tile);
    [[nodiscard]] bool reslice_tileset(const ResliceRequest& request);
    [[nodiscard]] bool update_grid(const dragonpixel::tiles::tile_grid_settings& grid);
    [[nodiscard]] bool update_layer_settings(
        int layer_index,
        const dragonpixel::tiles::tile_layer& layer);

    [[nodiscard]] bool add_palette_cell(int u, int v, const Brush& brush);
    [[nodiscard]] bool remove_palette_cell(int u, int v);
    [[nodiscard]] bool move_palette_cell(int from_u, int from_v, int to_u, int to_v);
    [[nodiscard]] bool delete_selection(int layer_index, int min_x, int min_y, int max_x, int max_y);
    [[nodiscard]] bool move_selection(
        int layer_index, int min_x, int min_y, int max_x, int max_y, int delta_x, int delta_y);
    [[nodiscard]] bool edit_selection(
        int layer_index, int min_x, int min_y, int max_x, int max_y, const Brush& properties);
    [[nodiscard]] bool insert_rows(int layer_index, int before_y, int count = 1);
    [[nodiscard]] bool delete_rows(int layer_index, int first_y, int count = 1);
    [[nodiscard]] bool insert_columns(int layer_index, int before_x, int count = 1);
    [[nodiscard]] bool delete_columns(int layer_index, int first_x, int count = 1);

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
    [[nodiscard]] bool preview_rectangle(
        int layer_index, int start_x, int start_y, int end_x, int end_y,
        const Brush& brush, bool erase = false);
    [[nodiscard]] bool preview_line(
        int layer_index, int start_x, int start_y, int end_x, int end_y,
        const Brush& brush, bool erase = false);
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
    struct WorkspaceState final
    {
        dragonpixel::tiles::tilemap_document tilemap;
        std::vector<dragonpixel::tiles::tile_set_document> tilesets;
        std::optional<dragonpixel::tiles::tile_palette_document> palette;
    };

    [[nodiscard]] WorkspaceState snapshot() const;
    void restore(WorkspaceState state);
    [[nodiscard]] bool set_cell(
        dragonpixel::tiles::tilemap_document& document,
        int layer_index,
        int x,
        int y,
        const std::optional<Brush>& brush);
    [[nodiscard]] bool commit_document_edit(
        WorkspaceState before,
        bool previous_dirty);
    void normalize_layer_order();
    void publish_change(bool previous_dirty);

    std::optional<dragonpixel::tiles::tilemap_document> tilemap_;
    std::vector<dragonpixel::tiles::tile_set_document> tilesets_;
    std::optional<dragonpixel::tiles::tile_palette_document> palette_;
    std::optional<WorkspaceState> stroke_before_;
    std::vector<WorkspaceState> undo_;
    std::vector<WorkspaceState> redo_;
    QString tilemap_path_;
    QString tileset_path_;
    QStringList tileset_paths_;
    QString palette_path_;
    QString saved_encoding_;
    QString saved_palette_encoding_;
    QStringList saved_tileset_encodings_;
    QString error_;
};
