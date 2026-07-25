#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dragonpixel::core
{
class uuid final
{
public:
    constexpr uuid() noexcept = default;
    explicit constexpr uuid(std::array<std::uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] static uuid random_v4();
    [[nodiscard]] static std::optional<uuid> parse(std::string_view value) noexcept;

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] constexpr const std::array<std::uint8_t, 16>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr bool is_nil() const noexcept
    {
        for (const auto byte : bytes_)
        {
            if (byte != 0)
            {
                return false;
            }
        }
        return true;
    }

    friend constexpr bool operator==(const uuid&, const uuid&) noexcept = default;
    friend constexpr auto operator<=>(const uuid&, const uuid&) noexcept = default;

private:
    std::array<std::uint8_t, 16> bytes_{};
};

struct uuid_hash final
{
    [[nodiscard]] std::size_t operator()(const uuid& value) const noexcept;
};
}
