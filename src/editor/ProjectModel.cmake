add_executable(DragonPixelProjectModelTests
    "${CMAKE_CURRENT_LIST_DIR}/EditorModels.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EditorModels.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectModelTests.cpp"
)
target_include_directories(DragonPixelProjectModelTests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(DragonPixelProjectModelTests PRIVATE
    DragonPixel::Core
    DragonPixel::Metadata
    DragonPixel::Scene
    Qt6::Widgets
    Qt6::Test)
set_target_properties(DragonPixelProjectModelTests PROPERTIES AUTOMOC ON)
dpe_configure_native_target(DragonPixelProjectModelTests)

add_test(NAME s2.project_model_candidate COMMAND DragonPixelProjectModelTests)
set_tests_properties(s2.project_model_candidate PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(WIN32)
    if(NOT DPE_EDITOR_QT_BIN_DIRECTORY)
        get_target_property(DPE_PROJECT_MODEL_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(
            DPE_EDITOR_QT_BIN_DIRECTORY
            "${DPE_PROJECT_MODEL_QMAKE_EXECUTABLE}"
            DIRECTORY)
    endif()
    set_tests_properties(s2.project_model_candidate PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_EDITOR_QT_BIN_DIRECTORY}")
endif()
