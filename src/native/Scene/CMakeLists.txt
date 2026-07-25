add_library(DragonPixelScene STATIC
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
