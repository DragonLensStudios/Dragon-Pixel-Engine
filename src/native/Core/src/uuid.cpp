#include <dragonpixel/core/uuid.h>

#include <array>
#include <charconv>
#include <random>
#include <stdexcept>

namespace dragonpixel::core
{
namespace
{
constexpr std::array<std::size_t, 16> character_offsets{
    0, 2, 4, 6,
    9, 11,
    14, 16,
    19, 21,
    24, 26, 28, 30, 32, 34,
};

int hex_value(char character) noexcept
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f')
    {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F')
    {
        return character - 'A' + 10;
    }
    return -1;
}
}

uuid uuid::random_v4()
{
    std::random_device random;
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    std::array<std::uint8_t, 16> bytes{};
    for (auto& byte : bytes)
    {
        byte = static_cast<std::uint8_t>(distribution(random));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return uuid{bytes};
}

std::optional<uuid> uuid::parse(std::string_view value) noexcept
{
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-')
    {
        return std::nullopt;
    }

    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t index = 0; index < character_offsets.size(); ++index)
    {
        const auto offset = character_offsets[index];
        const auto high = hex_value(value[offset]);
        const auto low = hex_value(value[offset + 1]);
        if (high < 0 || low < 0)
        {
            return std::nullopt;
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return uuid{bytes};
}

std::string uuid::to_string() const
{
    constexpr auto digits = "0123456789abcdef";
    std::string result(36, '-');
    for (std::size_t index = 0; index < bytes_.size(); ++index)
    {
        const auto offset = character_offsets[index];
        result[offset] = digits[(bytes_[index] >> 4U) & 0x0fU];
        result[offset + 1] = digits[bytes_[index] & 0x0fU];
    }
    return result;
}

std::size_t uuid_hash::operator()(const uuid& value) const noexcept
{
    std::size_t result = 1469598103934665603ULL;
    for (const auto byte : value.bytes())
    {
        result ^= byte;
        result *= 1099511628211ULL;
    }
    return result;
}
}
