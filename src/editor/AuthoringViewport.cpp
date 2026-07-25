#include "AuthoringViewport.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
constexpr auto project_item_mime = "application/x-dragonpixel-project-item";

float snapped(float value, float increment)
{
    return increment > 0.0F ? std::round(value / increment) * increment : value;
}
}

AuthoringViewport::AuthoringViewport(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(640, 360);
    setAccessibleName(QStringLiteral("2D and 3D authoring viewport"));
    setAccessibleDescription(QStringLiteral("Perspective 3D authoring view. W, E, and R choose gizmos; F focuses; mouse controls the camera."));
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMouseTracking(true);
    connect(&animation_timer_, &QTimer::timeout, this, [this] {
        angle_ += 0.018;
        if (preview_frame_.isNull() && play_frame_.isNull())
        {
            update();
        }
    });
    animation_timer_.start(16);
}

void AuthoringViewport::set_preview_frame(const QImage& image)
{
    preview_frame_ = image;
    update();
}

void AuthoringViewport::set_play_frame(const QImage& image)
{
    play_frame_ = image;
    update();
}

void AuthoringViewport::clear_preview_frame()
{
    preview_frame_ = {};
    update();
}

void AuthoringViewport::set_play_mode(bool enabled)
{
    play_mode_ = enabled;
    if (enabled)
    {
        play_frame_ = {};
    }
    update();
}

void AuthoringViewport::set_selected_name(QString name)
{
    selected_name_ = std::move(name);
    if (selected_name_.isEmpty())
    {
        clear_selection_geometry();
    }
    update();
}

void AuthoringViewport::set_view_mode(ViewMode mode)
{
    view_mode_ = mode;
    if (mode == ViewMode::two_d)
    {
        camera_position_ = {0.0F, 0.0F, 10.0F};
        camera_target_ = {};
    }
    setAccessibleDescription(mode == ViewMode::two_d
        ? QStringLiteral("Orthographic 2D authoring view. Pan with middle mouse and zoom with the wheel.")
        : QStringLiteral("Perspective 3D authoring view. Orbit with right mouse, pan with middle mouse, and focus with F."));
    emit_camera();
    update();
}

void AuthoringViewport::set_gizmo_tool(GizmoTool tool)
{
    gizmo_tool_ = tool;
    update();
}

void AuthoringViewport::set_gizmo_orientation(GizmoOrientation orientation)
{
    gizmo_orientation_ = orientation;
    update();
}

void AuthoringViewport::set_snapping(bool enabled, float translation, float rotation, float scale)
{
    snapping_ = enabled;
    translation_snap_ = std::max(0.0001F, translation);
    rotation_snap_ = std::max(0.0001F, rotation);
    scale_snap_ = std::max(0.0001F, scale);
    update();
}

void AuthoringViewport::set_selection_geometry(
    const QVector3D& anchor,
    const QVector3D& bounds_minimum,
    const QVector3D& bounds_maximum,
    const QQuaternion& local_rotation)
{
    selection_anchor_ = anchor;
    selection_bounds_minimum_ = bounds_minimum;
    selection_bounds_maximum_ = bounds_maximum;
    selection_local_rotation_ = local_rotation.isNull() ? QQuaternion{} : local_rotation.normalized();
    has_selection_geometry_ = true;
    update();
}

void AuthoringViewport::clear_selection_geometry()
{
    has_selection_geometry_ = false;
    selection_anchor_ = {};
    selection_bounds_minimum_ = {};
    selection_bounds_maximum_ = {};
    selection_local_rotation_ = {};
    update();
}

