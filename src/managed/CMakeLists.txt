find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)

set(DPE_MANAGED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(DPE_CONTRACTS_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Contracts/DragonPixel.Contracts.csproj")
set(DPE_INTEROP_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.NativeInterop/DragonPixel.NativeInterop.csproj")
set(DPE_WORKER_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Runtime.Worker/DragonPixel.Runtime.Worker.csproj")
set(DPE_MANAGED_TEST_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.S1.ManagedTests/DragonPixel.S1.ManagedTests.csproj")
set(DPE_WORKER_DLL "${DPE_MANAGED_ROOT}/DragonPixel.Runtime.Worker/bin/Release/net10.0/DragonPixel.Runtime.Worker.dll")
set(DPE_MANAGED_TEST_DLL "${DPE_MANAGED_ROOT}/DragonPixel.S1.ManagedTests/bin/Release/net10.0/DragonPixel.S1.ManagedTests.dll")
set(DPE_MONOGAME_ADAPTER_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Adapter.MonoGame.Worker/DragonPixel.Adapter.MonoGame.Worker.csproj")
set(DPE_KNI_ADAPTER_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Adapter.Kni.Worker/DragonPixel.Adapter.Kni.Worker.csproj")

add_custom_target(DragonPixelManaged ALL
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_CONTRACTS_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_INTEROP_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_WORKER_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_MANAGED_TEST_PROJECT} --configuration Release --nologo
    COMMENT "Building S1.0 managed contracts, native interop, worker, and tests"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(DragonPixelManaged DragonPixelCAbi)

add_custom_target(DragonPixelAdapters ALL
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_MONOGAME_ADAPTER_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_KNI_ADAPTER_PROJECT} --configuration Release --nologo
    COMMENT "Building Slice 1 MonoGame and KNI adapter workers"
    USES_TERMINAL
    VERBATIM
)

add_test(
    NAME s1.managed_contracts_worker
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_MANAGED_TEST_DLL}
        $<TARGET_FILE:DragonPixelCAbi>
        ${DPE_WORKER_DLL}
        ${CMAKE_BINARY_DIR}/s1-managed-tests
)
set_tests_properties(s1.managed_contracts_worker PROPERTIES TIMEOUT 60)
dpe_configure_managed_asan_test(s1.managed_contracts_worker)
