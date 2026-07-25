#include "AuthoringViewport.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>

#include <array>
#include <cmath>

AuthoringViewport::AuthoringViewport(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(640, 360);
    setAccessibleName(QStringLiteral("2D and 3D authoring viewport"));
    setFocusPolicy(Qt::StrongFocus);
    connect(&animation_timer_, &QTimer::timeout, this, [this] {
        angle_ += 0.018;
        update();
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
    update();
}

void AuthoringViewport::paintEvent(QPaintEvent* event)
{
    static_cast<void>(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor{25, 21, 31});
    const auto& worker_frame = play_mode_ ? play_frame_ : preview_frame_;
    if (!worker_frame.isNull())
    {
        const auto scaled = worker_frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(QPoint{(width() - scaled.width()) / 2, (height() - scaled.height()) / 2}, scaled);
        painter.setPen(QColor{240, 196, 72});
        painter.drawText(
            QRect{12, 10, width() - 24, 24},
            play_mode_
                ? QStringLiteral("PLAY WORKER — IMMUTABLE SNAPSHOT")
                : QStringLiteral("EDIT PREVIEW WORKER — AUTHORING MIRROR"));
        return;
    }

    painter.setPen(QPen{QColor{53, 48, 61}, 1});
    for (int x = 0; x < width(); x += 32)
    {
        painter.drawLine(x, height() / 2, x, height());
    }
    for (int y = height() / 2; y < height(); y += 24)
    {
        painter.drawLine(0, y, width(), y);
    }

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
    painter.setPen(QPen{QColor{255, 220, 240}, 2});
    painter.drawRect(sprite_rect);

    const QPointF center{width() * 0.70, height() * 0.48};
    constexpr std::array<std::array<int, 2>, 12> edges{{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
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
    painter.setBrush(QColor{225, 92, 40, 105});
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygonF{points[4], points[5], points[7]});
    painter.drawPolygon(QPolygonF{points[5], points[6], points[7]});
    painter.setPen(QPen{QColor{242, 224, 210}, 2});
    for (const auto& edge : edges)
    {
        painter.drawLine(points[static_cast<std::size_t>(edge[0])], points[static_cast<std::size_t>(edge[1])]);
    }

    painter.setPen(QColor{218, 210, 226});
    painter.drawText(16, 26, QStringLiteral("EDIT — framework-neutral 2D sprite + lit 3D mesh"));
    if (!selected_name_.isEmpty())
    {
        painter.setPen(QColor{243, 185, 72});
        painter.drawText(16, 50, QStringLiteral("Selected: %1").arg(selected_name_));
    }
}
