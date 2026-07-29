find_program(DPE_DOTNET_EXECUTABLE dotnet REQUIRED)

set(DPE_MANAGED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(DPE_CONTRACTS_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Contracts/DragonPixel.Contracts.csproj")
set(DPE_INTEROP_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.NativeInterop/DragonPixel.NativeInterop.csproj")
set(DPE_WORKER_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Runtime.Worker/DragonPixel.Runtime.Worker.csproj")
set(DPE_MANAGED_TEST_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.S1.ManagedTests/DragonPixel.S1.ManagedTests.csproj")
set(DPE_WORKER_DLL "${DPE_MANAGED_ROOT}/DragonPixel.Runtime.Worker/bin/Release/net10.0/DragonPixel.Runtime.Worker.dll")
set(DPE_MANAGED_TEST_DLL "${DPE_MANAGED_ROOT}/DragonPixel.S1.ManagedTests/bin/Release/net10.0/DragonPixel.S1.ManagedTests.dll")
set(DPE_PHYSICS_INTEROP_TEST_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.PhysicsInterop.Tests/DragonPixel.PhysicsInterop.Tests.csproj")
set(DPE_PHYSICS_INTEROP_TEST_DLL "${DPE_MANAGED_ROOT}/DragonPixel.PhysicsInterop.Tests/bin/Release/net10.0/DragonPixel.PhysicsInterop.Tests.dll")
set(DPE_WORKER_PHYSICS_TEST_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.WorkerPhysics.Tests/DragonPixel.WorkerPhysics.Tests.csproj")
set(DPE_WORKER_PHYSICS_TEST_DLL "${DPE_MANAGED_ROOT}/DragonPixel.WorkerPhysics.Tests/bin/Release/net10.0/DragonPixel.WorkerPhysics.Tests.dll")
set(DPE_COMPONENT_RUNTIME_TEST_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.ComponentRuntime.Tests/DragonPixel.ComponentRuntime.Tests.csproj")
set(DPE_COMPONENT_RUNTIME_TEST_DLL "${DPE_MANAGED_ROOT}/DragonPixel.ComponentRuntime.Tests/bin/Release/net10.0/DragonPixel.ComponentRuntime.Tests.dll")
set(DPE_FRAME_PACING_TEST_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.FramePacing.Tests/DragonPixel.FramePacing.Tests.csproj")
set(DPE_FRAME_PACING_TEST_DLL "${DPE_MANAGED_ROOT}/DragonPixel.FramePacing.Tests/bin/Release/net10.0/DragonPixel.FramePacing.Tests.dll")
set(DPE_SAMPLE_SCRIPTS_TEST_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.SampleScripts.Tests/DragonPixel.SampleScripts.Tests.csproj")
set(DPE_SAMPLE_SCRIPTS_TEST_DLL "${DPE_MANAGED_ROOT}/DragonPixel.SampleScripts.Tests/bin/Release/net10.0/DragonPixel.SampleScripts.Tests.dll")
set(DPE_MONOGAME_ADAPTER_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Adapter.MonoGame.Worker/DragonPixel.Adapter.MonoGame.Worker.csproj")
set(DPE_KNI_ADAPTER_PROJECT "${DPE_MANAGED_ROOT}/DragonPixel.Adapter.Kni.Worker/DragonPixel.Adapter.Kni.Worker.csproj")

add_custom_target(DragonPixelManaged ALL
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_CONTRACTS_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_INTEROP_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_WORKER_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_MANAGED_TEST_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_PHYSICS_INTEROP_TEST_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_WORKER_PHYSICS_TEST_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_COMPONENT_RUNTIME_TEST_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_FRAME_PACING_TEST_PROJECT} --configuration Release --nologo
    COMMAND ${DPE_DOTNET_EXECUTABLE} build ${DPE_SAMPLE_SCRIPTS_TEST_PROJECT} --configuration Release --nologo
    COMMENT "Building S1.0 managed contracts, native interop, worker, and tests"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(DragonPixelManaged DragonPixelCAbi)
if(TARGET dpe_component_runtime_test_plugin)
    add_dependencies(DragonPixelManaged dpe_component_runtime_test_plugin)
endif()
if(TARGET dpe_component_runtime_v1_test_plugin)
    add_dependencies(DragonPixelManaged dpe_component_runtime_v1_test_plugin)
endif()

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

add_test(
    NAME s2.physics_managed_interop
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_PHYSICS_INTEROP_TEST_DLL}
        $<TARGET_FILE:DragonPixelCAbi>
)
add_test(
    NAME poc_g.managed_interop
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_PHYSICS_INTEROP_TEST_DLL}
        $<TARGET_FILE:DragonPixelCAbi>
)
set_tests_properties(s2.physics_managed_interop poc_g.managed_interop PROPERTIES TIMEOUT 60)
dpe_configure_managed_asan_test(s2.physics_managed_interop)
dpe_configure_managed_asan_test(poc_g.managed_interop)

add_test(
    NAME s2.worker_physics
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_WORKER_PHYSICS_TEST_DLL}
        $<TARGET_FILE:DragonPixelCAbi>
        ${CMAKE_BINARY_DIR}/worker-physics-tests
        ${CMAKE_SOURCE_DIR}/samples/Slice1Sample/Scenes/Main.dpescene
)
add_test(
    NAME poc_g.worker_physics
    COMMAND
        ${DPE_DOTNET_EXECUTABLE} ${DPE_WORKER_PHYSICS_TEST_DLL}
        $<TARGET_FILE:DragonPixelCAbi>
        ${CMAKE_BINARY_DIR}/poc-g-worker-physics-tests
        ${CMAKE_SOURCE_DIR}/samples/Slice1Sample/Scenes/Main.dpescene
)
set_tests_properties(s2.worker_physics poc_g.worker_physics PROPERTIES TIMEOUT 90)
dpe_configure_managed_asan_test(s2.worker_physics)
dpe_configure_managed_asan_test(poc_g.worker_physics)

add_test(
    NAME s2.project_component_runtime
    COMMAND ${DPE_DOTNET_EXECUTABLE} ${DPE_COMPONENT_RUNTIME_TEST_DLL}
        $<TARGET_FILE:dpe_component_runtime_test_plugin>
        $<TARGET_FILE:dpe_component_runtime_v1_test_plugin>
)
add_test(
    NAME poc_i.worker_only_component_runtime
    COMMAND ${DPE_DOTNET_EXECUTABLE} ${DPE_COMPONENT_RUNTIME_TEST_DLL}
        $<TARGET_FILE:dpe_component_runtime_test_plugin>
        $<TARGET_FILE:dpe_component_runtime_v1_test_plugin>
)
set_tests_properties(s2.project_component_runtime poc_i.worker_only_component_runtime PROPERTIES TIMEOUT 60)
# These managed test hosts load the instrumented native modules directly.
# Preload the Clang ASan runtime into dotnet rather than reserving it for a
# child process that these aliases never launch.
dpe_configure_managed_asan_test(s2.project_component_runtime)
dpe_configure_managed_asan_test(poc_i.worker_only_component_runtime)

add_test(
    NAME s2.sample_mymover_runtime
    COMMAND ${DPE_DOTNET_EXECUTABLE} ${DPE_SAMPLE_SCRIPTS_TEST_DLL}
)
add_test(
    NAME poc_i.sample_mymover_runtime
    COMMAND ${DPE_DOTNET_EXECUTABLE} ${DPE_SAMPLE_SCRIPTS_TEST_DLL}
)
set_tests_properties(s2.sample_mymover_runtime poc_i.sample_mymover_runtime PROPERTIES TIMEOUT 30)
dpe_configure_managed_asan_test(s2.sample_mymover_runtime)
dpe_configure_managed_asan_test(poc_i.sample_mymover_runtime)

add_test(
    NAME poc_b.absolute_frame_pacing
    COMMAND ${DPE_DOTNET_EXECUTABLE} ${DPE_FRAME_PACING_TEST_DLL}
)
set_tests_properties(poc_b.absolute_frame_pacing PROPERTIES TIMEOUT 30)
