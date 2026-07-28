add_library(DragonPixelTiles STATIC
    src/tile_documents.cpp
    src/tile_grid.cpp
)
target_include_directories(DragonPixelTiles PUBLIC include)
target_link_libraries(DragonPixelTiles PUBLIC DragonPixel::Core PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(DragonPixelTiles PUBLIC cxx_std_20)
dpe_configure_native_target(DragonPixelTiles)
add_library(DragonPixel::Tiles ALIAS DragonPixelTiles)

if(BUILD_TESTING)
    add_executable(dpe_tile_tests Tests/tile_document_tests.cpp)
    target_link_libraries(dpe_tile_tests PRIVATE DragonPixel::Tiles nlohmann_json::nlohmann_json)
    dpe_configure_native_target(dpe_tile_tests)
    add_test(NAME s2.tile_documents COMMAND dpe_tile_tests)
    add_test(NAME poc_k.tile_documents COMMAND dpe_tile_tests)
    set_tests_properties(s2.tile_documents poc_k.tile_documents PROPERTIES TIMEOUT 30)
endif()
