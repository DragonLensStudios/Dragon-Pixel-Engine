add_executable(dpe_poc_a_native_tests
    tests/dpe_api_v1_tests.cpp
)
target_link_libraries(dpe_poc_a_native_tests PRIVATE DragonPixel::CAbi)
add_custom_command(TARGET dpe_poc_a_native_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DragonPixelCAbi>
        $<TARGET_FILE_DIR:dpe_poc_a_native_tests>
)

if(MSVC)
    target_compile_options(dpe_poc_a_native_tests PRIVATE /W4 /WX /permissive- /EHsc)
    if(DPE_ENABLE_ASAN)
        target_compile_options(dpe_poc_a_native_tests PRIVATE /fsanitize=address /Zi)
    endif()
else()
    target_compile_options(dpe_poc_a_native_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(DPE_ENABLE_ASAN)
        target_compile_options(dpe_poc_a_native_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(dpe_poc_a_native_tests PRIVATE -fsanitize=address)
    endif()
endif()

add_test(NAME poc_a.native COMMAND dpe_poc_a_native_tests)

find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)
set(DPE_POC_A_MANAGED_PROJECT
    "${CMAKE_SOURCE_DIR}/pocs/abi/managed/DragonPixel.PocA.Worker/DragonPixel.PocA.Worker.csproj")
set(DPE_POC_A_MANAGED_DLL
    "${CMAKE_SOURCE_DIR}/pocs/abi/managed/DragonPixel.PocA.Worker/bin/Release/net10.0/DragonPixel.PocA.Worker.dll")
add_custom_target(dpe_poc_a_managed ALL
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_POC_A_MANAGED_PROJECT} --configuration Release --nologo
    COMMENT "Building POC A managed ownership worker"
    USES_TERMINAL
    VERBATIM
)
add_test(
    NAME poc_a.managed
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_POC_A_MANAGED_DLL}
        $<TARGET_FILE:DragonPixelCAbi>
)
set_tests_properties(poc_a.managed PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
)
dpe_configure_managed_asan_test(poc_a.managed)
