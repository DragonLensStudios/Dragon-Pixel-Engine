#pragma once

#include <filesystem>
#include <string_view>

namespace dragonpixel::serialization
{
enum class save_fault
{
    none,
    after_temporary_flush,
};

struct save_result final
{
    bool succeeded{};
    std::string error;
};

[[nodiscard]] save_result save_utf8_atomic(
    const std::filesystem::path& target,
    std::string_view contents,
    save_fault injected_fault = save_fault::none);

[[nodiscard]] save_result recover_backup(const std::filesystem::path& target);
}
