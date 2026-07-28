find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets Network Test)
find_package(SDL3 CONFIG REQUIRED)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)
find_program(DPE_CMAKE_EXECUTABLE cmake REQUIRED)
if(WIN32)
    set(DPE_COMPONENT_CMAKE_GENERATOR "Ninja")
    find_program(DPE_COMPONENT_MAKE_PROGRAM NAMES ninja.exe ninja REQUIRED)
else()
    set(DPE_COMPONENT_CMAKE_GENERATOR "${CMAKE_GENERATOR}")
    set(DPE_COMPONENT_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM}")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(DPE_EDITOR_MONOGAME_DLL
    "${CMAKE_SOURCE_DIR}/src/managed/DragonPixel.Adapter.MonoGame.Worker/bin/Release/net10.0/DragonPixel.Adapter.MonoGame.Worker.dll")
set(DPE_EDITOR_KNI_DLL
    "${CMAKE_SOURCE_DIR}/src/managed/DragonPixel.Adapter.Kni.Worker/bin/Release/net10.0/DragonPixel.Adapter.Kni.Worker.dll")

add_library(DragonPixelEditorLibrary OBJECT
    EditorResources.qrc
    AutomationBroker.cpp
    AutomationBroker.h
    ComponentModuleService.cpp
    ComponentModuleService.h
    EditorRuntimePaths.cpp
    EditorRuntimePaths.h
    AuthoringViewport.cpp
    AuthoringViewport.h
    EditorWindow.cpp
    EditorWindow.h
    EditorModels.cpp
    EditorModels.h
    GameViewport.cpp
    GameViewport.h
    GamepadInputSource.cpp
    GamepadInputSource.h
    InputMapEditorDialog.cpp
    InputMapEditorDialog.h
    InputMapService.cpp
    InputMapService.h
    MetadataManifestService.cpp
    MetadataManifestService.h
    TileDocumentService.cpp
    TileDocumentService.h
    TilePaletteWidget.cpp
    TilePaletteWidget.h
    TileSetWizard.cpp
    TileSetWizard.h
    PrefabService.cpp
    PrefabService.h
    ScriptEditorService.cpp
    ScriptEditorService.h
    LocalEndpoint.h
    WorkerClient.cpp
    WorkerClient.h
)
add_library(DragonPixel::Editor ALIAS DragonPixelEditorLibrary)

target_link_libraries(DragonPixelEditorLibrary PUBLIC
    DragonPixel::Core
    DragonPixel::Metadata
    DragonPixel::Prefab
    DragonPixel::Scene
    DragonPixel::Serialization
    DragonPixel::Tiles
    Qt6::Widgets
    Qt6::Network
    SDL3::SDL3
)
target_compile_definitions(DragonPixelEditorLibrary PRIVATE
    DPE_DOTNET_EXECUTABLE="${DPE_DOTNET_EXECUTABLE}"
    DPE_CMAKE_EXECUTABLE="${DPE_CMAKE_EXECUTABLE}"
    DPE_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    DPE_CXX_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    DPE_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    DPE_COMPONENT_CMAKE_GENERATOR="${DPE_COMPONENT_CMAKE_GENERATOR}"
    DPE_COMPONENT_MAKE_PROGRAM="${DPE_COMPONENT_MAKE_PROGRAM}"
    DPE_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    DPE_PYTHON_TOOLS_ROOT="${CMAKE_SOURCE_DIR}/tools/python"
    DPE_EDITOR_MONOGAME_DLL="${DPE_EDITOR_MONOGAME_DLL}"
    DPE_EDITOR_KNI_DLL="${DPE_EDITOR_KNI_DLL}"
    DPE_EDITOR_NATIVE_LIBRARY="$<TARGET_FILE:DragonPixelCAbi>"
    DPE_CONTRACTS_ASSEMBLY="${CMAKE_SOURCE_DIR}/src/managed/DragonPixel.Contracts/bin/Release/netstandard2.1/DragonPixel.Contracts.dll"
    DPE_DEFAULT_SAMPLE_PROJECT="${CMAKE_SOURCE_DIR}/samples/Slice1Sample/DragonPixelProject.json"
    DPE_DEFAULT_SAMPLE_SCENE="${CMAKE_SOURCE_DIR}/samples/Slice1Sample/Scenes/Main.dpescene"
)
if(TARGET DragonPixelAdapters)
    add_dependencies(DragonPixelEditorLibrary DragonPixelAdapters)
