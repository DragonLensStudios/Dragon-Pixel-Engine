#include <dragonpixel/cabi/component_plugin_v1.h>

#include <new>
#include <string>

namespace
{
constexpr auto type_id = "858ee1db-6c57-4bc2-b245-e22cc16cdbbd";

struct legacy_component final
{
    std::string error;
    dpe_component_diagnostic_fn diagnostic{};
    void* diagnostic_user{};
};

void report(legacy_component* value, const char* message, size_t length)
{
    if (value != nullptr && value->diagnostic != nullptr)
    {
        value->diagnostic(value->diagnostic_user, 0, message, length);
    }
}

dpe_component_handle DPE_COMPONENT_PLUGIN_CALL create(
    const char*,
    dpe_component_diagnostic_fn diagnostic,
    void* user)
{
    auto* value = new (std::nothrow) legacy_component;
    if (value != nullptr)
    {
        value->diagnostic = diagnostic;
        value->diagnostic_user = user;
        constexpr char message[] = "Legacy native v1 component initialized.";
        report(value, message, sizeof(message) - 1);
    }
    return value;
}

void DPE_COMPONENT_PLUGIN_CALL destroy(dpe_component_handle handle)
{
    auto* value = static_cast<legacy_component*>(handle);
    constexpr char message[] = "Legacy native v1 component destroyed.";
    report(value, message, sizeof(message) - 1);
    delete value;
}

int32_t DPE_COMPONENT_PLUGIN_CALL set_properties(
    dpe_component_handle handle,
    const char*,
    size_t)
{
    return handle == nullptr ? -1 : 0;
}

int32_t DPE_COMPONENT_PLUGIN_CALL update(
    dpe_component_handle handle,
    const dpe_component_update_v1* value)
{
    auto* component = static_cast<legacy_component*>(handle);
    if (component == nullptr || value == nullptr) return -1;
    constexpr char message[] = "Legacy native v1 component updated.";
    report(component, message, sizeof(message) - 1);
    return 0;
}

const char* DPE_COMPONENT_PLUGIN_CALL last_error(dpe_component_handle handle)
{
    if (handle == nullptr) return "null legacy component";
    return static_cast<legacy_component*>(handle)->error.c_str();
}

const dpe_component_plugin_v1 api{
    1U,
    type_id,
    &create,
    &destroy,
    &set_properties,
    &update,
    &last_error};
}

extern "C" DPE_COMPONENT_PLUGIN_EXPORT const dpe_component_plugin_v1* DPE_COMPONENT_PLUGIN_CALL
dpe_component_plugin_get_v1(void)
{
    return &api;
}
