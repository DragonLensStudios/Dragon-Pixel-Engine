#pragma once

#include <string>

namespace dragonpixel::core
{
enum class diagnostic_severity
{
    info,
    warning,
    error,
};

struct diagnostic final
{
    diagnostic_severity severity{diagnostic_severity::info};
    std::string code;
    std::string message;
    std::string context;
};
}