endif()
if(TARGET DragonPixelCAbi)
    add_dependencies(DragonPixelEditorLibrary DragonPixelCAbi)
endif()

dpe_configure_native_target(DragonPixelEditorLibrary)

add_executable(DragonPixelEditor
    main.cpp
)
target_link_libraries(DragonPixelEditor PRIVATE
    DragonPixel::Editor
)
target_compile_definitions(DragonPixelEditor PRIVATE
    DPE_DEFAULT_SAMPLE_PROJECT="${CMAKE_SOURCE_DIR}/samples/Slice1Sample/DragonPixelProject.json"
)
dpe_configure_native_target(DragonPixelEditor)

add_executable(DragonPixelEditorInteractionTests
    EditorInteractionTests.cpp
)
target_link_libraries(DragonPixelEditorInteractionTests PRIVATE
    DragonPixel::Editor
    Qt6::Test
)
target_compile_definitions(DragonPixelEditorInteractionTests PRIVATE
    DPE_CONTRACTS_ASSEMBLY="${CMAKE_SOURCE_DIR}/src/managed/DragonPixel.Contracts/bin/Release/netstandard2.1/DragonPixel.Contracts.dll"
    DPE_DEFAULT_SAMPLE_PROJECT="${CMAKE_SOURCE_DIR}/samples/Slice1Sample/DragonPixelProject.json"
)
dpe_configure_native_target(DragonPixelEditorInteractionTests)

include("${CMAKE_CURRENT_LIST_DIR}/ProjectIndexService.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/AssetPreviewService.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ProjectModel.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ConsoleModel.cmake")

