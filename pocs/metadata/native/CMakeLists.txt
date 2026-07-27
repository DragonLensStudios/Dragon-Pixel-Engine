add_executable(dpe_poc_c_native_manifest
    src/component_manifest.cpp
)

dpe_configure_native_target(dpe_poc_c_native_manifest)

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
