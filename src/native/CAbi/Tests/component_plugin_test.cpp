#include <dragonpixel/cabi/component_plugin_v1.h>

#include <new>
#include <string>

namespace
{
constexpr auto type_id = "ce9c24d8-278e-457d-b733-3671c85f6a45";

struct test_component final
{
    std::string properties;
    std::string error;
    dpe_component_diagnostic_fn diagnostic{};
    void* diagnostic_user{};
    bool property_initialization_failed{};
    bool fail_late_update{};
};

dpe_component_handle DPE_COMPONENT_PLUGIN_CALL create(
    const char*,
    dpe_component_diagnostic_fn diagnostic,
    void* user)
{
    auto* value = new (std::nothrow) test_component;
    if (value != nullptr && diagnostic != nullptr)
    {
        value->diagnostic = diagnostic;
        value->diagnostic_user = user;
        constexpr char message[] = "Native runtime test component initialized.";
        diagnostic(user, 0, message, sizeof(message) - 1);
    }
    return value;
}

void DPE_COMPONENT_PLUGIN_CALL destroy(dpe_component_handle handle)
{
    auto* value = static_cast<test_component*>(handle);
    if (value != nullptr && value->diagnostic != nullptr)
    {
        if (value->property_initialization_failed)
        {
            constexpr char message[] =
                "Native runtime test component destroyed after set-properties failure.";
            value->diagnostic(value->diagnostic_user, 0, message, sizeof(message) - 1);
        }
        else
        {
            constexpr char message[] = "Native runtime test component destroyed.";
            value->diagnostic(value->diagnostic_user, 0, message, sizeof(message) - 1);
        }
    }
    delete value;
}

int32_t DPE_COMPONENT_PLUGIN_CALL set_properties(
    dpe_component_handle handle,
    const char* json,
    size_t length)
{
    if (handle == nullptr || json == nullptr) return -1;
    auto* value = static_cast<test_component*>(handle);
    value->properties.assign(json, length);
    if (value->properties.find("\"failSetProperties\":true") != std::string::npos)
    {
        value->property_initialization_failed = true;
        value->error = "injected native set-properties failure";
        return -1;
    }
    value->fail_late_update =
        value->properties.find("\"failLateUpdate\":true") != std::string::npos;
    return 0;
}

int32_t DPE_COMPONENT_PLUGIN_CALL update(
    dpe_component_handle handle,
    const dpe_component_update_v1* value)
{
    return handle == nullptr || value == nullptr ? -1 : 0;
}

void report(test_component* value, const char* message, size_t length)
{
    if (value != nullptr && value->diagnostic != nullptr)
    {
        value->diagnostic(value->diagnostic_user, 0, message, length);
    }
}

int32_t DPE_COMPONENT_PLUGIN_CALL dispatch_lifecycle(
    dpe_component_handle handle,
    dpe_component_lifecycle_phase_v2 phase,
    const dpe_component_update_v1* update_value)
{
    auto* value = static_cast<test_component*>(handle);
    if (value == nullptr) return -1;
    switch (phase)
    {
        case DPE_COMPONENT_LIFECYCLE_ENABLE_V2:
        {
            constexpr char message[] = "Native runtime test component enabled.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_FIXED_UPDATE_V2:
        {
            if (update_value == nullptr) return -1;
            constexpr char message[] = "Native runtime test component fixed update.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_VARIABLE_UPDATE_V2:
        {
            if (update_value == nullptr) return -1;
            constexpr char message[] = "Native runtime test component variable update.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_LATE_UPDATE_V2:
        {
            if (update_value == nullptr) return -1;
            if (value->fail_late_update)
            {
                value->error = "injected native late-update failure";
                return -1;
            }
            constexpr char message[] = "Native runtime test component late update.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_RENDER_SUBMISSION_V2:
        {
            if (update_value == nullptr) return -1;
            constexpr char message[] = "Native runtime test component render submission.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
        case DPE_COMPONENT_LIFECYCLE_DISABLE_V2:
        {
            constexpr char message[] = "Native runtime test component disabled.";
            report(value, message, sizeof(message) - 1);
            return 0;
        }
    }
    value->error = "invalid lifecycle phase";
    return -1;
}

const char* DPE_COMPONENT_PLUGIN_CALL last_error(dpe_component_handle handle)
{
    if (handle == nullptr) return "null test component";
    return static_cast<test_component*>(handle)->error.c_str();
}

const dpe_component_plugin_v1 api{1U, type_id, &create, &destroy, &set_properties, &update, &last_error};
const dpe_component_plugin_v2 api_v2{
    2U,
    static_cast<uint32_t>(sizeof(dpe_component_plugin_v2)),
    type_id,
    &create,
    &destroy,
    &set_properties,
    &dispatch_lifecycle,
    &last_error};
}

extern "C" DPE_COMPONENT_PLUGIN_EXPORT const dpe_component_plugin_v1* DPE_COMPONENT_PLUGIN_CALL
dpe_component_plugin_get_v1(void)
{
    return &api;
}

extern "C" DPE_COMPONENT_PLUGIN_EXPORT const dpe_component_plugin_v2* DPE_COMPONENT_PLUGIN_CALL
dpe_component_plugin_get_v2(void)
{
    return &api_v2;
}
