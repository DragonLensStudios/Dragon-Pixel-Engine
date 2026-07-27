#pragma once

#include "TileDocumentService.h"

#include <QWidget>

#include <optional>

class QActionGroup;
class QComboBox;
class QLabel;
class QListWidget;
class QSlider;

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
    void set_selected_tile(std::optional<dragonpixel::core::uuid> tile) { selected_tile_ = tile; }
    void set_zoom(double zoom) noexcept;

signals:
    void tilePicked(const QString& tile_id);
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
    std::optional<dragonpixel::core::uuid> selected_tile_;
    std::optional<QPoint> stroke_start_;
    std::optional<QPoint> selected_cell_;
    std::optional<QPoint> last_cell_;
};

class TilePaletteWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TilePaletteWidget(TileDocumentService* service, QWidget* parent = nullptr);
    [[nodiscard]] bool load_documents(const QString& tilemap_path, const QString& tileset_path);

private:
    void rebuild();

    TileDocumentService* service_{};
    TileCanvas* canvas_{};
    QListWidget* tiles_{};
    QComboBox* layers_{};
    QLabel* status_{};
    QSlider* zoom_{};
};
