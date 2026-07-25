#pragma once

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/scene/scene.h>

#include <span>
#include <string>
#include <vector>

namespace dragonpixel::scene
{
class world final
{
public:
    explicit world(core::uuid id) : id_(id) {}

    [[nodiscard]] const core::uuid& id() const noexcept { return id_; }
    [[nodiscard]] std::span<const scene> scenes() const noexcept { return scenes_; }
    [[nodiscard]] scene& create_scene(std::string name);
    [[nodiscard]] scene* find_scene(const core::uuid& id) noexcept;

private:
    core::uuid id_;
    std::vector<scene> scenes_;
};
}
