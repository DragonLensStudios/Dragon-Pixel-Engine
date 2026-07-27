# Include after DragonPixelEditorLibrary is declared. This keeps the
# asynchronous preview service registration and focused tests together.

target_sources(DragonPixelEditorLibrary PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/AssetPreviewService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/AssetPreviewService.h"
)

add_executable(DragonPixelAssetPreviewServiceTests
    "${CMAKE_CURRENT_LIST_DIR}/AssetPreviewService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/AssetPreviewService.h"
    "${CMAKE_CURRENT_LIST_DIR}/AssetPreviewServiceTests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
)
target_include_directories(DragonPixelAssetPreviewServiceTests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(DragonPixelAssetPreviewServiceTests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test)
set_target_properties(DragonPixelAssetPreviewServiceTests PROPERTIES AUTOMOC ON)
dpe_configure_native_target(DragonPixelAssetPreviewServiceTests)

add_test(NAME s2.asset_preview_service COMMAND DragonPixelAssetPreviewServiceTests)
set_tests_properties(s2.asset_preview_service PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(WIN32)
    if(NOT DPE_EDITOR_QT_BIN_DIRECTORY)
        get_target_property(DPE_ASSET_PREVIEW_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(
            DPE_EDITOR_QT_BIN_DIRECTORY
            "${DPE_ASSET_PREVIEW_QMAKE_EXECUTABLE}"
            DIRECTORY)
    endif()
    set_tests_properties(s2.asset_preview_service PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_EDITOR_QT_BIN_DIRECTORY}")
endif()
