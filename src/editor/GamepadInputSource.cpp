#include "GamepadInputSource.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace
{
double axis_value(SDL_Gamepad* gamepad, SDL_GamepadAxis axis)
{
    const auto value = SDL_GetGamepadAxis(gamepad, axis);
    if (value >= 0) return static_cast<double>(value) / 32767.0;
    return static_cast<double>(value) / 32768.0;
}

void insert_button(
    QHash<QString, double>& controls,
    SDL_Gamepad* gamepad,
    const QString& path,
    SDL_GamepadButton button)
{
    controls.insert(path, SDL_GetGamepadButton(gamepad, button) ? 1.0 : 0.0);
}
}

GamepadInputSource::GamepadInputSource(QObject* parent) : QObject(parent)
{
    poll_timer_.setInterval(16);
    poll_timer_.setTimerType(Qt::PreciseTimer);
    connect(&poll_timer_, &QTimer::timeout, this, &GamepadInputSource::poll);
    initialized_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!initialized_)
    {
        diagnostic_ = QStringLiteral("SDL gamepad initialization failed: %1")
            .arg(QString::fromUtf8(SDL_GetError()));
    }
}

GamepadInputSource::~GamepadInputSource()
{
    poll_timer_.stop();
    close_gamepad();
    if (initialized_) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void GamepadInputSource::set_active(bool active)
{
    if (!initialized_) return;
    if (active)
    {
        if (!poll_timer_.isActive())
        {
            poll_timer_.start();
            poll();
        }
        return;
    }
    poll_timer_.stop();
    close_gamepad();
    if (!last_controls_.isEmpty())
    {
        last_controls_.clear();
        emit controls_changed(last_controls_);
    }
}

void GamepadInputSource::poll()
{
    SDL_UpdateGamepads();
    if (gamepad_ != nullptr && !SDL_GamepadConnected(gamepad_)) close_gamepad();
    if (gamepad_ == nullptr)
    {
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        if (ids != nullptr && count > 0)
        {
            gamepad_id_ = ids[0];
            gamepad_ = SDL_OpenGamepad(gamepad_id_);
            if (gamepad_ == nullptr)
            {
                diagnostic_ = QStringLiteral("SDL could not open gamepad: %1")
                    .arg(QString::fromUtf8(SDL_GetError()));
                gamepad_id_ = 0;
            }
        }
        SDL_free(ids);
    }

    QHash<QString, double> controls;
    if (gamepad_ != nullptr)
    {
        controls.insert(QStringLiteral("gamepad/left-x"),
            axis_value(gamepad_, SDL_GAMEPAD_AXIS_LEFTX));
        controls.insert(QStringLiteral("gamepad/left-y"),
            axis_value(gamepad_, SDL_GAMEPAD_AXIS_LEFTY));
        controls.insert(QStringLiteral("gamepad/right-x"),
            axis_value(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX));
        controls.insert(QStringLiteral("gamepad/right-y"),
            axis_value(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY));
        controls.insert(QStringLiteral("gamepad/left-trigger"), std::clamp(
            axis_value(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0, 1.0));
        controls.insert(QStringLiteral("gamepad/right-trigger"), std::clamp(
            axis_value(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0, 1.0));

        insert_button(controls, gamepad_, QStringLiteral("gamepad/south"), SDL_GAMEPAD_BUTTON_SOUTH);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/east"), SDL_GAMEPAD_BUTTON_EAST);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/west"), SDL_GAMEPAD_BUTTON_WEST);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/north"), SDL_GAMEPAD_BUTTON_NORTH);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/back"), SDL_GAMEPAD_BUTTON_BACK);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/guide"), SDL_GAMEPAD_BUTTON_GUIDE);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/start"), SDL_GAMEPAD_BUTTON_START);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/left-stick"), SDL_GAMEPAD_BUTTON_LEFT_STICK);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/right-stick"), SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/left-shoulder"), SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/right-shoulder"), SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/dpad-up"), SDL_GAMEPAD_BUTTON_DPAD_UP);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/dpad-down"), SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/dpad-left"), SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        insert_button(controls, gamepad_, QStringLiteral("gamepad/dpad-right"), SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    }
    if (controls != last_controls_)
    {
        last_controls_ = controls;
        emit controls_changed(last_controls_);
    }
}

void GamepadInputSource::close_gamepad()
{
    if (gamepad_ != nullptr) SDL_CloseGamepad(gamepad_);
    gamepad_ = nullptr;
    gamepad_id_ = 0;
}

