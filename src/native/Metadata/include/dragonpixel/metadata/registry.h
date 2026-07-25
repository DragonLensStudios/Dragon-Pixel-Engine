#pragma once

#include <dragonpixel/metadata/descriptor.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace dragonpixel::metadata
{
class registry final
{
public:
    [[nodiscard]] bool add(component_descriptor descriptor);
    [[nodiscard]] const component_descriptor* find(std::string_view type_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return descriptors_.size(); }
    [[nodiscard]] std::vector<std::reference_wrapper<const component_descriptor>> descriptors() const;

    [[nodiscard]] static registry slice_one_defaults();

private:
    std::unordered_map<std::string, component_descriptor> descriptors_;
};
}
