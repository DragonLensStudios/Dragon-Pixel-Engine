#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

struct SDL_Gamepad;

class GamepadInputSource final : public QObject
{
    Q_OBJECT

public:
    explicit GamepadInputSource(QObject* parent = nullptr);
    ~GamepadInputSource() override;

    void set_active(bool active);
    [[nodiscard]] bool is_available() const noexcept { return initialized_; }
    [[nodiscard]] QString diagnostic() const { return diagnostic_; }

signals:
    void controls_changed(const QHash<QString, double>& controls);

private:
    void poll();
    void close_gamepad();

    QTimer poll_timer_;
    SDL_Gamepad* gamepad_{};
    unsigned int gamepad_id_{};
    QHash<QString, double> last_controls_;
    QString diagnostic_;
    bool initialized_{};
};

