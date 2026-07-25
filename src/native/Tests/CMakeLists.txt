add_executable(dpe_s1_native_tests
    scene_serialization_tests.cpp
)
target_link_libraries(dpe_s1_native_tests PRIVATE
    DragonPixel::Core
    DragonPixel::Metadata
    DragonPixel::Scene
    DragonPixel::Serialization
    nlohmann_json::nlohmann_json
)
dpe_configure_native_target(dpe_s1_native_tests)

add_test(
    NAME s1.native_core
    COMMAND dpe_s1_native_tests ${CMAKE_BINARY_DIR}/s1-native-tests
)
set_tests_properties(s1.native_core PROPERTIES TIMEOUT 30)