add_executable(DragonPixelGameViewportTests
    GameViewport.cpp
    GameViewport.h
    GamepadInputSource.cpp
    GamepadInputSource.h
    InputMapService.cpp
    InputMapService.h
    GameViewportTests.cpp
)
target_link_libraries(DragonPixelGameViewportTests PRIVATE
    Qt6::Widgets
    Qt6::Test
    SDL3::SDL3
)
dpe_configure_native_target(DragonPixelGameViewportTests)
add_test(NAME s2.game_view_input_capture COMMAND DragonPixelGameViewportTests)
set_tests_properties(s2.game_view_input_capture PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(DragonPixelInputMapServiceTests
    InputMapService.cpp
    InputMapService.h
    InputMapServiceTests.cpp
)
target_link_libraries(DragonPixelInputMapServiceTests PRIVATE
    Qt6::Core
    Qt6::Test
)
dpe_configure_native_target(DragonPixelInputMapServiceTests)
add_test(NAME s2.input_map_service COMMAND DragonPixelInputMapServiceTests)
add_test(NAME poc_j.input_map_rebinding COMMAND DragonPixelInputMapServiceTests)
set_tests_properties(
    s2.input_map_service
    poc_j.input_map_rebinding
    PROPERTIES
        TIMEOUT 30
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

if(WIN32)
    find_program(DPE_WINDOWS_POWERSHELL_EXECUTABLE NAMES powershell.exe REQUIRED)
    add_executable(DragonPixelLauncherArgumentProbe
        "${CMAKE_SOURCE_DIR}/scripts/dev/tests/LauncherArgumentProbe.cpp"
    )
    dpe_configure_native_target(DragonPixelLauncherArgumentProbe)

    add_test(
        NAME s1.windows_launcher_path_with_spaces_quoted
        COMMAND "${DPE_WINDOWS_POWERSHELL_EXECUTABLE}"
            -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/dev/tests/Test-Launch-Editor.ps1"
            -LauncherPath "${CMAKE_SOURCE_DIR}/scripts/dev/Launch-Editor.ps1"
            -ProbeExecutable "$<TARGET_FILE:DragonPixelLauncherArgumentProbe>"
            -ArgumentTransport QuotedArguments
    )
    set_tests_properties(s1.windows_launcher_path_with_spaces_quoted PROPERTIES TIMEOUT 30)

    find_program(DPE_POWERSHELL_CORE_EXECUTABLE NAMES pwsh.exe pwsh)
    if(DPE_POWERSHELL_CORE_EXECUTABLE)
        add_test(
            NAME s1.windows_launcher_path_with_spaces_argument_list
            COMMAND "${DPE_POWERSHELL_CORE_EXECUTABLE}"
                -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/dev/tests/Test-Launch-Editor.ps1"
                -LauncherPath "${CMAKE_SOURCE_DIR}/scripts/dev/Launch-Editor.ps1"
                -ProbeExecutable "$<TARGET_FILE:DragonPixelLauncherArgumentProbe>"
                -ArgumentTransport ArgumentList
        )
        set_tests_properties(s1.windows_launcher_path_with_spaces_argument_list PROPERTIES TIMEOUT 30)
    endif()
endif()

add_executable(DragonPixelComponentModuleServiceTests
    ComponentModuleService.cpp
    ComponentModuleService.h
    ComponentModuleServiceTests.cpp
    MetadataManifestService.cpp
    MetadataManifestService.h
)
target_link_libraries(DragonPixelComponentModuleServiceTests PRIVATE
    DragonPixel::Core
    DragonPixel::Metadata
    Qt6::Core
)
target_compile_definitions(DragonPixelComponentModuleServiceTests PRIVATE
    DPE_CONTRACTS_ASSEMBLY="${CMAKE_SOURCE_DIR}/src/managed/DragonPixel.Contracts/bin/Release/netstandard2.1/DragonPixel.Contracts.dll"
    DPE_DOTNET_EXECUTABLE="${DPE_DOTNET_EXECUTABLE}"
    DPE_CMAKE_EXECUTABLE="${DPE_CMAKE_EXECUTABLE}"
    DPE_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    DPE_CXX_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    DPE_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    DPE_COMPONENT_CMAKE_GENERATOR="${DPE_COMPONENT_CMAKE_GENERATOR}"
    DPE_COMPONENT_MAKE_PROGRAM="${DPE_COMPONENT_MAKE_PROGRAM}"
)
dpe_configure_native_target(DragonPixelComponentModuleServiceTests)
if(TARGET DragonPixelAdapters)
    add_dependencies(DragonPixelComponentModuleServiceTests DragonPixelAdapters)
endif()
add_test(NAME s2.project_component_modules COMMAND DragonPixelComponentModuleServiceTests)
add_test(NAME poc_i.project_component_modules COMMAND DragonPixelComponentModuleServiceTests)
set_tests_properties(s2.project_component_modules poc_i.project_component_modules PROPERTIES TIMEOUT 600)

add_executable(DragonPixelScriptEditorServiceTests
    ScriptEditorService.cpp
    ScriptEditorService.h
    ScriptEditorServiceTests.cpp
)
target_link_libraries(DragonPixelScriptEditorServiceTests PRIVATE
    Qt6::Core
    Qt6::Test
)
dpe_configure_native_target(DragonPixelScriptEditorServiceTests)
add_test(NAME s2.script_editor_service COMMAND DragonPixelScriptEditorServiceTests)
add_test(NAME poc_i.rider_source_authoring COMMAND DragonPixelScriptEditorServiceTests)
set_tests_properties(
    s2.script_editor_service
    poc_i.rider_source_authoring
    PROPERTIES TIMEOUT 30)

add_executable(DragonPixelPrefabServiceTests
    PrefabService.cpp
    PrefabService.h
    PrefabServiceTests.cpp
)
target_link_libraries(DragonPixelPrefabServiceTests PRIVATE
    DragonPixel::Core
    DragonPixel::Prefab
    DragonPixel::Scene
    DragonPixel::Serialization
    Qt6::Core
)
dpe_configure_native_target(DragonPixelPrefabServiceTests)
add_test(NAME s2.prefab_editor_service COMMAND DragonPixelPrefabServiceTests)
add_test(NAME poc_f.prefab_editor_workflows COMMAND DragonPixelPrefabServiceTests)
set_tests_properties(s2.prefab_editor_service poc_f.prefab_editor_workflows PROPERTIES TIMEOUT 30)

add_test(NAME s1.editor_monogame COMMAND DragonPixelEditor --self-test monogame)
add_test(NAME s1.editor_kni COMMAND DragonPixelEditor --self-test kni)
add_test(NAME s1.editor_crash_recovery COMMAND DragonPixelEditor --self-test-crash monogame)
add_test(NAME s2.editor_interactions COMMAND DragonPixelEditorInteractionTests)
add_test(NAME poc_h.qt_interactions COMMAND DragonPixelEditorInteractionTests)
add_test(
    NAME s2.tilemap_creation_workflow
    COMMAND DragonPixelEditorInteractionTests
        tilemap_creation_workflow_is_paint_ready_recoverable_and_guided
)
add_test(
    NAME poc_j.qt_input_latency
    COMMAND DragonPixelEditorInteractionTests poc_j_qt_input_latency
)
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
if(DPE_ENABLE_ASAN)
    set(DPE_EDITOR_INTERACTION_TEST_TIMEOUT 480)
else()
    # This executable now covers project creation, asset transactions, domain
    # drag/drop, grouped hierarchy edits, and restartable multi-Inspector docks
    # in addition to the original worker-backed editor interactions. Product
    # timing gates remain in their focused tests; this is only the suite budget.
    set(DPE_EDITOR_INTERACTION_TEST_TIMEOUT 240)
endif()
set_tests_properties(s2.editor_interactions poc_h.qt_interactions PROPERTIES
    TIMEOUT ${DPE_EDITOR_INTERACTION_TEST_TIMEOUT}
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
if(DPE_ENABLE_ASAN)
    set(DPE_TILEMAP_CREATION_WORKFLOW_TIMEOUT 240)
else()
    set(DPE_TILEMAP_CREATION_WORKFLOW_TIMEOUT 120)
endif()
set_tests_properties(s2.tilemap_creation_workflow PROPERTIES
    TIMEOUT ${DPE_TILEMAP_CREATION_WORKFLOW_TIMEOUT}
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DPE_RUN_TILEMAP_CREATION_WORKFLOW=1"
)
if(DPE_ENABLE_ASAN)
    set(DPE_POC_J_TEST_TIMEOUT 1200)
else()
    set(DPE_POC_J_TEST_TIMEOUT 750)
endif()
set_tests_properties(poc_j.qt_input_latency PROPERTIES
    TIMEOUT ${DPE_POC_J_TEST_TIMEOUT}
    RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DPE_RUN_POC_J=1"
)
dpe_configure_managed_asan_test(s1.editor_monogame NATIVE_HOST)
dpe_configure_managed_asan_test(s1.editor_kni NATIVE_HOST)
dpe_configure_managed_asan_test(s1.editor_crash_recovery NATIVE_HOST)
dpe_configure_managed_asan_test(poc_j.qt_input_latency NATIVE_HOST)

if(WIN32)
    get_target_property(DPE_EDITOR_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(DPE_EDITOR_QT_BIN_DIRECTORY "${DPE_EDITOR_QMAKE_EXECUTABLE}" DIRECTORY)
    set_tests_properties(
        s1.editor_monogame
        s1.editor_kni
        s1.editor_crash_recovery
        s2.editor_interactions
        poc_h.qt_interactions
        s2.tilemap_creation_workflow
        poc_j.qt_input_latency
        s2.project_component_modules
        poc_i.project_component_modules
        s2.script_editor_service
        poc_i.rider_source_authoring
        s2.prefab_editor_service
        poc_f.prefab_editor_workflows
        s2.game_view_input_capture
        PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_EDITOR_QT_BIN_DIRECTORY}"
    )
endif()
