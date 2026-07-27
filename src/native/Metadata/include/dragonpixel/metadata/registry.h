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
    [[nodiscard]] bool add_contract(contract_descriptor descriptor);
    [[nodiscard]] bool add_object_type(object_type_descriptor descriptor);
    [[nodiscard]] const component_descriptor* find(std::string_view type_id) const noexcept;
    [[nodiscard]] const contract_descriptor* find_contract(std::string_view contract_id) const noexcept;
    [[nodiscard]] const object_type_descriptor* find_object_type(std::string_view type_id) const noexcept;
    [[nodiscard]] std::vector<std::reference_wrapper<const object_type_descriptor>> implementations(
        std::string_view contract_id) const;
    [[nodiscard]] std::vector<std::reference_wrapper<const object_type_descriptor>> object_types() const;
    [[nodiscard]] std::size_t size() const noexcept { return descriptors_.size(); }
    [[nodiscard]] std::vector<std::reference_wrapper<const component_descriptor>> descriptors() const;

    [[nodiscard]] static registry slice_one_defaults();

private:
    std::unordered_map<std::string, component_descriptor> descriptors_;
    std::unordered_map<std::string, contract_descriptor> contracts_;
    std::unordered_map<std::string, object_type_descriptor> object_types_;
};
}
