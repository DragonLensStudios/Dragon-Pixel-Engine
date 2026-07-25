find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets Network)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)

set(CMAKE_AUTOMOC ON)

set(DPE_EDITOR_MONOGAME_DLL
    "${CMAKE_SOURCE_DIR}/src/managed/DragonPixel.Adapter.MonoGame.Worker/bin/Release/net10.0/DragonPixel.Adapter.MonoGame.Worker.dll")
set(DPE_EDITOR_KNI_DLL
    "${CMAKE_SOURCE_DIR}/src/managed/DragonPixel.Adapter.Kni.Worker/bin/Release/net10.0/DragonPixel.Adapter.Kni.Worker.dll")

add_executable(DragonPixelEditor
    main.cpp
    AutomationBroker.cpp
    AutomationBroker.h
    AuthoringViewport.cpp
    AuthoringViewport.h
    EditorWindow.cpp
    EditorWindow.h
    LocalEndpoint.h
    WorkerClient.cpp
    WorkerClient.h
)
target_link_libraries(DragonPixelEditor PRIVATE
    DragonPixel::Core
    DragonPixel::Metadata
    DragonPixel::Scene
    DragonPixel::Serialization
    Qt6::Widgets
    Qt6::Network
)
target_compile_definitions(DragonPixelEditor PRIVATE
    DPE_DOTNET_EXECUTABLE="${DPE_DOTNET_EXECUTABLE}"
    DPE_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    DPE_PYTHON_TOOLS_ROOT="${CMAKE_SOURCE_DIR}/tools/python"
    DPE_EDITOR_MONOGAME_DLL="${DPE_EDITOR_MONOGAME_DLL}"
    DPE_EDITOR_KNI_DLL="${DPE_EDITOR_KNI_DLL}"
    DPE_DEFAULT_SAMPLE_PROJECT="${CMAKE_SOURCE_DIR}/samples/Slice1Sample/DragonPixelProject.json"
    DPE_DEFAULT_SAMPLE_SCENE="${CMAKE_SOURCE_DIR}/samples/Slice1Sample/Scenes/Main.dpescene"
)
add_dependencies(DragonPixelEditor DragonPixelAdapters)
dpe_configure_native_target(DragonPixelEditor)

add_test(NAME s1.editor_monogame COMMAND DragonPixelEditor --self-test monogame)
add_test(NAME s1.editor_kni COMMAND DragonPixelEditor --self-test kni)
add_test(NAME s1.editor_crash_recovery COMMAND DragonPixelEditor --self-test-crash monogame)
add_test(
    NAME s1.python_automation_contracts
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/tools/python/tests"
        -p "test_*.py"
)
set_tests_properties(s1.python_automation_contracts PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}/tools/python"
)
set_tests_properties(s1.editor_monogame s1.editor_kni s1.editor_crash_recovery PROPERTIES
    TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(WIN32)
    get_target_property(DPE_EDITOR_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(DPE_EDITOR_QT_BIN_DIRECTORY "${DPE_EDITOR_QMAKE_EXECUTABLE}" DIRECTORY)
    set_tests_properties(s1.editor_monogame s1.editor_kni s1.editor_crash_recovery PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_EDITOR_QT_BIN_DIRECTORY}"
    )
endif()