QRect AuthoringViewport::frame_rect() const
{
    const auto& frame = play_mode_ ? play_frame_ : preview_frame_;
    if (frame.isNull())
    {
        return rect();
    }
    const auto scaled = frame.size().scaled(size(), Qt::KeepAspectRatio);
    return {(width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled.width(), scaled.height()};
}

QPoint AuthoringViewport::map_to_frame(const QPoint& widget_position) const
{
    const auto& frame = play_mode_ ? play_frame_ : preview_frame_;
    const auto target = frame_rect();
    if (frame.isNull() || !target.contains(widget_position) || target.width() <= 0 || target.height() <= 0)
    {
        return {-1, -1};
    }
    return {
        std::clamp((widget_position.x() - target.x()) * frame.width() / target.width(), 0, frame.width() - 1),
        std::clamp((widget_position.y() - target.y()) * frame.height() / target.height(), 0, frame.height() - 1),
    };
}

void AuthoringViewport::draw_grid(QPainter& painter, const QRect& target) const
{
    painter.save();
    painter.setClipRect(target);
    painter.setPen(QPen{QColor{180, 180, 200, 45}, 1});
    const auto spacing = view_mode_ == ViewMode::two_d
        ? std::clamp(static_cast<int>(64.0F / std::max(0.25F, orthographic_size_ / 10.0F)), 16, 128)
        : 48;
    const auto center = target.center();
    for (int x = center.x() % spacing; x < target.right(); x += spacing)
    {
        painter.drawLine(x, target.top(), x, target.bottom());
    }
    for (int y = center.y() % spacing; y < target.bottom(); y += spacing)
    {
        painter.drawLine(target.left(), y, target.right(), y);
    }
    painter.setPen(QPen{QColor{215, 70, 70, 135}, 1});
    painter.drawLine(target.left(), center.y(), target.right(), center.y());
    painter.setPen(QPen{QColor{70, 210, 110, 135}, 1});
    painter.drawLine(center.x(), target.top(), center.x(), target.bottom());
    painter.restore();
}

QRect AuthoringViewport::gizmo_hit_rect() const
{
    const auto center = gizmo_anchor_widget_position();
    if (center.x() < 0)
    {
        return {};
    }
    return {center.x() - 70, center.y() - 70, 140, 140};
}

std::optional<QPointF> AuthoringViewport::project_world_position(const QVector3D& position) const
{
    const auto target = frame_rect();
    if (target.width() <= 0 || target.height() <= 0)
    {
        return std::nullopt;
    }
    const QPointF center = target.center();
    if (view_mode_ == ViewMode::two_d)
    {
        const auto pixels_per_unit = static_cast<float>(target.height())
            / std::max(0.1F, orthographic_size_);
        const auto relative = position - camera_target_;
        const QPointF projected{
            center.x() + (relative.x() * pixels_per_unit),
            center.y() - (relative.y() * pixels_per_unit),
        };
        return target.contains(projected.toPoint())
            ? std::optional<QPointF>{projected}
            : std::nullopt;
    }

    auto forward = camera_target_ - camera_position_;
    if (forward.lengthSquared() < 0.000001F)
    {
        return std::nullopt;
    }
    forward.normalize();
    auto right = QVector3D::crossProduct(forward, QVector3D{0.0F, 1.0F, 0.0F});
    if (right.lengthSquared() < 0.000001F)
    {
        right = QVector3D{1.0F, 0.0F, 0.0F};
    }
    right.normalize();
    const auto up = QVector3D::crossProduct(right, forward).normalized();
    const auto relative = position - camera_position_;
    const auto depth = QVector3D::dotProduct(relative, forward);
    if (depth <= 0.01F)
    {
        return std::nullopt;
    }
    const auto focal_length = (static_cast<float>(target.height()) * 0.5F)
        / std::tan(qDegreesToRadians(field_of_view_) * 0.5F);
    const QPointF projected{
        center.x() + (QVector3D::dotProduct(relative, right) * focal_length / depth),
        center.y() - (QVector3D::dotProduct(relative, up) * focal_length / depth),
    };
    return target.contains(projected.toPoint())
        ? std::optional<QPointF>{projected}
        : std::nullopt;
}

QPoint AuthoringViewport::gizmo_anchor_widget_position() const
{
    if (!has_selection_geometry_)
    {
        return {-1, -1};
    }
    const auto projected = project_world_position(selection_anchor_);
    return projected ? projected->toPoint() : QPoint{-1, -1};
}

void AuthoringViewport::draw_gizmo(QPainter& painter) const
{
    if (selected_name_.isEmpty() || play_mode_ || !has_selection_geometry_)
    {
        return;
    }
    const auto center = gizmo_anchor_widget_position();
    if (center.x() < 0)
    {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (gizmo_tool_ == GizmoTool::rotate)
    {
        painter.setPen(QPen{QColor{245, 205, 70}, 4});
        painter.drawEllipse(center, 42, 42);
    }
    else
    {
        const auto camera_distance = std::max(0.1F, (selection_anchor_ - camera_position_).length());
        const auto world_length = view_mode_ == ViewMode::two_d
            ? orthographic_size_ * 60.0F / static_cast<float>(std::max(1, frame_rect().height()))
            : camera_distance * std::tan(qDegreesToRadians(field_of_view_) * 0.5F)
                * 120.0F / static_cast<float>(std::max(1, frame_rect().height()));
        const auto orientation = gizmo_orientation_ == GizmoOrientation::local
            ? selection_local_rotation_
            : QQuaternion{};
        const std::array<std::pair<QVector3D, QColor>, 3> axes{{
            {orientation.rotatedVector({1.0F, 0.0F, 0.0F}), QColor{235, 70, 70}},
            {orientation.rotatedVector({0.0F, 1.0F, 0.0F}), QColor{70, 225, 110}},
            {orientation.rotatedVector({0.0F, 0.0F, 1.0F}), QColor{70, 135, 245}},
        }};
        const auto axis_count = view_mode_ == ViewMode::three_d ? axes.size() : std::size_t{2};
        for (std::size_t index = 0; index < axis_count; ++index)
        {
            const auto projected = project_world_position(
                selection_anchor_ + (axes[index].first * world_length));
            if (projected)
            {
                painter.setPen(QPen{axes[index].second, 4});
                painter.drawLine(center, projected->toPoint());
            }
        }
        if (gizmo_tool_ == GizmoTool::scale)
        {
            painter.setBrush(QColor{245, 245, 245});
            painter.setPen(Qt::NoPen);
            painter.drawRect(QRect{center - QPoint{5, 5}, QSize{10, 10}});
        }
    }
    painter.setPen(QColor{245, 225, 180});
    painter.drawText(center + QPoint{12, 78}, QStringLiteral("%1 · %2%3")
        .arg(gizmo_tool_ == GizmoTool::move ? QStringLiteral("Move")
             : gizmo_tool_ == GizmoTool::rotate ? QStringLiteral("Rotate") : QStringLiteral("Scale"),
             gizmo_orientation_ == GizmoOrientation::local ? QStringLiteral("Local") : QStringLiteral("Global"),
             snapping_ ? QStringLiteral(" · Snap") : QString{}));
    painter.restore();
}

void AuthoringViewport::paintEvent(QPaintEvent* event)
{
    static_cast<void>(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor{25, 21, 31});
    const auto& worker_frame = play_mode_ ? play_frame_ : preview_frame_;
    const auto target = frame_rect();
    if (!worker_frame.isNull())
    {
        painter.drawImage(target, worker_frame);
    }
    else
    {
        draw_grid(painter, target);
        const auto sprite_size = std::max(48, std::min(width(), height()) / 5);
        const QRect sprite_rect{width() / 5 - sprite_size / 2, height() / 2 - sprite_size / 2, sprite_size, sprite_size};
        const auto cell = std::max(8, sprite_size / 6);
        for (int y = 0; y < sprite_size; y += cell)
        {
            for (int x = 0; x < sprite_size; x += cell)
            {
                painter.fillRect(
                    QRect{sprite_rect.x() + x, sprite_rect.y() + y, cell, cell},
                    ((x / cell) + (y / cell)) % 2 == 0 ? QColor{243, 94, 172} : QColor{190, 50, 106});
            }
        }
        const QPointF center{width() * 0.70, height() * 0.48};
        constexpr std::array<std::array<int, 2>, 12> edges{{
            {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}}, {{4, 5}}, {{5, 6}},
            {{6, 7}}, {{7, 4}}, {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
        }};
        std::array<QPointF, 8> points{};
        const auto scale = std::min(width(), height()) * 0.16;
        for (int index = 0; index < 8; ++index)
        {
            const auto x = (index & 1) != 0 ? 1.0 : -1.0;
            const auto y = (index & 2) != 0 ? 1.0 : -1.0;
            const auto z = (index & 4) != 0 ? 1.0 : -1.0;
            const auto rotated_x = (x * std::cos(angle_)) - (z * std::sin(angle_));
            const auto rotated_z = (x * std::sin(angle_)) + (z * std::cos(angle_));
            const auto perspective = 1.0 / (4.2 + rotated_z);
            points[static_cast<std::size_t>(index)] = {
                center.x() + (rotated_x * scale * perspective * 3.2),
                center.y() - (y * scale * perspective * 3.2),
            };
        }
        painter.setPen(QPen{QColor{242, 224, 210}, 2});
        for (const auto& edge : edges)
        {
            painter.drawLine(points[static_cast<std::size_t>(edge[0])], points[static_cast<std::size_t>(edge[1])]);
        }
    }

    if (!play_mode_)
    {
        draw_grid(painter, target);
        draw_gizmo(painter);
    }
    painter.setPen(QColor{240, 196, 72});
    painter.drawText(
        QRect{12, 10, width() - 24, 24},
        play_mode_ ? QStringLiteral("PLAY WORKER — IMMUTABLE SNAPSHOT")
                   : QStringLiteral("EDIT PREVIEW — %1 · REAL ADAPTER FRAME")
                         .arg(view_mode_ == ViewMode::two_d ? QStringLiteral("2D") : QStringLiteral("3D")));
    if (!selected_name_.isEmpty())
    {
        painter.setPen(QColor{243, 185, 72});
        painter.drawText(16, 50, QStringLiteral("Selected: %1").arg(selected_name_));
    }
}

void AuthoringViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit viewport_resized(event->size());
}

void AuthoringViewport::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    pointer_origin_ = event->position().toPoint();
    pointer_previous_ = pointer_origin_;
    if (!play_mode_ && event->button() == Qt::LeftButton && !selected_name_.isEmpty()
        && gizmo_hit_rect().contains(pointer_origin_))
    {
        pointer_mode_ = PointerMode::gizmo;
        emit gizmo_started(gizmo_tool_);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::AltModifier)))
    {
        pointer_mode_ = PointerMode::pan;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton)
    {
        pointer_mode_ = PointerMode::orbit;
        setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        const auto frame_position = map_to_frame(pointer_origin_);
        if (frame_position.x() >= 0)
        {
            emit frame_clicked(frame_position);
        }
    }
    QWidget::mousePressEvent(event);
}

