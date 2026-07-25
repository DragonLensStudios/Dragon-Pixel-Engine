find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)

set(DPE_POC_D_MANAGED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/managed")
set(DPE_POC_D_SCANNER_PROJECT
    "${DPE_POC_D_MANAGED_ROOT}/DragonPixel.PocD.Scanner/DragonPixel.PocD.Scanner.csproj")
set(DPE_POC_D_TEST_PROJECT
    "${DPE_POC_D_MANAGED_ROOT}/DragonPixel.PocD.Tests/DragonPixel.PocD.Tests.csproj")
set(DPE_POC_D_SCANNER_DLL
    "${DPE_POC_D_MANAGED_ROOT}/DragonPixel.PocD.Scanner/bin/Release/net10.0/DragonPixel.PocD.Scanner.dll")
set(DPE_POC_D_TEST_DLL
    "${DPE_POC_D_MANAGED_ROOT}/DragonPixel.PocD.Tests/bin/Release/net10.0/DragonPixel.PocD.Tests.dll")

add_custom_target(dpe_poc_d_managed ALL
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_POC_D_SCANNER_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_POC_D_TEST_PROJECT} --configuration Release --nologo
    COMMENT "Building POC D read-only framework scanner and tests"
    USES_TERMINAL
    VERBATIM
)

add_test(
    NAME poc_d.read_only_scanner
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_POC_D_TEST_DLL}
        ${DPE_POC_D_SCANNER_DLL}
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
        ${CMAKE_BINARY_DIR}/poc-d-tests
)
set_tests_properties(poc_d.read_only_scanner PROPERTIES TIMEOUT 60)
