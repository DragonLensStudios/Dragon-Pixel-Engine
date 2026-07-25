find_package(box2d CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

add_library(DragonPixelPhysics STATIC
    src/world.cpp
)
target_include_directories(DragonPixelPhysics PUBLIC include)
target_link_libraries(DragonPixelPhysics
    PUBLIC DragonPixel::Core
    PRIVATE box2d::box2d Jolt::Jolt
)
target_compile_features(DragonPixelPhysics PUBLIC cxx_std_20)
set_target_properties(DragonPixelPhysics PROPERTIES POSITION_INDEPENDENT_CODE ON)
dpe_configure_native_target(DragonPixelPhysics)
add_library(DragonPixel::Physics ALIAS DragonPixelPhysics)

if(BUILD_TESTING)
    add_executable(dpe_physics_tests Tests/physics_world_tests.cpp)
    target_link_libraries(dpe_physics_tests PRIVATE DragonPixel::Physics)
    dpe_configure_native_target(dpe_physics_tests)
    add_test(NAME s2.physics_world COMMAND dpe_physics_tests)
    add_test(NAME poc_g.native_facade COMMAND dpe_physics_tests)
    set_tests_properties(s2.physics_world PROPERTIES TIMEOUT 60)
    set_tests_properties(poc_g.native_facade PROPERTIES TIMEOUT 60)
endif()
