find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets)

set(DPE_POC_B_MANAGED_ROOT "${CMAKE_SOURCE_DIR}/src/managed")
set(DPE_POC_B_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/managed")
set(DPE_POC_B_MONOGAME_PROJECT
    "${DPE_POC_B_MANAGED_ROOT}/DragonPixel.Adapter.MonoGame.Worker/DragonPixel.Adapter.MonoGame.Worker.csproj")
set(DPE_POC_B_KNI_PROJECT
    "${DPE_POC_B_MANAGED_ROOT}/DragonPixel.Adapter.Kni.Worker/DragonPixel.Adapter.Kni.Worker.csproj")
set(DPE_POC_B_TEST_PROJECT
    "${DPE_POC_B_TEST_ROOT}/DragonPixel.PocB.Tests/DragonPixel.PocB.Tests.csproj")
set(DPE_POC_B_MONOGAME_DLL
    "${DPE_POC_B_MANAGED_ROOT}/DragonPixel.Adapter.MonoGame.Worker/bin/Release/net10.0/DragonPixel.Adapter.MonoGame.Worker.dll")
set(DPE_POC_B_KNI_DLL
    "${DPE_POC_B_MANAGED_ROOT}/DragonPixel.Adapter.Kni.Worker/bin/Release/net10.0/DragonPixel.Adapter.Kni.Worker.dll")
set(DPE_POC_B_TEST_DLL
    "${DPE_POC_B_TEST_ROOT}/DragonPixel.PocB.Tests/bin/Release/net10.0/DragonPixel.PocB.Tests.dll")

add_custom_target(dpe_poc_b_managed ALL
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_POC_B_TEST_PROJECT} --configuration Release --nologo
    COMMENT "Building POC B lifecycle tests against production adapter workers"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(dpe_poc_b_managed DragonPixelAdapters DragonPixelCAbi)

add_executable(dpe_poc_b_viewer
    qt/main.cpp
)
target_link_libraries(dpe_poc_b_viewer PRIVATE Qt6::Widgets)
target_compile_definitions(dpe_poc_b_viewer PRIVATE
    DPE_DOTNET_EXECUTABLE="${DPE_DOTNET_EXECUTABLE}"
    DPE_POC_B_MONOGAME_DLL="${DPE_POC_B_MONOGAME_DLL}"
    DPE_POC_B_KNI_DLL="${DPE_POC_B_KNI_DLL}"
)
add_dependencies(dpe_poc_b_viewer dpe_poc_b_managed)

if(MSVC)
    target_compile_options(dpe_poc_b_viewer PRIVATE /W4 /WX /permissive- /EHsc)
    if(DPE_ENABLE_ASAN)
        target_compile_options(dpe_poc_b_viewer PRIVATE /fsanitize=address /Zi)
    endif()
else()
    target_compile_options(dpe_poc_b_viewer PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(DPE_ENABLE_ASAN)
        target_compile_options(dpe_poc_b_viewer PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(dpe_poc_b_viewer PRIVATE -fsanitize=address)
    endif()
endif()

add_test(
    NAME poc_b.worker_viewport
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_POC_B_TEST_DLL}
        ${DPE_POC_B_MONOGAME_DLL}
        ${DPE_POC_B_KNI_DLL}
        ${CMAKE_BINARY_DIR}/poc-b-tests
        $<TARGET_FILE:DragonPixelCAbi>
)
set_tests_properties(poc_b.worker_viewport PROPERTIES TIMEOUT 120)

# POC E records adapter-specific real-device rendering/readback/picking evidence. Keep
# these tests separate so one framework failure cannot suppress the other's result.
add_test(
    NAME poc_e.monogame_scene_rendering
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_POC_B_TEST_DLL}
        ${DPE_POC_B_MONOGAME_DLL}
        ${DPE_POC_B_KNI_DLL}
        ${CMAKE_BINARY_DIR}/poc-e-monogame-tests
        $<TARGET_FILE:DragonPixelCAbi>
        --adapter monogame
)
add_test(
    NAME poc_e.kni_scene_rendering
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_POC_B_TEST_DLL}
        ${DPE_POC_B_MONOGAME_DLL}
        ${DPE_POC_B_KNI_DLL}
        ${CMAKE_BINARY_DIR}/poc-e-kni-tests
        $<TARGET_FILE:DragonPixelCAbi>
        --adapter kni
)
set_tests_properties(
    poc_e.monogame_scene_rendering
    poc_e.kni_scene_rendering
    PROPERTIES TIMEOUT 120
)
dpe_configure_managed_asan_test(poc_b.worker_viewport)
dpe_configure_managed_asan_test(poc_e.monogame_scene_rendering)
dpe_configure_managed_asan_test(poc_e.kni_scene_rendering)

add_test(NAME poc_b.monogame_graphics
    COMMAND ${DPE_DOTNET_EXECUTABLE} ${DPE_POC_B_MONOGAME_DLL} --graphics-probe)
add_test(NAME poc_b.kni_graphics
    COMMAND ${DPE_DOTNET_EXECUTABLE} ${DPE_POC_B_KNI_DLL} --graphics-probe)
set_tests_properties(poc_b.monogame_graphics poc_b.kni_graphics PROPERTIES TIMEOUT 30)

add_test(NAME poc_b.qt_monogame COMMAND dpe_poc_b_viewer --self-test monogame)
add_test(NAME poc_b.qt_kni COMMAND dpe_poc_b_viewer --self-test kni)
set_tests_properties(poc_b.qt_monogame poc_b.qt_kni PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(WIN32)
    get_target_property(DPE_QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(DPE_QT_BIN_DIRECTORY "${DPE_QT_QMAKE_EXECUTABLE}" DIRECTORY)
    set_tests_properties(poc_b.qt_monogame poc_b.qt_kni PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${DPE_QT_BIN_DIRECTORY}"
    )
endif()
