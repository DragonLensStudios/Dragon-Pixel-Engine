#pragma once

#include "TileDocumentService.h"

#include <QImage>
#include <QHash>
#include <QWidget>

#include <optional>
#include <functional>
#include <vector>

class QActionGroup;
class QComboBox;
class QLabel;
class QListWidget;
class QSlider;
class QToolButton;
class QPolygonF;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;

class TileCanvas final : public QWidget
{
    Q_OBJECT

public:
    enum class Tool
    {
        paint,
        erase,
        rectangle,
        line,
        fill,
        eyedropper,
        select,
        move,
    };

    explicit TileCanvas(TileDocumentService* service, QWidget* parent = nullptr);
    void set_tool(Tool tool) noexcept { tool_ = tool; }
    void set_layer(int layer) noexcept { layer_ = layer; update(); }
    void set_selected_brush(std::optional<TileDocumentService::Brush> brush) { selected_brush_ = brush; }
    void set_atlas(QImage atlas) { atlas_ = std::move(atlas); update(); }
    void set_atlases(QHash<QString, QImage> atlases) { atlases_ = std::move(atlases); update(); }
    void set_brush_provider(std::function<std::optional<TileDocumentService::Brush>(int, int)> provider)
    {
        brush_provider_ = std::move(provider);
    }
    void set_pattern_provider(std::function<std::vector<std::pair<QPoint,
        TileDocumentService::Brush>>(int, int)> provider)
    {
        pattern_provider_ = std::move(provider);
    }
    void set_zoom(double zoom) noexcept;

    [[nodiscard]] Tool tool() const noexcept { return tool_; }
    [[nodiscard]] std::optional<QRect> selection() const;

signals:
    void brushPicked(const QString& tile_set_id, const QString& tile_id,
        bool flip_x, bool flip_y, int rotation_quarter_turns);
    void selectionChanged(int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QPoint cell_at(const QPoint& position) const;
    [[nodiscard]] QRect cell_rect(int x, int y) const;
    [[nodiscard]] QPolygonF cell_polygon(int x, int y) const;
    void apply_at(const QPoint& cell, bool preview_rectangle);

    TileDocumentService* service_{};
    Tool tool_{Tool::paint};
    int layer_{};
    double zoom_{1.0};
    std::optional<TileDocumentService::Brush> selected_brush_;
    std::function<std::optional<TileDocumentService::Brush>(int, int)> brush_provider_;
    std::function<std::vector<std::pair<QPoint, TileDocumentService::Brush>>(int, int)> pattern_provider_;
    QImage atlas_;
    QHash<QString, QImage> atlases_;
    std::optional<QPoint> stroke_start_;
    std::optional<QPoint> selected_cell_;
    std::optional<QPoint> selection_start_;
    std::optional<QPoint> selection_end_;
    std::optional<QPoint> last_cell_;
};

class TilePaletteWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TilePaletteWidget(TileDocumentService* service, QWidget* parent = nullptr);
    [[nodiscard]] bool load_documents(
        const QString& tilemap_path,
        const QString& tileset_path,
        const QString& texture_path = {},
        const QString& palette_path = {});
    [[nodiscard]] bool load_documents(
        const QString& tilemap_path,
        const QStringList& tileset_paths,
        const QStringList& texture_paths,
        const QString& palette_path = {});
    [[nodiscard]] TileCanvas::Tool active_tool() const noexcept;
    [[nodiscard]] int active_layer() const noexcept;
    [[nodiscard]] std::optional<TileDocumentService::Brush> active_brush() const;
    [[nodiscard]] std::optional<TileDocumentService::Brush> active_brush_at(int x, int y) const;
    [[nodiscard]] std::vector<std::pair<QPoint, TileDocumentService::Brush>>
        active_brush_pattern_at(int x, int y) const;
    void select_brush(const TileDocumentService::Brush& brush);

signals:
    void authoringStateChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void update_layer_controls();
    void update_brush();

    TileDocumentService* service_{};
    TileCanvas* canvas_{};
    QListWidget* tiles_{};
    QComboBox* palettes_{};
    QComboBox* brush_behavior_{};
    QComboBox* layers_{};
    QLabel* status_{};
    QSlider* zoom_{};
    QToolButton* layer_visible_{};
    QToolButton* layer_up_{};
    QToolButton* layer_down_{};
    QToolButton* layer_remove_{};
    QToolButton* flip_x_{};
    QToolButton* flip_y_{};
    QToolButton* rotate_{};
    QLineEdit* brush_tint_{};
    QDoubleSpinBox* brush_offset_x_{};
    QDoubleSpinBox* brush_offset_y_{};
    QDoubleSpinBox* brush_rotation_degrees_{};
    QDoubleSpinBox* brush_scale_x_{};
    QDoubleSpinBox* brush_scale_y_{};
    QSpinBox* brush_elevation_{};
    QSpinBox* group_gap_{};
    QSpinBox* group_limit_{};
    QCheckBox* brush_lock_color_{};
    QCheckBox* brush_lock_transform_{};
    QImage atlas_;
    QHash<QString, QImage> atlases_;
    QString texture_path_;
    unsigned brush_rotation_{};
    bool rebuilding_{};
};
