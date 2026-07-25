#pragma once

#include <QImage>
#include <QPoint>
#include <QQuaternion>
#include <QTimer>
#include <QVector3D>
#include <QWidget>

#include <optional>

class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

class AuthoringViewport final : public QWidget
{
    Q_OBJECT

public:
    enum class ViewMode
    {
        two_d,
        three_d,
    };

    enum class GizmoTool
    {
        move,
        rotate,
        scale,
    };

    enum class GizmoOrientation
    {
        global,
        local,
    };

    explicit AuthoringViewport(QWidget* parent = nullptr);

    void set_preview_frame(const QImage& image);
    void set_play_frame(const QImage& image);
    void clear_preview_frame();
    void set_play_mode(bool enabled);
    void set_selected_name(QString name);
    void set_view_mode(ViewMode mode);
    void set_gizmo_tool(GizmoTool tool);
    void set_gizmo_orientation(GizmoOrientation orientation);
    void set_snapping(bool enabled, float translation = 0.5F, float rotation = 15.0F, float scale = 0.1F);
    void set_selection_geometry(
        const QVector3D& anchor,
        const QVector3D& bounds_minimum,
        const QVector3D& bounds_maximum,
        const QQuaternion& local_rotation);
    void clear_selection_geometry();

    [[nodiscard]] ViewMode view_mode() const noexcept { return view_mode_; }
    [[nodiscard]] GizmoTool gizmo_tool() const noexcept { return gizmo_tool_; }
    [[nodiscard]] GizmoOrientation gizmo_orientation() const noexcept { return gizmo_orientation_; }
    [[nodiscard]] bool snapping_enabled() const noexcept { return snapping_; }
    [[nodiscard]] bool is_play_mode() const noexcept { return play_mode_; }
    [[nodiscard]] bool has_selection_geometry() const noexcept { return has_selection_geometry_; }
    [[nodiscard]] QPoint gizmo_anchor_widget_position() const;
    [[nodiscard]] const QVector3D& camera_target() const noexcept { return camera_target_; }
    [[nodiscard]] QPoint map_to_frame(const QPoint& widget_position) const;

signals:
    void viewport_resized(const QSize& size);
    void frame_clicked(const QPoint& frame_position);
    void camera_changed(
        bool orthographic,
        const QVector3D& position,
        const QVector3D& target,
        float field_of_view,
        float orthographic_size);
    void gizmo_started(AuthoringViewport::GizmoTool tool);
    void gizmo_previewed(AuthoringViewport::GizmoTool tool, const QVector3D& delta);
    void gizmo_committed(AuthoringViewport::GizmoTool tool, const QVector3D& delta);
    void gizmo_cancelled();
    void project_item_dropped(const QString& path, const QString& kind, const QString& asset_type, const QString& asset_id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum class PointerMode
    {
        none,
        pan,
        orbit,
        gizmo,
    };

    [[nodiscard]] QRect frame_rect() const;
    [[nodiscard]] QRect gizmo_hit_rect() const;
    [[nodiscard]] std::optional<QPointF> project_world_position(const QVector3D& position) const;
    [[nodiscard]] QVector3D gizmo_delta(const QPoint& current) const;
    void focus_selection();
    void emit_camera();
    void draw_grid(QPainter& painter, const QRect& target) const;
    void draw_gizmo(QPainter& painter) const;

    QImage preview_frame_;
    QImage play_frame_;
    QTimer animation_timer_;
    QString selected_name_;
    bool play_mode_{};
    ViewMode view_mode_{ViewMode::three_d};
    GizmoTool gizmo_tool_{GizmoTool::move};
    GizmoOrientation gizmo_orientation_{GizmoOrientation::global};
    PointerMode pointer_mode_{PointerMode::none};
    QPoint pointer_origin_;
    QPoint pointer_previous_;
    QVector3D camera_position_{0.0F, 2.0F, 7.0F};
    QVector3D camera_target_{};
    float field_of_view_{60.0F};
    float orthographic_size_{10.0F};
    bool snapping_{};
    float translation_snap_{0.5F};
    float rotation_snap_{15.0F};
    float scale_snap_{0.1F};
    bool has_selection_geometry_{};
    QVector3D selection_anchor_{};
    QVector3D selection_bounds_minimum_{};
    QVector3D selection_bounds_maximum_{};
    QQuaternion selection_local_rotation_{};
    qreal angle_{};
};

Q_DECLARE_METATYPE(AuthoringViewport::GizmoTool)
