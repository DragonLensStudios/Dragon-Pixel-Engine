add_library(DragonPixelPrefab STATIC
    src/prefab.cpp
)
target_include_directories(DragonPixelPrefab PUBLIC include)
target_link_libraries(DragonPixelPrefab
    PUBLIC DragonPixel::Core DragonPixel::Scene
    PRIVATE DragonPixel::Serialization nlohmann_json::nlohmann_json
)
target_compile_features(DragonPixelPrefab PUBLIC cxx_std_20)
dpe_configure_native_target(DragonPixelPrefab)
add_library(DragonPixel::Prefab ALIAS DragonPixelPrefab)

if(BUILD_TESTING)
    add_executable(dpe_prefab_tests Tests/prefab_tests.cpp)
    target_link_libraries(dpe_prefab_tests PRIVATE DragonPixel::Prefab nlohmann_json::nlohmann_json)
    dpe_configure_native_target(dpe_prefab_tests)
    add_test(NAME s2.linked_prefabs COMMAND dpe_prefab_tests)
    add_test(NAME poc_f.linked_prefabs COMMAND dpe_prefab_tests)
    set_tests_properties(s2.linked_prefabs poc_f.linked_prefabs PROPERTIES TIMEOUT 30)
endif()
