#include <dragonpixel/scene/world.h>

#include <algorithm>
#include <utility>

namespace dragonpixel::scene
{
scene& world::create_scene(std::string name)
{
    scenes_.emplace_back(core::uuid::random_v4(), std::move(name));
    return scenes_.back();
}

scene* world::find_scene(const core::uuid& id) noexcept
{
    const auto found = std::find_if(scenes_.begin(), scenes_.end(), [&id](const auto& value) {
        return value.id() == id;
    });
    return found == scenes_.end() ? nullptr : &*found;
}
}
