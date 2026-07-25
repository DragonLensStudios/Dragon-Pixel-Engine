add_library(DragonPixelMetadata STATIC
    src/registry.cpp
)
target_include_directories(DragonPixelMetadata PUBLIC include)
target_link_libraries(DragonPixelMetadata PUBLIC DragonPixel::Core)
target_compile_features(DragonPixelMetadata PUBLIC cxx_std_20)
dpe_configure_native_target(DragonPixelMetadata)
add_library(DragonPixel::Metadata ALIAS DragonPixelMetadata)
