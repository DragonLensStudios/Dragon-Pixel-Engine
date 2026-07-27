add_library(DragonPixelScene STATIC
    src/command_validation.cpp
    src/scene.cpp
    src/world.cpp
)
target_include_directories(DragonPixelScene PUBLIC include)
target_link_libraries(DragonPixelScene
    PUBLIC
        DragonPixel::Core
        DragonPixel::Metadata
        nlohmann_json::nlohmann_json
)
target_compile_features(DragonPixelScene PUBLIC cxx_std_20)
dpe_configure_native_target(DragonPixelScene)
add_library(DragonPixel::Scene ALIAS DragonPixelScene)

add_executable(dpe_scene_command_validation_tests
    Tests/command_validation_tests.cpp
)
target_link_libraries(dpe_scene_command_validation_tests PRIVATE
    DragonPixel::Core
    DragonPixel::Metadata
    DragonPixel::Scene
    nlohmann_json::nlohmann_json
)
dpe_configure_native_target(dpe_scene_command_validation_tests)

add_test(
    NAME s2.command_validation
    COMMAND dpe_scene_command_validation_tests
)
set_tests_properties(s2.command_validation PROPERTIES TIMEOUT 30)
