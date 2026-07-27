cmake_minimum_required(VERSION 3.25)

project(DragonPixelEngine VERSION 0.0.0 LANGUAGES C CXX)

option(DPE_BUILD_POC_A "Build the native/managed ABI ownership proof of concept" ON)
option(DPE_BUILD_POC_B "Build the worker/viewport/framework-adapter proof of concept" ON)
option(DPE_BUILD_POC_C "Build the unified metadata and serialization proof of concept" ON)
option(DPE_BUILD_POC_D "Build the read-only framework scanner proof of concept" ON)
option(DPE_BUILD_CORE "Build the production S1.0 native core modules" ON)
option(DPE_BUILD_MANAGED "Build the production S1.0 managed contracts and worker" ON)
option(DPE_BUILD_EDITOR "Build the Slice 1 Qt editor" ON)
option(DPE_ENABLE_ASAN "Enable AddressSanitizer for supported native targets" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Portable native modules are static today but are also linked into the public
# shared C ABI. Make that composition valid in clean Unix and ASan builds.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DragonPixelWarnings)

include(CTest)

if(DPE_BUILD_CORE)
    find_package(nlohmann_json CONFIG REQUIRED)
    add_subdirectory(src/native)
endif()

if(DPE_BUILD_MANAGED)
    add_subdirectory(src/managed)
endif()

if(DPE_BUILD_POC_A)
    add_subdirectory(pocs/abi/native)
endif()

if(DPE_BUILD_POC_C)
    add_subdirectory(pocs/metadata/native)
endif()

if(DPE_BUILD_POC_B)
    add_subdirectory(pocs/worker)
endif()

if(DPE_BUILD_POC_D)
    add_subdirectory(pocs/scanner)
endif()

if(DPE_BUILD_EDITOR)
    add_subdirectory(src/editor)
endif()
