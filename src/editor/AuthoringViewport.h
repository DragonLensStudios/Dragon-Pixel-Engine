#pragma once

#include <QImage>
#include <QTimer>
#include <QWidget>

class AuthoringViewport final : public QWidget
{
    Q_OBJECT

public:
    explicit AuthoringViewport(QWidget* parent = nullptr);

    void set_preview_frame(const QImage& image);
    void set_play_frame(const QImage& image);
    void clear_preview_frame();
    void set_play_mode(bool enabled);
    void set_selected_name(QString name);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage preview_frame_;
    QImage play_frame_;
    QTimer animation_timer_;
    QString selected_name_;
    bool play_mode_{};
    qreal angle_{};
};
