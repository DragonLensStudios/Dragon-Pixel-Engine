#pragma once

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/scene/component_record.h>

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace dragonpixel::scene
{
struct entity final
{
    core::uuid id;
    std::string name;
    std::optional<core::uuid> parent_id;
    std::vector<component_record> components;
    bool enabled{true};
    std::uint32_t sibling_order{};

    friend bool operator==(const entity&, const entity&) = default;
};
}
