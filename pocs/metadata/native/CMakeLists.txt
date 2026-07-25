add_executable(dpe_poc_c_native_manifest
    src/component_manifest.cpp
)

if(MSVC)
    target_compile_options(dpe_poc_c_native_manifest PRIVATE /W4 /WX /permissive- /EHsc)
    if(DPE_ENABLE_ASAN)
        target_compile_options(dpe_poc_c_native_manifest PRIVATE /fsanitize=address /Zi)
    endif()
else()
    target_compile_options(dpe_poc_c_native_manifest PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(DPE_ENABLE_ASAN)
        target_compile_options(dpe_poc_c_native_manifest PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(dpe_poc_c_native_manifest PRIVATE -fsanitize=address)
    endif()
endif()

find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)
add_test(
    NAME poc_c.metadata_serialization
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} run
        --project ${CMAKE_SOURCE_DIR}/pocs/metadata/managed/DragonPixel.PocC.Tests/DragonPixel.PocC.Tests.csproj
        --configuration Release
        --
        ${CMAKE_SOURCE_DIR}
        $<TARGET_FILE:dpe_poc_c_native_manifest>
)
set_tests_properties(poc_c.metadata_serialization PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 180
)
