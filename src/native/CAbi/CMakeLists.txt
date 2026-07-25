add_library(DragonPixelCAbi SHARED
    src/dpe_api_v1.cpp
)
target_include_directories(DragonPixelCAbi PUBLIC include)
target_compile_definitions(DragonPixelCAbi PRIVATE DPE_API_BUILD)
target_compile_features(DragonPixelCAbi PUBLIC cxx_std_20)
set_target_properties(DragonPixelCAbi PROPERTIES
    OUTPUT_NAME dragonpixel
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
dpe_configure_native_target(DragonPixelCAbi)
add_library(DragonPixel::CAbi ALIAS DragonPixelCAbi)
