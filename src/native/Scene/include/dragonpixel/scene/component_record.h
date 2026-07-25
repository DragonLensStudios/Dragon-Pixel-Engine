#pragma once

#include <dragonpixel/metadata/descriptor.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace dragonpixel::scene
{
struct component_record final
{
    std::string type_id;
    std::uint32_t schema_version{1};
    metadata::runtime_owner owner{metadata::runtime_owner::native};
    nlohmann::ordered_json properties = nlohmann::ordered_json::object();
    bool opaque{};
    nlohmann::ordered_json raw_record = nlohmann::ordered_json::object();
    bool enabled{true};
    std::string qualified_name;

    friend bool operator==(const component_record&, const component_record&) = default;
};
}
