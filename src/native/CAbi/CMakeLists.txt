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
    add_executable(dpe_physics_abi_tests Tests/physics_api_tests.cpp)
    target_link_libraries(dpe_physics_abi_tests PRIVATE DragonPixel::CAbi)
    dpe_configure_native_target(dpe_physics_abi_tests)
    add_test(NAME s2.physics_abi COMMAND dpe_physics_abi_tests)
    add_test(NAME poc_g.native_abi COMMAND dpe_physics_abi_tests)
    set_tests_properties(s2.physics_abi poc_g.native_abi PROPERTIES TIMEOUT 60)
endif()
