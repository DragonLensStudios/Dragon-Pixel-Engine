#include <dragonpixel/serialization/atomic_file.h>

#include <dragonpixel/core/uuid.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dragonpixel::serialization
{
namespace
{
std::filesystem::path temporary_path(const std::filesystem::path& target)
{
    return target.parent_path() / (target.filename().string() + ".tmp-" + core::uuid::random_v4().to_string());
}

std::filesystem::path backup_path(const std::filesystem::path& target)
{
    return target.parent_path() / (target.filename().string() + ".bak");
}

#if defined(_WIN32)
save_result write_and_flush(const std::filesystem::path& path, std::string_view contents)
{
    const auto handle = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return {false, "CreateFileW failed with error " + std::to_string(GetLastError())};
    }

    std::size_t offset = 0;
    while (offset < contents.size())
    {
        const auto remaining = std::min<std::size_t>(contents.size() - offset, MAXDWORD);
        DWORD written = 0;
        if (!WriteFile(handle, contents.data() + offset, static_cast<DWORD>(remaining), &written, nullptr) || written == 0)
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
}

save_result save_utf8_atomic(
    const std::filesystem::path& target,
    std::string_view contents,
    save_fault injected_fault)
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

    const auto temporary = temporary_path(target);
    const auto backup = backup_path(target);
    const auto write_result = write_and_flush(temporary, contents);
    if (!write_result.succeeded)
    {
        std::filesystem::remove(temporary, filesystem_error);
        return write_result;
    }
    if (injected_fault == save_fault::after_temporary_flush)
    {
        std::filesystem::remove(temporary, filesystem_error);
        return {false, "Injected failure after temporary-file flush."};
    }

#if defined(_WIN32)
    BOOL replaced = FALSE;
    if (std::filesystem::exists(target))
    {
        std::filesystem::remove(backup, filesystem_error);
        replaced = ReplaceFileW(
            target.c_str(), temporary.c_str(), backup.c_str(), REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
    }
    else
    {
        replaced = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    }
    if (!replaced)
    {
        const auto error = GetLastError();
        std::filesystem::remove(temporary, filesystem_error);
        return {false, "Atomic replace failed with error " + std::to_string(error)};
    }
#else
    if (std::filesystem::exists(target))
    {
        std::filesystem::remove(backup, filesystem_error);
        if (::link(target.c_str(), backup.c_str()) != 0)
        {
            std::filesystem::remove(temporary, filesystem_error);
            return {false, std::strerror(errno)};
        }
    }
    if (::rename(temporary.c_str(), target.c_str()) != 0)
    {
        const auto message = std::string{std::strerror(errno)};
        std::filesystem::remove(temporary, filesystem_error);
        return {false, message};
    }
    flush_parent_directory(target);
#endif
    return {true, {}};
}

save_result recover_backup(const std::filesystem::path& target)
{
    if (std::filesystem::exists(target))
    {
        return {true, {}};
    }
    const auto backup = backup_path(target);
    if (!std::filesystem::exists(backup))
    {
        return {false, "Neither target nor backup exists."};
    }
    std::error_code error;
    std::filesystem::rename(backup, target, error);
    return error ? save_result{false, error.message()} : save_result{true, {}};
}
}
