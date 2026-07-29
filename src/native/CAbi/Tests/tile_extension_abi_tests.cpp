#include <dragonpixel/cabi/tile_extension_plugin_v1.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
int32_t DPE_TILE_EXTENSION_PLUGIN_CALL evaluate(
    const dpe_tile_extension_context_v1* contexts,
    size_t context_count,
    dpe_tile_extension_result_v1* results,
    size_t result_count)
{
    if (contexts == nullptr || results == nullptr || context_count != 1 || result_count != 1
        || contexts[0].struct_size != sizeof(dpe_tile_extension_context_v1))
    {
        return -1;
    }
    static constexpr char json[] = R"({"sprite":"ok"})";
    results[0] = {static_cast<uint32_t>(sizeof(dpe_tile_extension_result_v1)), 0,
        {json, sizeof(json) - 1}, {nullptr, 0}, {nullptr, 0}};
    return 0;
}

int32_t DPE_TILE_EXTENSION_PLUGIN_CALL propose(
    const dpe_tile_extension_context_v1* context,
    dpe_tile_extension_string_v1 request,
    dpe_tile_extension_result_v1* result)
{
    if (context == nullptr || result == nullptr || request.data == nullptr) return -1;
    static constexpr char json[] = R"({"commands":[]})";
    *result = {static_cast<uint32_t>(sizeof(dpe_tile_extension_result_v1)), 0,
        {json, sizeof(json) - 1}, {nullptr, 0}, {nullptr, 0}};
    return 0;
}

void DPE_TILE_EXTENSION_PLUGIN_CALL release_value(dpe_tile_extension_string_v1) {}
}

int main()
{
    try
    {
        static constexpr char plugin_id[] = "example.tile-extension";
        const dpe_tile_extension_plugin_v1 api{
            DPE_TILE_EXTENSION_ABI_V1,
            static_cast<uint32_t>(sizeof(dpe_tile_extension_plugin_v1)),
            DPE_TILE_EXTENSION_CAPABILITY_EVALUATE_TILES_V1
                | DPE_TILE_EXTENSION_CAPABILITY_PROPOSE_BRUSH_V1,
            0,
            {plugin_id, sizeof(plugin_id) - 1},
            &evaluate,
            &propose,
            &release_value};
        if (api.abi_version != 1 || api.struct_size != sizeof(api)
            || api.plugin_id.length != std::strlen(plugin_id))
        {
            throw std::runtime_error{"Tile extension ABI identity/size contract failed."};
        }
        dpe_tile_extension_context_v1 context{};
        context.struct_size = sizeof(context);
        dpe_tile_extension_result_v1 result{};
        if (api.evaluate_tiles(&context, 1, &result, 1) != 0 || result.status != 0
            || result.result_json.length == 0)
        {
            throw std::runtime_error{"Tile extension batch evaluation contract failed."};
        }
        static constexpr char request[] = "{}";
        if (api.propose_brush(&context, {request, sizeof(request) - 1}, &result) != 0
            || result.status != 0)
        {
            throw std::runtime_error{"Tile extension brush proposal contract failed."};
        }
        std::cout << "Tile extension ABI tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
