# Include this file after DragonPixelEditor and DragonPixelEditorInteractionTests
# are declared. Keeping the service/test registration here allows the bounded
# implementation to land without rewriting the editor's main CMake file.

target_sources(DragonPixelEditor PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
)
target_sources(DragonPixelEditorInteractionTests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
)

add_executable(DragonPixelProjectIndexServiceTests
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.h"
    "${CMAKE_CURRENT_LIST_DIR}/ProjectIndexServiceTests.cpp"
)
target_link_libraries(DragonPixelProjectIndexServiceTests PRIVATE Qt6::Core Qt6::Test)
set_target_properties(DragonPixelProjectIndexServiceTests PROPERTIES AUTOMOC ON)
dpe_configure_native_target(DragonPixelProjectIndexServiceTests)

add_test(NAME s2.project_index_service COMMAND DragonPixelProjectIndexServiceTests)
set_tests_properties(s2.project_index_service PROPERTIES TIMEOUT 30)

if(WIN32)
    if(NOT DPE_EDITOR_QT_BIN_DIRECTORY)
        get_target_property(DPE_PROJECT_INDEX_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(
            DPE_EDITOR_QT_BIN_DIRECTORY
            "${DPE_PROJECT_INDEX_QMAKE_EXECUTABLE}"
            DIRECTORY)
    endif()
    set_tests_properties(s2.project_index_service PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_EDITOR_QT_BIN_DIRECTORY}")
endif()
