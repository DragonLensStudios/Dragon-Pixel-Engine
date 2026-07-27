#include <Windows.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::wstring environment_value(const wchar_t* name) {
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    const std::wstring result{value};
    std::free(value);
    return result;
}

std::wstring executable_path() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::wstring{buffer.data(), length};
        }
        buffer.resize(buffer.size() * 2);
    }
}

int sleep_duration_ms() {
    const std::wstring text = environment_value(L"DPE_LAUNCH_PROBE_SLEEP_MS");
    if (text.empty()) {
        return 500;
    }

    wchar_t* end = nullptr;
    errno = 0;
    const long value = std::wcstol(text.c_str(), &end, 10);
    const bool complete = end == text.c_str() + text.size();
    return errno == 0 && complete && value >= 0 && value <= INT_MAX ? static_cast<int>(value) : 500;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    const std::wstring expected_executable = environment_value(L"DPE_LAUNCH_PROBE_EXPECTED_EXECUTABLE");
    const std::wstring expected_project = environment_value(L"DPE_LAUNCH_PROBE_EXPECTED_PROJECT");
    const std::wstring marker = environment_value(L"DPE_LAUNCH_PROBE_MARKER");
    if (expected_executable.empty() || expected_project.empty() || marker.empty()) {
        return 20;
    }
    if (executable_path() != expected_executable) {
        return 21;
    }
    if (argc != 3 || std::wstring_view{argv[1]} != L"--project") {
        return 22;
    }
    if (std::wstring_view{argv[2]} != expected_project) {
        return 23;
    }

    std::ofstream output(std::filesystem::path{marker}, std::ios::binary | std::ios::trunc);
    if (!output) {
        return 24;
    }
    output << "argument-preservation-ok\n";
    output.close();
    if (!output) {
        return 24;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{sleep_duration_ms()});
    return 0;
}
