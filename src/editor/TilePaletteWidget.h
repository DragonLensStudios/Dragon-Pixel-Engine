#pragma once

#include "TileDocumentService.h"

#include <QImage>
#include <QWidget>

#include <optional>

class QActionGroup;
class QComboBox;
class QLabel;
class QListWidget;
class QSlider;
class QToolButton;

class TileCanvas final : public QWidget
{
    Q_OBJECT

public:
    enum class Tool
    {
        paint,
        erase,
        rectangle,
        fill,
        eyedropper,
        select,
    };

    explicit TileCanvas(TileDocumentService* service, QWidget* parent = nullptr);
    void set_tool(Tool tool) noexcept { tool_ = tool; }
    void set_layer(int layer) noexcept { layer_ = layer; update(); }
    void set_selected_brush(std::optional<TileDocumentService::Brush> brush) { selected_brush_ = brush; }
    void set_atlas(QImage atlas) { atlas_ = std::move(atlas); update(); }
    void set_zoom(double zoom) noexcept;

    [[nodiscard]] Tool tool() const noexcept { return tool_; }

signals:
    void brushPicked(const QString& tile_id, bool flip_x, bool flip_y, int rotation_quarter_turns);
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
    void apply_at(const QPoint& cell, bool preview_rectangle);

    TileDocumentService* service_{};
    Tool tool_{Tool::paint};
    int layer_{};
    double zoom_{1.0};
    std::optional<TileDocumentService::Brush> selected_brush_;
    QImage atlas_;
    std::optional<QPoint> stroke_start_;
    std::optional<QPoint> selected_cell_;
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
        const QString& texture_path = {});
    [[nodiscard]] TileCanvas::Tool active_tool() const noexcept;
    [[nodiscard]] int active_layer() const noexcept;
    [[nodiscard]] std::optional<TileDocumentService::Brush> active_brush() const;
    void select_brush(const TileDocumentService::Brush& brush);

signals:
    void authoringStateChanged();

private:
    void rebuild();
    void update_layer_controls();
    void update_brush();

    TileDocumentService* service_{};
    TileCanvas* canvas_{};
    QListWidget* tiles_{};
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
    QImage atlas_;
    QString texture_path_;
    unsigned brush_rotation_{};
    bool rebuilding_{};
};