QVector3D AuthoringViewport::gizmo_delta(const QPoint& current) const
{
    const auto pixels = current - pointer_origin_;
    QVector3D result;
    if (gizmo_tool_ == GizmoTool::move)
    {
        result = {pixels.x() * 0.02F, -pixels.y() * 0.02F, 0.0F};
        if (snapping_)
        {
            result.setX(snapped(result.x(), translation_snap_));
            result.setY(snapped(result.y(), translation_snap_));
        }
    }
    else if (gizmo_tool_ == GizmoTool::rotate)
    {
        result.setZ(static_cast<float>(pixels.x() - pixels.y()) * 0.5F);
        if (snapping_)
        {
            result.setZ(snapped(result.z(), rotation_snap_));
        }
    }
    else
    {
        const auto uniform = static_cast<float>(pixels.x() - pixels.y()) * 0.005F;
        result = {uniform, uniform, uniform};
        if (snapping_)
        {
            result = {snapped(result.x(), scale_snap_), snapped(result.y(), scale_snap_), snapped(result.z(), scale_snap_)};
        }
    }
    return result;
}

void AuthoringViewport::mouseMoveEvent(QMouseEvent* event)
{
    const auto current = event->position().toPoint();
    const auto delta = current - pointer_previous_;
    pointer_previous_ = current;
    if (pointer_mode_ == PointerMode::gizmo)
    {
        emit gizmo_previewed(gizmo_tool_, gizmo_delta(current));
        update();
        return;
    }
    if (pointer_mode_ == PointerMode::pan)
    {
        const auto scale = view_mode_ == ViewMode::two_d ? orthographic_size_ / 500.0F : 0.01F;
        const QVector3D shift{-delta.x() * scale, delta.y() * scale, 0.0F};
        camera_position_ += shift;
        camera_target_ += shift;
        emit_camera();
        return;
    }
    if (pointer_mode_ == PointerMode::orbit && view_mode_ == ViewMode::three_d)
    {
        auto offset = camera_position_ - camera_target_;
        const auto radius = std::max(0.25F, offset.length());
        auto yaw = std::atan2(offset.x(), offset.z()) + delta.x() * 0.008F;
        auto pitch = std::asin(std::clamp(offset.y() / radius, -0.99F, 0.99F)) + delta.y() * 0.008F;
        pitch = std::clamp(pitch, -1.45F, 1.45F);
        camera_position_ = camera_target_ + QVector3D{
            radius * std::sin(yaw) * std::cos(pitch),
            radius * std::sin(pitch),
            radius * std::cos(yaw) * std::cos(pitch),
        };
        emit_camera();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void AuthoringViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (pointer_mode_ == PointerMode::gizmo && event->button() == Qt::LeftButton)
    {
        emit gizmo_committed(gizmo_tool_, gizmo_delta(event->position().toPoint()));
    }
    pointer_mode_ = PointerMode::none;
    unsetCursor();
    QWidget::mouseReleaseEvent(event);
}

void AuthoringViewport::wheelEvent(QWheelEvent* event)
{
    const auto steps = static_cast<float>(event->angleDelta().y()) / 120.0F;
    if (view_mode_ == ViewMode::two_d)
    {
        orthographic_size_ = std::clamp(orthographic_size_ * std::pow(0.88F, steps), 0.1F, 10000.0F);
    }
    else
    {
        auto offset = camera_position_ - camera_target_;
        const auto length = std::clamp(offset.length() * std::pow(0.88F, steps), 0.2F, 10000.0F);
        camera_position_ = camera_target_ + offset.normalized() * length;
    }
    emit_camera();
    event->accept();
}

void AuthoringViewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && pointer_mode_ == PointerMode::gizmo)
    {
        pointer_mode_ = PointerMode::none;
        unsetCursor();
        emit gizmo_cancelled();
        update();
        return;
    }
    if (event->key() == Qt::Key_W)
    {
        set_gizmo_tool(GizmoTool::move);
        return;
    }
    if (event->key() == Qt::Key_E)
    {
        set_gizmo_tool(GizmoTool::rotate);
        return;
    }
    if (event->key() == Qt::Key_R)
    {
        set_gizmo_tool(GizmoTool::scale);
        return;
    }
    if (event->key() == Qt::Key_X)
    {
        set_gizmo_orientation(gizmo_orientation_ == GizmoOrientation::global
            ? GizmoOrientation::local : GizmoOrientation::global);
        return;
    }
    if (event->key() == Qt::Key_S && event->modifiers().testFlag(Qt::ShiftModifier))
    {
        set_snapping(!snapping_);
        return;
    }
    if (event->key() == Qt::Key_F)
    {
        focus_selection();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AuthoringViewport::focus_selection()
{
    if (!has_selection_geometry_)
    {
        return;
    }
    const auto center = (selection_bounds_minimum_ + selection_bounds_maximum_) * 0.5F;
    const auto half_extent = (selection_bounds_maximum_ - selection_bounds_minimum_) * 0.5F;
    if (view_mode_ == ViewMode::two_d)
    {
        const auto aspect = static_cast<float>(std::max(1, frame_rect().width()))
            / static_cast<float>(std::max(1, frame_rect().height()));
        orthographic_size_ = std::max({
            2.0F,
            half_extent.y() * 2.8F,
            half_extent.x() * 2.8F / std::max(0.1F, aspect),
        });
        camera_target_ = center;
        camera_position_ = center + QVector3D{0.0F, 0.0F, 10.0F};
    }
    else
    {
        auto direction = camera_position_ - camera_target_;
        if (direction.lengthSquared() < 0.000001F)
        {
            direction = {0.0F, 0.25F, 1.0F};
        }
        direction.normalize();
        const auto radius = std::max(0.5F, half_extent.length());
        const auto distance = std::max(
            2.5F,
            radius * 1.5F / std::tan(qDegreesToRadians(field_of_view_) * 0.5F));
        camera_target_ = center;
        camera_position_ = center + (direction * distance);
    }
    emit_camera();
}

void AuthoringViewport::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(project_item_mime)))
    {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void AuthoringViewport::dropEvent(QDropEvent* event)
{
    const auto payload = event->mimeData()->data(QString::fromLatin1(project_item_mime));
    const auto object = QJsonDocument::fromJson(payload).object();
    if (!object.isEmpty())
    {
        emit project_item_dropped(
            object.value(QStringLiteral("path")).toString(),
            object.value(QStringLiteral("kind")).toString(),
            object.value(QStringLiteral("assetType")).toString(),
            object.value(QStringLiteral("assetId")).toString());
        event->acceptProposedAction();
        return;
    }
    QWidget::dropEvent(event);
}

void AuthoringViewport::emit_camera()
{
    emit camera_changed(
        view_mode_ == ViewMode::two_d,
        camera_position_,
        camera_target_,
        field_of_view_,
        orthographic_size_);
    update();
}
