add_library(DragonPixelCAbi SHARED
    src/dpe_api_v1.cpp
)
target_include_directories(DragonPixelCAbi PUBLIC include)
target_compile_definitions(DragonPixelCAbi PRIVATE DPE_API_BUILD)
target_compile_features(DragonPixelCAbi PUBLIC cxx_std_20)
target_link_libraries(DragonPixelCAbi PRIVATE DragonPixel::Physics)
set_target_properties(DragonPixelCAbi PROPERTIES
    OUTPUT_NAME dragonpixel
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
dpe_configure_native_target(DragonPixelCAbi)
add_library(DragonPixel::CAbi ALIAS DragonPixelCAbi)

if(BUILD_TESTING)
    add_library(dpe_component_runtime_test_plugin SHARED Tests/component_plugin_test.cpp)
    target_include_directories(dpe_component_runtime_test_plugin PRIVATE include)
    target_compile_definitions(dpe_component_runtime_test_plugin PRIVATE
        DPE_COMPONENT_PLUGIN_BUILD
        DPE_TILE_EXTENSION_PLUGIN_BUILD
    )
    set_target_properties(dpe_component_runtime_test_plugin PROPERTIES
        OUTPUT_NAME dpe_component_runtime_test
        CXX_VISIBILITY_PRESET hidden
    )
    dpe_configure_native_target(dpe_component_runtime_test_plugin)

    add_library(dpe_component_runtime_v1_test_plugin SHARED Tests/component_plugin_v1_legacy_test.cpp)
    target_include_directories(dpe_component_runtime_v1_test_plugin PRIVATE include)
    target_compile_definitions(dpe_component_runtime_v1_test_plugin PRIVATE DPE_COMPONENT_PLUGIN_BUILD)
    set_target_properties(dpe_component_runtime_v1_test_plugin PROPERTIES
        OUTPUT_NAME dpe_component_runtime_v1_test
        CXX_VISIBILITY_PRESET hidden
    )
    dpe_configure_native_target(dpe_component_runtime_v1_test_plugin)

    add_executable(dpe_physics_abi_tests Tests/physics_api_tests.cpp)
    target_link_libraries(dpe_physics_abi_tests PRIVATE DragonPixel::CAbi)
    dpe_configure_native_target(dpe_physics_abi_tests)
    add_test(NAME s2.physics_abi COMMAND dpe_physics_abi_tests)
    add_test(NAME poc_g.native_abi COMMAND dpe_physics_abi_tests)
    set_tests_properties(s2.physics_abi poc_g.native_abi PROPERTIES TIMEOUT 60)

    add_executable(dpe_tile_extension_abi_tests Tests/tile_extension_abi_tests.cpp)
    target_include_directories(dpe_tile_extension_abi_tests PRIVATE include)
    dpe_configure_native_target(dpe_tile_extension_abi_tests)
    add_test(NAME s2.tile_extension_abi COMMAND dpe_tile_extension_abi_tests)
    add_test(NAME poc_p.tile_extension_abi COMMAND dpe_tile_extension_abi_tests)
    set_tests_properties(s2.tile_extension_abi poc_p.tile_extension_abi PROPERTIES TIMEOUT 30)
endif()
