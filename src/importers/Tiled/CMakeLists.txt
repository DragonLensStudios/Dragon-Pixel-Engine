add_library(DragonPixelTiledImporter STATIC
    TiledJsonImporter.cpp
    TiledJsonImporter.h
)
target_include_directories(DragonPixelTiledImporter PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(DragonPixelTiledImporter
    PUBLIC
        DragonPixel::Core
        DragonPixel::Tiles
    PRIVATE
        Qt6::Core
        nlohmann_json::nlohmann_json
)
target_compile_features(DragonPixelTiledImporter PUBLIC cxx_std_20)
dpe_configure_native_target(DragonPixelTiledImporter)

add_executable(DragonPixelTiledImporterWorker
    main.cpp
)
target_link_libraries(DragonPixelTiledImporterWorker PRIVATE DragonPixelTiledImporter)
dpe_configure_native_target(DragonPixelTiledImporterWorker)

if(BUILD_TESTING)
    add_executable(DragonPixelTiledImporterTests
        TiledJsonImporterTests.cpp
    )
    target_link_libraries(DragonPixelTiledImporterTests
        PRIVATE
            DragonPixelTiledImporter
            nlohmann_json::nlohmann_json
    )
    dpe_configure_native_target(DragonPixelTiledImporterTests)
    add_test(NAME s2.tiled_tilemap_importer COMMAND DragonPixelTiledImporterTests)
    add_test(NAME poc_k.tiled_tilemap_importer COMMAND DragonPixelTiledImporterTests)
    set_tests_properties(
        s2.tiled_tilemap_importer
        poc_k.tiled_tilemap_importer
        PROPERTIES
            TIMEOUT 30
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
