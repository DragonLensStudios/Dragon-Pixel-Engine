add_library(DragonPixelSerialization STATIC
    src/scene_json.cpp
    src/atomic_file.cpp
)
target_include_directories(DragonPixelSerialization PUBLIC include)
target_link_libraries(DragonPixelSerialization
    PUBLIC
        DragonPixel::Core
        DragonPixel::Metadata
        DragonPixel::Scene
        nlohmann_json::nlohmann_json
)
target_compile_features(DragonPixelSerialization PUBLIC cxx_std_20)
dpe_configure_native_target(DragonPixelSerialization)
add_library(DragonPixel::Serialization ALIAS DragonPixelSerialization)
