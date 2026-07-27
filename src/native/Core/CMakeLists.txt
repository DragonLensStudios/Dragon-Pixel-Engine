add_library(DragonPixelCore STATIC
    src/uuid.cpp
)
target_include_directories(DragonPixelCore PUBLIC include)
target_compile_features(DragonPixelCore PUBLIC cxx_std_20)
dpe_configure_native_target(DragonPixelCore)
add_library(DragonPixel::Core ALIAS DragonPixelCore)
