# Include this file after DragonPixelEditorLibrary is declared. Keeping the
# service/test registration here preserves the bounded service test targets.

target_sources(DragonPixelEditorLibrary PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectLifecycleService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectLifecycleService.h"
    "${CMAKE_CURRENT_LIST_DIR}/AssetService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/AssetService.h"
    "${CMAKE_CURRENT_LIST_DIR}/SelectionService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SelectionService.h"
)
target_compile_definitions(DragonPixelEditorLibrary PRIVATE
    DPE_PROJECT_TEMPLATES_ROOT="${CMAKE_SOURCE_DIR}/templates")

add_executable(DragonPixelProjectIndexServiceTests
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexServiceTests.cpp"
)
target_link_libraries(DragonPixelProjectIndexServiceTests PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(DragonPixelProjectIndexServiceTests PRIVATE
    DPE_DEFAULT_SAMPLE_PROJECT="${CMAKE_SOURCE_DIR}/samples/Slice1Sample/DragonPixelProject.json")
set_target_properties(DragonPixelProjectIndexServiceTests PROPERTIES AUTOMOC ON)
dpe_configure_native_target(DragonPixelProjectIndexServiceTests)

add_test(NAME s2.project_index_service COMMAND DragonPixelProjectIndexServiceTests)
set_tests_properties(s2.project_index_service PROPERTIES TIMEOUT 30)

add_executable(DragonPixelProjectLifecycleServiceTests
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectLifecycleService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectLifecycleService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectLifecycleServiceTests.cpp"
)
target_link_libraries(DragonPixelProjectLifecycleServiceTests PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(DragonPixelProjectLifecycleServiceTests PRIVATE
    DPE_PROJECT_TEMPLATES_ROOT="${CMAKE_SOURCE_DIR}/templates")
set_target_properties(DragonPixelProjectLifecycleServiceTests PROPERTIES AUTOMOC ON)
dpe_configure_native_target(DragonPixelProjectLifecycleServiceTests)
add_test(NAME s3.project_lifecycle_service COMMAND DragonPixelProjectLifecycleServiceTests)
add_test(NAME poc_m.project_creation_recovery COMMAND DragonPixelProjectLifecycleServiceTests)
set_tests_properties(
    s3.project_lifecycle_service
    poc_m.project_creation_recovery
    PROPERTIES TIMEOUT 30)

add_executable(DragonPixelAssetServiceTests
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectLifecycleService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectLifecycleService.h"
    "${CMAKE_CURRENT_LIST_DIR}/AssetService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/AssetService.h"
    "${CMAKE_CURRENT_LIST_DIR}/AssetServiceTests.cpp"
)
target_link_libraries(DragonPixelAssetServiceTests PRIVATE
    DragonPixel::Tiles Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(DragonPixelAssetServiceTests PRIVATE
    DPE_PROJECT_TEMPLATES_ROOT="${CMAKE_SOURCE_DIR}/templates")
set_target_properties(DragonPixelAssetServiceTests PROPERTIES AUTOMOC ON)
dpe_configure_native_target(DragonPixelAssetServiceTests)
add_test(NAME s3.asset_service COMMAND DragonPixelAssetServiceTests)
add_test(NAME poc_o.asset_import_cache_integrity COMMAND DragonPixelAssetServiceTests)
set_tests_properties(
    s3.asset_service
    poc_o.asset_import_cache_integrity
    PROPERTIES TIMEOUT 30)

if(WIN32)
    if(NOT DPE_EDITOR_QT_BIN_DIRECTORY)
        get_target_property(DPE_PROJECT_INDEX_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(
            DPE_EDITOR_QT_BIN_DIRECTORY
            "${DPE_PROJECT_INDEX_QMAKE_EXECUTABLE}"
            DIRECTORY)
    endif()
    set_tests_properties(
        s2.project_index_service
        s3.project_lifecycle_service
        poc_m.project_creation_recovery
        s3.asset_service
        poc_o.asset_import_cache_integrity
        PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_EDITOR_QT_BIN_DIRECTORY}")
endif()
