add_executable(DragonPixelConsoleModelTests
    "${CMAKE_CURRENT_LIST_DIR}/EditorModels.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/EditorModels.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ConsoleModelTests.cpp"
)
target_include_directories(DragonPixelConsoleModelTests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(DragonPixelConsoleModelTests PRIVATE
    DragonPixel::Core
    DragonPixel::Metadata
    DragonPixel::Scene
    Qt6::Widgets
    Qt6::Test)
set_target_properties(DragonPixelConsoleModelTests PROPERTIES AUTOMOC ON)
dpe_configure_native_target(DragonPixelConsoleModelTests)

add_test(NAME s2.console_model_structured COMMAND DragonPixelConsoleModelTests)
set_tests_properties(s2.console_model_structured PROPERTIES TIMEOUT 30)

if(WIN32)
    if(NOT DPE_EDITOR_QT_BIN_DIRECTORY)
        get_target_property(DPE_CONSOLE_MODEL_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(
            DPE_EDITOR_QT_BIN_DIRECTORY
            "${DPE_CONSOLE_MODEL_QMAKE_EXECUTABLE}"
            DIRECTORY)
    endif()
    set_tests_properties(s2.console_model_structured PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_EDITOR_QT_BIN_DIRECTORY}")
endif()
