#include <dragonpixel/serialization/atomic_file.h>

#include <dragonpixel/core/uuid.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dragonpixel::serialization
{
namespace
{
using json = nlohmann::ordered_json;

constexpr std::size_t maximum_journal_size = 4U * 1024U * 1024U;
constexpr std::size_t maximum_transaction_entries = 4096U;
constexpr std::string_view journal_format = "dpe.utf8-transaction-journal";
constexpr int journal_format_version = 1;

enum class publication_fault
{
    none,
    transient_sharing_violation,
    transient_sharing_violation_then_topology_unavailable,
    reported_failure_after_publication,
    persistent_sharing_violation,
};

enum class publication_target_state
{
    missing,
    existing,
};

enum class read_fault
{
    none,
    transient_open_failure,
};

enum class inspection_fault
{
    none,
    transient_unavailable,
};

enum class recovery_fault
{
    none,
    transient_preimage_open_failure,
};

#if defined(_WIN32)
constexpr std::array<DWORD, 6> windows_retry_delays_ms{1, 2, 4, 8, 16, 32};
#endif

std::filesystem::path ascii_path(std::string_view value)
{
#if defined(_WIN32)
    std::wstring native;
    native.reserve(value.size());
    for (const auto character : value)
    {
        native.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{native};
#else
    return std::filesystem::path{value};
#endif
}

std::filesystem::path append_ascii(std::filesystem::path value, std::string_view suffix)
{
    value += ascii_path(suffix);
    return value;
}

std::filesystem::path temporary_path(const std::filesystem::path& target)
{
    auto result = append_ascii(target, ".tmp-");
    result += ascii_path(core::uuid::random_v4().to_string());
    return result;
}

std::filesystem::path backup_path(const std::filesystem::path& target)
{
    return append_ascii(target, ".bak");
}

std::filesystem::path transaction_base_path(const std::filesystem::path& recovery_root)
{
    return recovery_root / ascii_path(".dragonpixel") / ascii_path("Recovery") / ascii_path("Transactions");
}

std::string path_to_utf8(const std::filesystem::path& value)
{
    const auto encoded = value.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path path_from_utf8(std::string_view value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const auto character : value)
    {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{encoded};
}

bool is_safe_relative_path(const std::filesystem::path& value)
{
    if (value.empty() || value.is_absolute() || value.has_root_name() || value.has_root_directory())
    {
        return false;
    }
    for (const auto& component : value)
    {
        if (component == ascii_path(".."))
        {
            return false;
        }
    }
    return true;
}

bool is_within(const std::filesystem::path& root, const std::filesystem::path& candidate, bool allow_equal = false)
{
    const auto relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty())
    {
        return allow_equal && candidate.lexically_normal() == root.lexically_normal();
    }
    if (relative.is_absolute())
    {
        return false;
    }
    for (const auto& component : relative)
    {
        if (component == ascii_path(".."))
        {
            return false;
        }
    }
    return true;
}

save_result filesystem_exists(const std::filesystem::path& value, bool& exists)
{
    std::error_code error;
    exists = std::filesystem::exists(value, error);
    return error ? save_result{false, "Could not inspect " + path_to_utf8(value) + ": " + error.message()}
                 : save_result{true, {}};
}

save_result inspect_path(
    const std::filesystem::path& value,
    bool& exists,
    bool& is_regular,
    bool& is_symlink,
    inspection_fault injected_fault = inspection_fault::none)
{
    if (injected_fault == inspection_fault::transient_unavailable)
    {
        return {false, "Injected transient path-inspection failure."};
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(value, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        exists = false;
        is_regular = false;
        is_symlink = false;
        return {true, {}};
    }
    if (error)
    {
        return {false, "Could not inspect " + path_to_utf8(value) + ": " + error.message()};
    }
    exists = std::filesystem::exists(status);
    is_regular = std::filesystem::is_regular_file(status);
    is_symlink = std::filesystem::is_symlink(status);
    return {true, {}};
}

save_result read_file(
    const std::filesystem::path& value,
    std::string& contents,
    read_fault injected_fault = read_fault::none)
{
#if defined(_WIN32)
    std::string last_error = "The file could not be opened.";
    for (std::size_t attempt = 0;; ++attempt)
    {
        bool exists = false;
        bool regular = false;
        bool symlink = false;
        const auto inspected = inspect_path(value, exists, regular, symlink);
        if (inspected.succeeded && (!exists || !regular || symlink))
        {
            return {false, "Could not safely open " + path_to_utf8(value) + " for reading."};
        }
        const auto simulate_failure = injected_fault == read_fault::transient_open_failure
            && attempt == 0;
        if (inspected.succeeded && !simulate_failure)
        {
            std::ifstream input{value, std::ios::binary};
            if (input)
            {
                contents.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
                return input.bad() ? save_result{false, "Could not read " + path_to_utf8(value) + "."}
                                   : save_result{true, {}};
            }
            last_error = "The file could not be opened.";
        }
        else if (!inspected.succeeded)
        {
            last_error = inspected.error;
        }
        else
        {
            last_error = "Injected transient file-open failure.";
        }

        if (attempt >= windows_retry_delays_ms.size())
        {
            return {false, "Could not open " + path_to_utf8(value) + " for reading after "
                + std::to_string(attempt + 1) + " attempt(s): " + last_error};
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds{windows_retry_delays_ms[attempt]});
    }
#else
    static_cast<void>(injected_fault);
    std::ifstream input{value, std::ios::binary};
    if (!input)
    {
        return {false, "Could not open " + path_to_utf8(value) + " for reading."};
    }
    contents.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    return input.bad() ? save_result{false, "Could not read " + path_to_utf8(value) + "."}
                       : save_result{true, {}};
#endif
}

class sha256 final
{
public:
    void update(const std::uint8_t* data, std::size_t size)
    {
        total_bits_ += static_cast<std::uint64_t>(size) * 8U;
        while (size > 0)
        {
            const auto copied = std::min(size, block_.size() - block_size_);
            std::memcpy(block_.data() + block_size_, data, copied);
            block_size_ += copied;
            data += copied;
            size -= copied;
            if (block_size_ == block_.size())
            {
                transform();
                block_size_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish()
    {
        block_[block_size_++] = 0x80;
        if (block_size_ > 56)
        {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0);
            transform();
            block_size_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56, 0);
        for (int index = 0; index < 8; ++index)
        {
            block_[63 - index] = static_cast<std::uint8_t>(total_bits_ >> (index * 8));
        }
        transform();
        std::array<std::uint8_t, 32> result{};
        for (std::size_t index = 0; index < state_.size(); ++index)
        {
            result[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24);
            result[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
            result[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
            result[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
        }
        return result;
    }

private:
    static constexpr std::array<std::uint32_t, 64> constants_{
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U,
    };

    void transform()
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index)
        {
            words[index] = (static_cast<std::uint32_t>(block_[index * 4]) << 24)
                | (static_cast<std::uint32_t>(block_[index * 4 + 1]) << 16)
                | (static_cast<std::uint32_t>(block_[index * 4 + 2]) << 8)
                | static_cast<std::uint32_t>(block_[index * 4 + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index)
        {
            const auto s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18)
                ^ (words[index - 15] >> 3);
            const auto s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19)
                ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const auto s1 = std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
            const auto choice = (e&f)^((~e)&g);
            const auto t1 = h+s1+choice+constants_[index]+words[index];
            const auto s0 = std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
            const auto majority = (a&b)^(a&c)^(b&c);
            const auto t2 = s0+majority;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_{};
    std::uint64_t total_bits_{};
};

std::string sha256_hex(std::string_view contents)
{
    sha256 hash;
    hash.update(reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size());
    const auto digest = hash.finish();
    constexpr char hexadecimal[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto value : digest)
    {
        result.push_back(hexadecimal[value >> 4]);
        result.push_back(hexadecimal[value & 0x0FU]);
    }
    return result;
}

bool is_sha256(std::string_view value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

#if defined(_WIN32)
save_result write_and_flush(const std::filesystem::path& path, std::string_view contents)
{
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return {false, "CreateFileW failed with error " + std::to_string(GetLastError())};
    }
    std::size_t offset = 0;
    while (offset < contents.size())
    {
        const auto remaining = std::min<std::size_t>(contents.size() - offset, MAXDWORD);
        DWORD written = 0;
        if (!WriteFile(handle, contents.data() + offset, static_cast<DWORD>(remaining), &written, nullptr)
            || written == 0)
        {
            const auto error = GetLastError();
            CloseHandle(handle);
            return {false, "WriteFile failed with error " + std::to_string(error)};
        }
        offset += written;
    }
    if (!FlushFileBuffers(handle))
    {
        const auto error = GetLastError();
        CloseHandle(handle);
        return {false, "FlushFileBuffers failed with error " + std::to_string(error)};
    }
    if (!CloseHandle(handle))
    {
        return {false, "CloseHandle failed with error " + std::to_string(GetLastError())};
    }
    return {true, {}};
}

void flush_parent_directory(const std::filesystem::path&)
{
}
#else
save_result write_and_flush(const std::filesystem::path& path, std::string_view contents)
{
    const auto descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0)
    {
        return {false, std::strerror(errno)};
    }
    std::size_t offset = 0;
    while (offset < contents.size())
    {
        const auto written = ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (written <= 0)
        {
            const auto message = std::string{std::strerror(errno)};
            ::close(descriptor);
            return {false, message};
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0)
    {
        const auto message = std::string{std::strerror(errno)};
        ::close(descriptor);
        return {false, message};
    }
    if (::close(descriptor) != 0)
    {
        return {false, std::strerror(errno)};
    }
    return {true, {}};
}

void flush_parent_directory(const std::filesystem::path& target)
{
    const auto parent = target.parent_path().empty() ? std::filesystem::path{"."} : target.parent_path();
    const auto descriptor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor >= 0)
    {
        static_cast<void>(::fsync(descriptor));
        static_cast<void>(::close(descriptor));
    }
}
#endif

save_result remove_file(const std::filesystem::path& value)
{
    std::error_code error;
    const auto removed = std::filesystem::remove(value, error);
    if (error)
    {
        return {false, "Could not remove " + path_to_utf8(value) + ": " + error.message()};
    }
    if (removed)
    {
        flush_parent_directory(value);
    }
    return {true, {}};
}

#if defined(_WIN32)
std::string windows_system_message(DWORD error)
{
    std::array<wchar_t, 512> buffer{};
    auto length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        nullptr);
    while (length > 0
        && (buffer[length - 1] == L'\r' || buffer[length - 1] == L'\n'
            || buffer[length - 1] == L' ' || buffer[length - 1] == L'\t'))
    {
        --length;
    }
    if (length == 0)
    {
        return "system message unavailable";
    }
    const auto utf8_size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0)
    {
        return "system message unavailable";
    }
    std::string message(static_cast<std::size_t>(utf8_size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), static_cast<int>(length),
            message.data(), utf8_size, nullptr, nullptr)
        != utf8_size)
    {
        return "system message unavailable";
    }
    return message;
}

enum class publication_topology
{
    safe,
    unavailable,
    unsafe,
};

struct publication_topology_result final
{
    publication_topology state{};
    std::string detail;
};

save_result windows_publication_failure(
    std::string_view api,
    DWORD error,
    std::size_t attempts,
    const std::filesystem::path& target,
    const std::filesystem::path& staged,
    const std::optional<std::filesystem::path>& backup)
{
    return {
        false,
        std::string{api} + " failed with error " + std::to_string(error) + " ("
            + windows_system_message(error) + ") after " + std::to_string(attempts)
            + " attempt(s); target=\"" + path_to_utf8(target) + "\"; staged=\""
            + path_to_utf8(staged) + "\"; backup="
            + (backup ? "\"" + path_to_utf8(*backup) + "\"" : "<none>") + "."};
}

save_result windows_topology_failure(
    std::string_view api,
    std::size_t probes,
    std::size_t api_attempts,
    const publication_topology_result& topology,
    const std::filesystem::path& target,
    const std::filesystem::path& staged,
    const std::optional<std::filesystem::path>& backup)
{
    return {
        false,
        "Could not verify a safe " + std::string{api} + " publication topology after "
            + std::to_string(probes) + " probe(s) and " + std::to_string(api_attempts)
            + " API attempt(s): " + topology.detail + " target=\"" + path_to_utf8(target)
            + "\"; staged=\"" + path_to_utf8(staged) + "\"; backup="
            + (backup ? "\"" + path_to_utf8(*backup) + "\"" : "<none>") + "."};
}

constexpr bool move_file_retryable(DWORD error)
{
    return error == ERROR_SHARING_VIOLATION
        || error == ERROR_LOCK_VIOLATION
        || error == ERROR_USER_MAPPED_FILE
        || error == ERROR_RETRY
        || error == ERROR_ACCESS_DENIED;
}

constexpr bool replace_file_retryable(DWORD error)
{
    return move_file_retryable(error)
        || error == ERROR_UNABLE_TO_REMOVE_REPLACED
        || error == ERROR_UNABLE_TO_MOVE_REPLACEMENT;
}

static_assert(!move_file_retryable(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2));
static_assert(!replace_file_retryable(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2));
static_assert(move_file_retryable(ERROR_ACCESS_DENIED));
static_assert(replace_file_retryable(ERROR_ACCESS_DENIED));

publication_topology_result inspect_replace_file_retry_topology(
    const std::filesystem::path& target,
    const std::filesystem::path& staged,
    const std::filesystem::path& backup)
{
    bool target_exists = false;
    bool target_regular = false;
    bool target_symlink = false;
    bool staged_exists = false;
    bool staged_regular = false;
    bool staged_symlink = false;
    bool backup_exists = false;
    bool backup_regular = false;
    bool backup_symlink = false;
    auto inspected = inspect_path(target, target_exists, target_regular, target_symlink);
    if (!inspected.succeeded)
    {
        return {publication_topology::unavailable, inspected.error};
    }
    inspected = inspect_path(staged, staged_exists, staged_regular, staged_symlink);
    if (!inspected.succeeded)
    {
        return {publication_topology::unavailable, inspected.error};
    }
    inspected = inspect_path(backup, backup_exists, backup_regular, backup_symlink);
    if (!inspected.succeeded)
    {
        return {publication_topology::unavailable, inspected.error};
    }
    const auto safe = target_exists
        && target_regular
        && !target_symlink
        && staged_exists
        && staged_regular
        && !staged_symlink
        && !backup_exists
        && !backup_symlink;
    return safe
        ? publication_topology_result{publication_topology::safe, {}}
        : publication_topology_result{
            publication_topology::unsafe,
            "The target, staged file, or backup changed during publication."};
}

publication_topology_result inspect_move_file_retry_topology(
    const std::filesystem::path& target,
    const std::filesystem::path& staged,
    publication_target_state expected_target_state,
    inspection_fault injected_fault = inspection_fault::none)
{
    bool target_exists = false;
    bool target_regular = false;
    bool target_symlink = false;
    bool staged_exists = false;
    bool staged_regular = false;
    bool staged_symlink = false;
    auto inspected = inspect_path(
        target, target_exists, target_regular, target_symlink, injected_fault);
    if (!inspected.succeeded)
    {
        return {publication_topology::unavailable, inspected.error};
    }
    inspected = inspect_path(staged, staged_exists, staged_regular, staged_symlink);
    if (!inspected.succeeded)
    {
        return {publication_topology::unavailable, inspected.error};
    }
    const auto safe = staged_exists && staged_regular && !staged_symlink
        && (expected_target_state == publication_target_state::existing
        ? target_exists && target_regular && !target_symlink
        : !target_exists && !target_symlink);
    return safe
        ? publication_topology_result{publication_topology::safe, {}}
        : publication_topology_result{
            publication_topology::unsafe,
            "The target or staged file changed during publication."};
}
#endif

save_result replace_staged_file(
    const std::filesystem::path& target,
    const std::filesystem::path& staged,
    const std::optional<std::filesystem::path>& backup,
    publication_target_state expected_target_state,
    publication_fault injected_fault = publication_fault::none)
{
    bool target_exists = false;
    bool target_regular = false;
    bool target_symlink = false;
    auto inspected = inspect_path(target, target_exists, target_regular, target_symlink);
    if (!inspected.succeeded)
    {
        return inspected;
    }
    bool staged_exists = false;
    bool staged_regular = false;
    bool staged_symlink = false;
    inspected = inspect_path(staged, staged_exists, staged_regular, staged_symlink);
    if (!inspected.succeeded)
    {
        return inspected;
    }
    if (!staged_exists || !staged_regular || staged_symlink)
    {
        return {false, "The staged publication file was missing or unsafe: staged=\""
            + path_to_utf8(staged) + "\"."};
    }
    const auto target_matches = expected_target_state == publication_target_state::existing
        ? target_exists && target_regular && !target_symlink
        : !target_exists && !target_symlink;
    if (!target_matches)
    {
        return {false, "The publication target did not match its expected "
            + std::string{expected_target_state == publication_target_state::existing ? "existing" : "missing"}
            + " state: target=\"" + path_to_utf8(target) + "\"; staged=\""
            + path_to_utf8(staged) + "\"; backup="
            + (backup ? "\"" + path_to_utf8(*backup) + "\"" : "<none>") + "."};
    }
    if (expected_target_state == publication_target_state::missing && backup)
    {
        return {false, "A backup was requested for a publication target expected to be missing: target=\""
            + path_to_utf8(target) + "\"; staged=\"" + path_to_utf8(staged)
            + "\"; backup=\"" + path_to_utf8(*backup) + "\"."};
    }
#if defined(_WIN32)
    if (backup)
    {
        const auto removed = remove_file(*backup);
        if (!removed.succeeded)
        {
            return removed;
        }
        std::size_t delay_index = 0;
        std::size_t topology_probes = 1;
        std::size_t api_attempts = 0;
        auto topology = inspect_replace_file_retry_topology(target, staged, *backup);
        for (;;)
        {
            if (topology.state != publication_topology::safe)
            {
                if (topology.state == publication_topology::unsafe
                    || delay_index >= windows_retry_delays_ms.size())
                {
                    return windows_topology_failure(
                        "ReplaceFileW", topology_probes, api_attempts,
                        topology, target, staged, backup);
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds{windows_retry_delays_ms[delay_index++]});
                topology = inspect_replace_file_retry_topology(target, staged, *backup);
                ++topology_probes;
                continue;
            }
            ++api_attempts;
            if (ReplaceFileW(target.c_str(), staged.c_str(), backup->c_str(), 0, nullptr, nullptr))
            {
                return {true, {}};
            }
            const auto last_error = GetLastError();
            if (!replace_file_retryable(last_error))
            {
                return windows_publication_failure(
                    "ReplaceFileW", last_error, api_attempts, target, staged, backup);
            }
            topology = inspect_replace_file_retry_topology(target, staged, *backup);
            ++topology_probes;
            if (topology.state == publication_topology::unsafe)
            {
                return windows_topology_failure(
                    "ReplaceFileW", topology_probes, api_attempts,
                    topology, target, staged, backup);
            }
            if (delay_index >= windows_retry_delays_ms.size())
            {
                return topology.state == publication_topology::unavailable
                    ? windows_topology_failure(
                        "ReplaceFileW", topology_probes, api_attempts,
                        topology, target, staged, backup)
                    : windows_publication_failure(
                        "ReplaceFileW", last_error, api_attempts, target, staged, backup);
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds{windows_retry_delays_ms[delay_index++]});
            topology = inspect_replace_file_retry_topology(target, staged, *backup);
            ++topology_probes;
        }
    }

    auto flags = static_cast<DWORD>(MOVEFILE_WRITE_THROUGH);
    if (expected_target_state == publication_target_state::existing)
    {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    const auto transient_api_fault = injected_fault == publication_fault::transient_sharing_violation
        || injected_fault == publication_fault::transient_sharing_violation_then_topology_unavailable;
    const auto persistent_api_fault = injected_fault == publication_fault::persistent_sharing_violation;
    auto inject_topology_unavailable =
        injected_fault == publication_fault::transient_sharing_violation_then_topology_unavailable;
    std::size_t delay_index = 0;
    std::size_t topology_probes = 1;
    std::size_t api_attempts = 0;
    auto topology = inspect_move_file_retry_topology(target, staged, expected_target_state);
    for (;;)
    {
        if (topology.state != publication_topology::safe)
        {
            if (topology.state == publication_topology::unsafe
                || delay_index >= windows_retry_delays_ms.size())
            {
                return windows_topology_failure(
                    "MoveFileExW", topology_probes, api_attempts,
                    topology, target, staged, backup);
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds{windows_retry_delays_ms[delay_index++]});
            topology = inspect_move_file_retry_topology(target, staged, expected_target_state);
            ++topology_probes;
            continue;
        }
        ++api_attempts;
        const auto simulate_failure = persistent_api_fault
            || (transient_api_fault && api_attempts == 1);
        DWORD last_error = ERROR_SUCCESS;
        if (!simulate_failure)
        {
            if (MoveFileExW(staged.c_str(), target.c_str(), flags))
            {
                return {true, {}};
            }
            last_error = GetLastError();
        }
        else
        {
            last_error = ERROR_SHARING_VIOLATION;
        }
        if (!move_file_retryable(last_error))
        {
            return windows_publication_failure(
                "MoveFileExW", last_error, api_attempts, target, staged, backup);
        }
        topology = inspect_move_file_retry_topology(
            target,
            staged,
            expected_target_state,
            inject_topology_unavailable
                ? inspection_fault::transient_unavailable
                : inspection_fault::none);
        inject_topology_unavailable = false;
        ++topology_probes;
        if (topology.state == publication_topology::unsafe)
        {
            return windows_topology_failure(
                "MoveFileExW", topology_probes, api_attempts,
                topology, target, staged, backup);
        }
        if (delay_index >= windows_retry_delays_ms.size())
        {
            return topology.state == publication_topology::unavailable
                ? windows_topology_failure(
                    "MoveFileExW", topology_probes, api_attempts,
                    topology, target, staged, backup)
                : windows_publication_failure(
                    "MoveFileExW", last_error, api_attempts, target, staged, backup);
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds{windows_retry_delays_ms[delay_index++]});
        topology = inspect_move_file_retry_topology(target, staged, expected_target_state);
        ++topology_probes;
    }
#else
    static_cast<void>(injected_fault);
    if (backup)
    {
        const auto removed = remove_file(*backup);
        if (!removed.succeeded)
        {
            return removed;
        }
        if (::link(target.c_str(), backup->c_str()) != 0)
        {
            return {false, std::strerror(errno)};
        }
    }
    if (::rename(staged.c_str(), target.c_str()) != 0)
    {
        return {false, std::strerror(errno)};
    }
    flush_parent_directory(target);
#endif
    return {true, {}};
}

bool same_lexical_path(const std::filesystem::path& left, const std::filesystem::path& right)
{
    const auto left_native = left.lexically_normal().native();
    const auto right_native = right.lexically_normal().native();
#if defined(_WIN32)
    return CompareStringOrdinal(
               left_native.c_str(), static_cast<int>(left_native.size()),
               right_native.c_str(), static_cast<int>(right_native.size()), TRUE) == CSTR_EQUAL;
#elif defined(__APPLE__)
    if (left_native.size() != right_native.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left_native.size(); ++index)
    {
        const auto fold = [](unsigned char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
        };
        if (fold(static_cast<unsigned char>(left_native[index]))
            != fold(static_cast<unsigned char>(right_native[index])))
        {
            return false;
        }
    }
    return true;
#else
    return left_native == right_native;
#endif
}

save_result paths_alias(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    bool& alias)
{
    alias = same_lexical_path(left, right);
    if (alias)
    {
        return {true, {}};
    }
    bool left_exists = false;
    bool right_exists = false;
    auto inspected = filesystem_exists(left, left_exists);
    if (!inspected.succeeded)
    {
        return inspected;
    }
    inspected = filesystem_exists(right, right_exists);
    if (!inspected.succeeded)
    {
        return inspected;
    }
    if (left_exists && right_exists)
    {
        std::error_code error;
        alias = std::filesystem::equivalent(left, right, error);
        if (error)
        {
            return {false, "Could not compare path identities: " + error.message()};
        }
    }
    return {true, {}};
}

struct recovery_paths final
{
    std::filesystem::path root;
    std::filesystem::path transaction_base;
    bool transaction_base_exists{};
};

save_result resolve_recovery_paths(
    const std::filesystem::path& requested_root,
    bool create_transaction_base,
    recovery_paths& result)
{
    if (requested_root.empty())
    {
        return {false, "The transaction recovery root was empty."};
    }
    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(requested_root, error);
    if (error)
    {
        return {false, "Could not resolve the transaction recovery root: " + error.message()};
    }
    result.root = std::filesystem::canonical(absolute_root, error);
    if (error || !std::filesystem::is_directory(result.root, error) || error)
    {
        return {false, "The transaction recovery root must be an existing directory."};
    }

    const auto candidate = transaction_base_path(result.root);
    const auto prospective = std::filesystem::weakly_canonical(candidate, error);
    if (error || !is_within(result.root, prospective))
    {
        return {false, "The transaction recovery directory would escape its recovery root."};
    }
    bool base_exists = false;
    auto inspected = filesystem_exists(candidate, base_exists);
    if (!inspected.succeeded)
    {
        return inspected;
    }
    if (!base_exists && !create_transaction_base)
    {
        result.transaction_base = candidate.lexically_normal();
        result.transaction_base_exists = false;
        return {true, {}};
    }
    if (!base_exists)
    {
        std::filesystem::create_directories(candidate, error);
        if (error)
        {
            return {false, "Could not create the transaction recovery directory: " + error.message()};
        }
    }
    result.transaction_base = std::filesystem::canonical(candidate, error);
    if (error || !is_within(result.root, result.transaction_base))
    {
        return {false, "The transaction recovery directory escaped its recovery root."};
    }
    result.transaction_base_exists = true;
    return {true, {}};
}

save_result resolve_contained_file(
    const recovery_paths& paths,
    const std::filesystem::path& requested,
    bool allow_missing,
    std::filesystem::path& resolved,
    bool& exists)
{
    if (requested.empty())
    {
        return {false, "A transaction target path was empty."};
    }
    std::error_code error;
    auto candidate = requested.is_absolute() ? requested : paths.root / requested;
    candidate = std::filesystem::absolute(candidate, error).lexically_normal();
    if (error || candidate.filename().empty())
    {
        return {false, "A transaction target path was invalid."};
    }
    const auto parent = std::filesystem::canonical(candidate.parent_path(), error);
    if (error || !std::filesystem::is_directory(parent, error) || error)
    {
        return {false, "Every transaction target parent must be an existing directory."};
    }

    bool regular = false;
    bool symlink = false;
    auto inspected = inspect_path(candidate, exists, regular, symlink);
    if (!inspected.succeeded)
    {
        return inspected;
    }
    if (symlink)
    {
        return {false, "Transaction targets and recovery artifacts may not be symbolic links."};
    }
    if (exists && !regular)
    {
        return {false, "Transaction targets and recovery artifacts must be regular files."};
    }
    if (!exists && !allow_missing)
    {
        return {false, "A required transaction artifact was missing."};
    }
    resolved = exists ? std::filesystem::canonical(candidate, error) : parent / candidate.filename();
    if (error || !is_within(paths.root, resolved)
        || is_within(paths.transaction_base, resolved, true))
    {
        return {false, "A transaction target or recovery artifact escaped the permitted root."};
    }
    return {true, {}};
}

struct transaction_entry final
{
    std::filesystem::path target;
    std::filesystem::path backup;
    std::filesystem::path staged;
    std::filesystem::path preimage;
    std::string target_relative;
    std::string backup_relative;
    std::string staged_relative;
    std::string preimage_relative;
    std::string preimage_hash;
    std::string postimage_hash;
    std::string postimage;
    bool target_existed{};
};

struct transaction_record final
{
    recovery_paths paths;
    std::string transaction_id;
    std::string phase;
    std::filesystem::path directory;
    std::filesystem::path journal;
    std::vector<transaction_entry> entries;
};

save_result ensure_no_aliases(const std::vector<transaction_entry>& entries)
{
    struct named_path final
    {
        std::filesystem::path value;
        std::string role;
    };
    std::vector<named_path> values;
    values.reserve(entries.size() * 3);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        values.push_back({entries[index].target, "target " + std::to_string(index)});
        values.push_back({entries[index].backup, "backup " + std::to_string(index)});
        if (!entries[index].staged.empty())
        {
            values.push_back({entries[index].staged, "staged file " + std::to_string(index)});
        }
    }
    for (std::size_t left = 0; left < values.size(); ++left)
    {
        for (std::size_t right = left + 1; right < values.size(); ++right)
        {
            bool alias = false;
            const auto compared = paths_alias(values[left].value, values[right].value, alias);
            if (!compared.succeeded)
            {
                return compared;
            }
            if (alias)
            {
                return {false, "Transaction path collision between " + values[left].role
                    + " and " + values[right].role + "."};
            }
        }
    }
    return {true, {}};
}

json journal_json(const transaction_record& record)
{
    json entries = json::array();
    for (const auto& entry : record.entries)
    {
        entries.push_back({
            {"target", entry.target_relative},
            {"backup", entry.backup_relative},
            {"staged", entry.staged_relative},
            {"targetExisted", entry.target_existed},
            {"preimage", entry.preimage_relative},
            {"preimageSha256", entry.preimage_hash},
            {"postimageSha256", entry.postimage_hash},
        });
    }
    return {
        {"$schema", "https://dragonpixel.dev/schemas/v1/utf8-transaction-journal.schema.json"},
        {"format", journal_format},
        {"formatVersion", journal_format_version},
        {"transactionId", record.transaction_id},
        {"phase", record.phase},
        {"entries", std::move(entries)},
    };
}

save_result write_journal(
    const transaction_record& record,
    publication_fault injected_fault = publication_fault::none)
{
    const auto temporary = temporary_path(record.journal);
    const auto contents = journal_json(record).dump(2) + "\n";
    auto result = write_and_flush(temporary, contents);
    if (!result.succeeded)
    {
        static_cast<void>(remove_file(temporary));
        return {false, "Could not flush the transaction journal: " + result.error};
    }
    const auto expected_target_state = record.phase == "staging"
        ? publication_target_state::missing
        : publication_target_state::existing;
    const auto replacement_fault = injected_fault == publication_fault::reported_failure_after_publication
        ? publication_fault::none
        : injected_fault;
    result = replace_staged_file(
        record.journal, temporary, std::nullopt, expected_target_state, replacement_fault);
    if (!result.succeeded)
    {
        static_cast<void>(remove_file(temporary));
        return {false, "Could not publish the transaction journal: " + result.error};
    }
    flush_parent_directory(record.journal);
    if (injected_fault == publication_fault::reported_failure_after_publication)
    {
        return {false, "Injected failure after the transaction journal became durable."};
    }
    return {true, {}};
}

save_result remove_transaction_directory(const transaction_record& record)
{
    bool exists = false;
    auto inspected = filesystem_exists(record.directory, exists);
    if (!inspected.succeeded || !exists)
    {
        return inspected;
    }
    std::error_code error;
    const auto canonical = std::filesystem::canonical(record.directory, error);
    if (error || !is_within(record.paths.transaction_base, canonical))
    {
        return {false, "Refused to remove an uncontained transaction artifact directory."};
    }
    std::filesystem::remove_all(canonical, error);
    if (error)
    {
        return {false, "Could not remove transaction artifacts: " + error.message()};
    }
    flush_parent_directory(canonical);
    return {true, {}};
}

save_result cleanup_staged_files(const transaction_record& record)
{
    for (const auto& entry : record.entries)
    {
        const auto removed = remove_file(entry.staged);
        if (!removed.succeeded)
        {
            return removed;
        }
    }
    return {true, {}};
}

save_result validate_preimage_file(
    const transaction_entry& entry,
    read_fault injected_fault = read_fault::none)
{
    if (!entry.target_existed)
    {
        return entry.preimage.empty() && entry.preimage_hash.empty()
            ? save_result{true, {}}
            : save_result{false, "A new target journal entry had a pre-image."};
    }
    std::string contents;
    const auto read = read_file(entry.preimage, contents, injected_fault);
    if (!read.succeeded)
    {
        return read;
    }
    return sha256_hex(contents) == entry.preimage_hash
        ? save_result{true, {}}
        : save_result{false, "A transaction pre-image hash did not match its journal."};
}

enum class target_state
{
    missing,
    preimage,
    postimage,
    unexpected,
};

save_result inspect_target_state(const transaction_entry& entry, target_state& state)
{
    bool exists = false;
    bool regular = false;
    bool symlink = false;
    const auto inspected = inspect_path(entry.target, exists, regular, symlink);
    if (!inspected.succeeded)
    {
        return inspected;
    }
    if (!exists)
    {
        state = target_state::missing;
        return {true, {}};
    }
    if (symlink || !regular)
    {
        state = target_state::unexpected;
        return {true, {}};
    }
    std::string contents;
    const auto read = read_file(entry.target, contents);
    if (!read.succeeded)
    {
        return read;
    }
    const auto hash = sha256_hex(contents);
    if (entry.target_existed && hash == entry.preimage_hash)
    {
        state = target_state::preimage;
    }
    else if (hash == entry.postimage_hash)
    {
        state = target_state::postimage;
    }
    else
    {
        state = target_state::unexpected;
    }
    return {true, {}};
}

save_result restore_preimage(const transaction_entry& entry)
{
    std::string contents;
    auto result = read_file(entry.preimage, contents);
    if (!result.succeeded)
    {
        return result;
    }
    const auto temporary = temporary_path(entry.target);
    result = write_and_flush(temporary, contents);
    if (!result.succeeded)
    {
        static_cast<void>(remove_file(temporary));
        return {false, "Could not stage a transaction pre-image: " + result.error};
    }
    result = replace_staged_file(
        entry.target, temporary, std::nullopt, publication_target_state::existing);
    if (!result.succeeded)
    {
        static_cast<void>(remove_file(temporary));
    }
    return result;
}

std::string preimage_filename(std::size_t index)
{
    std::ostringstream filename;
    filename << "preimage-" << std::setw(6) << std::setfill('0') << index << ".bin";
    return filename.str();
}

bool has_valid_staging_name(
    const std::filesystem::path& target,
    const std::filesystem::path& staged)
{
    if (!same_lexical_path(target.parent_path(), staged.parent_path()))
    {
        return false;
    }
    const auto target_name = path_to_utf8(target.filename());
    const auto staged_name = path_to_utf8(staged.filename());
    const auto prefix = target_name + ".tmp-";
    return staged_name.starts_with(prefix)
        && core::uuid::parse(staged_name.substr(prefix.size())).has_value();
}

save_result validate_loaded_entry_paths(transaction_record& record)
{
    for (std::size_t index = 0; index < record.entries.size(); ++index)
    {
        auto& entry = record.entries[index];
        auto target_relative = path_from_utf8(entry.target_relative).lexically_normal();
        auto backup_relative = path_from_utf8(entry.backup_relative).lexically_normal();
        auto staged_relative = path_from_utf8(entry.staged_relative).lexically_normal();
        auto preimage_relative = path_from_utf8(entry.preimage_relative).lexically_normal();
        if (!is_safe_relative_path(target_relative) || !is_safe_relative_path(backup_relative)
            || !is_safe_relative_path(staged_relative)
            || (entry.target_existed && !is_safe_relative_path(preimage_relative)))
        {
            return {false, "A transaction journal contained an unsafe relative path."};
        }
        bool ignored = false;
        auto resolved = resolve_contained_file(record.paths, target_relative, true, entry.target, ignored);
        if (!resolved.succeeded)
        {
            return resolved;
        }
        resolved = resolve_contained_file(record.paths, backup_relative, true, entry.backup, ignored);
        if (!resolved.succeeded)
        {
            return resolved;
        }
        resolved = resolve_contained_file(record.paths, staged_relative, true, entry.staged, ignored);
        if (!resolved.succeeded)
        {
            return resolved;
        }
        if (!same_lexical_path(entry.backup, backup_path(entry.target))
            || !has_valid_staging_name(entry.target, entry.staged))
        {
            return {false, "A transaction journal entry had invalid recovery artifact paths."};
        }
        if (entry.target_existed)
        {
            if (entry.preimage_relative != preimage_filename(index))
            {
                return {false, "A transaction journal entry had an invalid pre-image path."};
            }
            const auto preimage_candidate = record.directory / preimage_relative;
            std::error_code error;
            const auto normalized_preimage = std::filesystem::absolute(preimage_candidate, error).lexically_normal();
            if (error || !is_within(record.directory, normalized_preimage))
            {
                return {false, "A transaction pre-image escaped its artifact directory."};
            }
            bool exists = false;
            bool regular = false;
            bool symlink = false;
            const auto inspected = inspect_path(normalized_preimage, exists, regular, symlink);
            if (!inspected.succeeded || (!exists && record.phase != "committed")
                || (exists && (!regular || symlink)))
            {
                return inspected.succeeded
                    ? save_result{false, "A transaction pre-image was missing or unsafe."}
                    : inspected;
            }
            entry.preimage = exists
                ? std::filesystem::canonical(normalized_preimage, error)
                : normalized_preimage;
            if (error || !is_within(record.directory, entry.preimage))
            {
                return {false, "A transaction pre-image could not be resolved safely."};
            }
        }
    }
    return ensure_no_aliases(record.entries);
}

save_result load_journal(
    const recovery_paths& paths,
    const std::filesystem::path& transaction_directory,
    transaction_record& record)
{
    std::error_code error;
    record.paths = paths;
    record.directory = std::filesystem::canonical(transaction_directory, error);
    if (error || !is_within(paths.transaction_base, record.directory))
    {
        return {false, "A transaction artifact directory escaped its recovery root."};
    }
    record.transaction_id = path_to_utf8(record.directory.filename());
    if (!core::uuid::parse(record.transaction_id))
    {
        return {false, "A transaction artifact directory did not have a valid transaction ID."};
    }
    record.journal = record.directory / ascii_path("journal.json");
    bool journal_exists = false;
    bool journal_regular = false;
    bool journal_symlink = false;
    auto inspected = inspect_path(record.journal, journal_exists, journal_regular, journal_symlink);
    if (!inspected.succeeded || !journal_exists || !journal_regular || journal_symlink)
    {
        return inspected.succeeded
            ? save_result{false, "A transaction artifact directory had no safe journal."}
            : inspected;
    }
    const auto size = std::filesystem::file_size(record.journal, error);
    if (error || size > maximum_journal_size)
    {
        return {false, "A transaction journal was unreadable or exceeded its size limit."};
    }
    std::string encoded;
    auto read = read_file(record.journal, encoded);
    if (!read.succeeded)
    {
        return read;
    }
    const auto document = json::parse(encoded, nullptr, false);
    if (document.is_discarded() || !document.is_object()
        || document.value("format", std::string{}) != journal_format
        || document.value("formatVersion", 0) != journal_format_version
        || document.value("transactionId", std::string{}) != record.transaction_id
        || !document.contains("entries") || !document["entries"].is_array()
        || document["entries"].empty() || document["entries"].size() > maximum_transaction_entries)
    {
        return {false, "A transaction journal was malformed or incompatible."};
    }
    record.phase = document.value("phase", std::string{});
    if (record.phase != "staging" && record.phase != "prepared" && record.phase != "committed")
    {
        return {false, "A transaction journal had an unsupported phase."};
    }
    for (const auto& value : document["entries"])
    {
        if (!value.is_object())
        {
            return {false, "A transaction journal entry was malformed."};
        }
        transaction_entry entry;
        entry.target_relative = value.value("target", std::string{});
        entry.backup_relative = value.value("backup", std::string{});
        entry.staged_relative = value.value("staged", std::string{});
        entry.target_existed = value.value("targetExisted", false);
        entry.preimage_relative = value.value("preimage", std::string{});
        entry.preimage_hash = value.value("preimageSha256", std::string{});
        entry.postimage_hash = value.value("postimageSha256", std::string{});
        if (!is_sha256(entry.postimage_hash)
            || (entry.target_existed && !is_sha256(entry.preimage_hash))
            || (!entry.target_existed && (!entry.preimage_hash.empty() || !entry.preimage_relative.empty())))
        {
            return {false, "A transaction journal entry had invalid hashes."};
        }
        record.entries.push_back(std::move(entry));
    }
    return validate_loaded_entry_paths(record);
}

save_result recover_transaction(
    transaction_record& record,
    recovery_fault injected_fault = recovery_fault::none)
{
    if (record.phase == "committed")
    {
        auto cleaned = cleanup_staged_files(record);
        return cleaned.succeeded ? remove_transaction_directory(record) : cleaned;
    }

    std::vector<target_state> states;
    states.reserve(record.entries.size());
    for (std::size_t index = 0; index < record.entries.size(); ++index)
    {
        const auto& entry = record.entries[index];
        const auto preimage = validate_preimage_file(
            entry,
            injected_fault == recovery_fault::transient_preimage_open_failure && index == 0
                ? read_fault::transient_open_failure
                : read_fault::none);
        if (!preimage.succeeded)
        {
            return preimage;
        }
        target_state state{};
        const auto inspected = inspect_target_state(entry, state);
        if (!inspected.succeeded)
        {
            return inspected;
        }
        const auto allowed = entry.target_existed
            ? state == target_state::preimage
                || (record.phase == "prepared" && state == target_state::postimage)
            : state == target_state::missing
                || (record.phase == "prepared" && state == target_state::postimage);
        if (!allowed)
        {
            return {false, "Transaction recovery found unexpected target contents and made no changes."};
        }
        states.push_back(state);
    }

    for (std::size_t index = 0; index < record.entries.size(); ++index)
    {
        const auto& entry = record.entries[index];
        if (entry.target_existed && states[index] != target_state::preimage)
        {
            const auto restored = restore_preimage(entry);
            if (!restored.succeeded)
            {
                return {false, "Transaction pre-image restoration failed: " + restored.error};
            }
        }
        else if (!entry.target_existed && states[index] == target_state::postimage)
        {
            const auto removed = remove_file(entry.target);
            if (!removed.succeeded)
            {
                return removed;
            }
        }
    }
    auto cleaned = cleanup_staged_files(record);
    return cleaned.succeeded ? remove_transaction_directory(record) : cleaned;
}

save_result recover_transaction_directory(
    const recovery_paths& paths,
    const std::filesystem::path& transaction_directory)
{
    transaction_record record;
    const auto loaded = load_journal(paths, transaction_directory, record);
    return loaded.succeeded ? recover_transaction(record) : loaded;
}

save_result create_transaction_directory(transaction_record& record)
{
    std::error_code error;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        record.transaction_id = core::uuid::random_v4().to_string();
        record.directory = record.paths.transaction_base / ascii_path(record.transaction_id);
        if (std::filesystem::create_directory(record.directory, error))
        {
            record.journal = record.directory / ascii_path("journal.json");
            flush_parent_directory(record.directory);
            return {true, {}};
        }
        if (error)
        {
            return {false, "Could not create transaction artifacts: " + error.message()};
        }
    }
    return {false, "Could not allocate a unique transaction artifact directory."};
}

save_result prepare_transaction_entries(
    transaction_record& record,
    std::span<const utf8_transaction_write> writes)
{
    record.entries.reserve(writes.size());
    for (std::size_t index = 0; index < writes.size(); ++index)
    {
        transaction_entry entry;
        bool target_exists = false;
        auto resolved = resolve_contained_file(record.paths, writes[index].target, true, entry.target, target_exists);
        if (!resolved.succeeded)
        {
            return resolved;
        }
        entry.target_existed = target_exists;
        entry.backup = backup_path(entry.target);
        bool backup_exists = false;
        std::filesystem::path resolved_backup;
        resolved = resolve_contained_file(record.paths, entry.backup, true, resolved_backup, backup_exists);
        if (!resolved.succeeded)
        {
            return resolved;
        }
        entry.backup = resolved_backup;

        for (int attempt = 0; attempt < 16; ++attempt)
        {
            entry.staged = temporary_path(entry.target);
            bool staged_exists = false;
            auto inspected = filesystem_exists(entry.staged, staged_exists);
            if (!inspected.succeeded)
            {
                return inspected;
            }
            if (!staged_exists)
            {
                break;
            }
            entry.staged.clear();
        }
        if (entry.staged.empty())
        {
            return {false, "Could not allocate a unique transaction staging file."};
        }
        entry.target_relative = path_to_utf8(entry.target.lexically_relative(record.paths.root));
        entry.backup_relative = path_to_utf8(entry.backup.lexically_relative(record.paths.root));
        entry.staged_relative = path_to_utf8(entry.staged.lexically_relative(record.paths.root));
        entry.postimage = writes[index].contents;
        entry.postimage_hash = sha256_hex(entry.postimage);
        record.entries.push_back(std::move(entry));
    }
    return ensure_no_aliases(record.entries);
}

save_result write_preimages(transaction_record& record)
{
    for (std::size_t index = 0; index < record.entries.size(); ++index)
    {
        auto& entry = record.entries[index];
        if (!entry.target_existed)
        {
            continue;
        }
        std::string contents;
        auto result = read_file(entry.target, contents);
        if (!result.succeeded)
        {
            return result;
        }
        entry.preimage_relative = preimage_filename(index);
        entry.preimage = record.directory / ascii_path(entry.preimage_relative);
        entry.preimage_hash = sha256_hex(contents);
        result = write_and_flush(entry.preimage, contents);
        if (!result.succeeded)
        {
            return {false, "Could not flush a transaction pre-image: " + result.error};
        }
    }
    flush_parent_directory(record.directory);
    return {true, {}};
}

save_result write_staged_postimages(const transaction_record& record)
{
    for (const auto& entry : record.entries)
    {
        const auto written = write_and_flush(entry.staged, entry.postimage);
        if (!written.succeeded)
        {
            return {false, "Transaction staging failed: " + written.error};
        }
        flush_parent_directory(entry.staged);
    }
    return {true, {}};
}
}

save_result save_utf8_atomic(
    const std::filesystem::path& target,
    std::string_view contents,
    save_fault injected_fault)
{
    try
    {
        std::error_code filesystem_error;
        if (!target.parent_path().empty())
        {
            std::filesystem::create_directories(target.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                return {false, filesystem_error.message()};
            }
        }
        bool target_exists = false;
        bool target_regular = false;
        bool target_symlink = false;
        auto inspected = inspect_path(target, target_exists, target_regular, target_symlink);
        if (!inspected.succeeded)
        {
            return inspected;
        }
        if (target_symlink || (target_exists && !target_regular))
        {
            return {false, "Atomic save targets must be regular files and may not be symbolic links."};
        }
        const auto expected_target_state = target_exists
            ? publication_target_state::existing
            : publication_target_state::missing;
        const auto temporary = temporary_path(target);
        const auto backup = backup_path(target);
        const auto write_result = write_and_flush(temporary, contents);
        if (!write_result.succeeded)
        {
            static_cast<void>(remove_file(temporary));
            return write_result;
        }
        if (injected_fault == save_fault::after_temporary_flush)
        {
            static_cast<void>(remove_file(temporary));
            return {false, "Injected failure after temporary-file flush."};
        }
        const auto replace_result = replace_staged_file(
            target,
            temporary,
            target_exists ? std::optional{backup} : std::nullopt,
            expected_target_state);
        if (!replace_result.succeeded)
        {
            static_cast<void>(remove_file(temporary));
        }
        return replace_result;
    }
    catch (const std::exception& exception)
    {
        return {false, std::string{"Atomic save failed: "} + exception.what()};
    }
}

save_result save_utf8_transaction(
    std::span<const utf8_transaction_write> writes,
    const std::filesystem::path& recovery_root,
    transaction_save_fault injected_fault)
{
    try
    {
        if (writes.empty())
        {
            return {true, {}};
        }
        if (writes.size() > maximum_transaction_entries)
        {
            return {false, "The transaction exceeded its document limit."};
        }

        const auto pending_recovery = recover_utf8_transactions(recovery_root);
        if (!pending_recovery.succeeded)
        {
            return {false, "Could not recover pending transactions before starting a new save: "
                + pending_recovery.error};
        }

        transaction_record record;
        auto result = resolve_recovery_paths(recovery_root, true, record.paths);
        if (!result.succeeded)
        {
            return result;
        }
        result = prepare_transaction_entries(record, writes);
        if (!result.succeeded)
        {
            return result;
        }
        result = create_transaction_directory(record);
        if (!result.succeeded)
        {
            return result;
        }
        result = write_preimages(record);
        if (!result.succeeded)
        {
            static_cast<void>(remove_transaction_directory(record));
            return result;
        }

        record.phase = "staging";
        result = write_journal(record);
        if (!result.succeeded)
        {
            static_cast<void>(remove_transaction_directory(record));
            return result;
        }
        result = write_staged_postimages(record);
        if (!result.succeeded)
        {
            const auto recovered = recover_transaction(record);
            return recovered.succeeded ? result
                : save_result{false, result.error + " Recovery failed: " + recovered.error};
        }
        record.phase = "prepared";
        result = write_journal(record);
        if (!result.succeeded)
        {
            const auto recovered = recover_transaction(record);
            return recovered.succeeded ? result
                : save_result{false, result.error + " Recovery failed: " + recovered.error};
        }
        if (injected_fault == transaction_save_fault::after_staging)
        {
            const auto recovered = recover_transaction(record);
            return recovered.succeeded
                ? save_result{false, "Injected transaction failure after staging; pre-images restored."}
                : save_result{false, "Injected transaction failure after staging; recovery failed: " + recovered.error};
        }

        for (std::size_t index = 0; index < record.entries.size(); ++index)
        {
            auto& entry = record.entries[index];
            result = replace_staged_file(
                entry.target,
                entry.staged,
                entry.target_existed ? std::optional{entry.backup} : std::nullopt,
                entry.target_existed
                    ? publication_target_state::existing
                    : publication_target_state::missing);
            if (!result.succeeded)
            {
                const auto recovered = recover_transaction(record);
                return recovered.succeeded
                    ? save_result{false, "Transaction commit failed; pre-images restored: " + result.error}
                    : save_result{false, "Transaction commit and recovery failed: " + result.error + " " + recovered.error};
            }
            if (index == 0 && injected_fault == transaction_save_fault::leave_interrupted_after_first_replace)
            {
                return {false, "Injected interruption after the first replacement; durable recovery journal retained."};
            }
            if (index == 0 && injected_fault == transaction_save_fault::after_first_replace)
            {
                const auto recovered = recover_transaction(record);
                return recovered.succeeded
                    ? save_result{false, "Injected transaction failure after the first replacement; pre-images restored."}
                    : save_result{false, "Injected transaction failure after the first replacement; recovery failed: "
                        + recovered.error};
            }
        }

        record.phase = "committed";
        auto committed_journal_fault = publication_fault::none;
        if (injected_fault == transaction_save_fault::committed_journal_transient_sharing_violation)
        {
            committed_journal_fault = publication_fault::transient_sharing_violation;
        }
        else if (injected_fault
            == transaction_save_fault::committed_journal_transient_sharing_violation_then_topology_unavailable)
        {
            committed_journal_fault =
                publication_fault::transient_sharing_violation_then_topology_unavailable;
        }
        else if (injected_fault
            == transaction_save_fault::committed_journal_reported_failure_after_publication)
        {
            committed_journal_fault = publication_fault::reported_failure_after_publication;
        }
        else if (injected_fault == transaction_save_fault::committed_journal_persistent_sharing_violation
            || injected_fault
                == transaction_save_fault::committed_journal_persistent_sharing_violation_then_transient_recovery_read)
        {
            committed_journal_fault = publication_fault::persistent_sharing_violation;
        }
        result = write_journal(record, committed_journal_fault);
        if (!result.succeeded)
        {
            transaction_record durable_record;
            const auto loaded = load_journal(record.paths, record.directory, durable_record);
            if (!loaded.succeeded)
            {
                return {
                    false,
                    "The committed marker could not be flushed and its durable state could not be inspected; "
                    "no recovery changes were made: "
                        + result.error + " " + loaded.error};
            }
            if (durable_record.phase == "committed")
            {
                static_cast<void>(recover_transaction(durable_record));
                return {true, {}};
            }
            if (durable_record.phase != "prepared")
            {
                return {
                    false,
                    "The committed marker could not be flushed and the durable journal was not prepared; "
                    "no recovery changes were made: "
                        + result.error};
            }
            const auto recovered = recover_transaction(
                durable_record,
                injected_fault
                        == transaction_save_fault::committed_journal_persistent_sharing_violation_then_transient_recovery_read
                    ? recovery_fault::transient_preimage_open_failure
                    : recovery_fault::none);
            return recovered.succeeded
                ? save_result{false, "The committed marker could not be flushed; pre-images restored: "
                    + result.error}
                : save_result{false, "The committed marker and recovery both failed: " + result.error + " "
                    + recovered.error};
        }
        if (injected_fault == transaction_save_fault::leave_interrupted_after_committed_journal)
        {
            return {
                false,
                "Injected interruption after the committed journal became durable; cleanup artifacts retained."};
        }
        static_cast<void>(cleanup_staged_files(record));
        // The committed marker makes orphan cleanup deterministic. Failure to
        // delete bounded artifacts does not turn an already committed save into
        // a rollback; recover_utf8_transactions will remove them at startup.
        static_cast<void>(remove_transaction_directory(record));
        return {true, {}};
    }
    catch (const std::exception& exception)
    {
        return {false, std::string{"Transaction save failed: "} + exception.what()};
    }
}

save_result recover_utf8_transactions(const std::filesystem::path& recovery_root)
{
    try
    {
        recovery_paths paths;
        auto result = resolve_recovery_paths(recovery_root, false, paths);
        if (!result.succeeded || !paths.transaction_base_exists)
        {
            return result;
        }
        std::error_code error;
        std::vector<std::filesystem::path> transactions;
        for (std::filesystem::directory_iterator iterator{paths.transaction_base, error}, end;
             !error && iterator != end; iterator.increment(error))
        {
            const auto status = iterator->symlink_status(error);
            if (error)
            {
                break;
            }
            if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
            {
                return {false, "The transaction recovery directory contained an unsafe artifact."};
            }
            transactions.push_back(iterator->path());
        }
        if (error)
        {
            return {false, "Could not enumerate transaction recovery artifacts: " + error.message()};
        }
        std::sort(transactions.begin(), transactions.end());
        for (const auto& transaction : transactions)
        {
            result = recover_transaction_directory(paths, transaction);
            if (!result.succeeded)
            {
                return result;
            }
        }
        return {true, {}};
    }
    catch (const std::exception& exception)
    {
        return {false, std::string{"Transaction recovery failed: "} + exception.what()};
    }
}

save_result recover_backup(const std::filesystem::path& target)
{
    try
    {
        bool target_exists = false;
        auto inspected = filesystem_exists(target, target_exists);
        if (!inspected.succeeded || target_exists)
        {
            return inspected;
        }
        const auto backup = backup_path(target);
        bool backup_exists = false;
        inspected = filesystem_exists(backup, backup_exists);
        if (!inspected.succeeded)
        {
            return inspected;
        }
        if (!backup_exists)
        {
            return {false, "Neither target nor backup exists."};
        }
        std::error_code error;
        std::filesystem::rename(backup, target, error);
        if (!error)
        {
            flush_parent_directory(target);
        }
        return error ? save_result{false, error.message()} : save_result{true, {}};
    }
    catch (const std::exception& exception)
    {
        return {false, std::string{"Backup recovery failed: "} + exception.what()};
    }
}
}
