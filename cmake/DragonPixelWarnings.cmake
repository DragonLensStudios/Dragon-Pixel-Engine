function(dpe_configure_native_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
        if(DPE_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            # Static third-party libraries (including Jolt) are built by vcpkg
            # without MSVC STL container annotations. Keep one consistent STL
            # ABI while retaining address instrumentation for engine targets.
            target_compile_definitions(${target} PRIVATE
                _DISABLE_STRING_ANNOTATION
                _DISABLE_VECTOR_ANNOTATION
            )
            get_filename_component(_dpe_msvc_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
            if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
                set(_dpe_msvc_asan_runtime "${_dpe_msvc_compiler_directory}/clang_rt.asan_dynamic-aarch64.dll")
            elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
                set(_dpe_msvc_asan_runtime "${_dpe_msvc_compiler_directory}/clang_rt.asan_dynamic-x86_64.dll")
            else()
                set(_dpe_msvc_asan_runtime "${_dpe_msvc_compiler_directory}/clang_rt.asan_dynamic-i386.dll")
            endif()
            if(NOT EXISTS "${_dpe_msvc_asan_runtime}")
                message(FATAL_ERROR "MSVC AddressSanitizer runtime was not found: ${_dpe_msvc_asan_runtime}")
            endif()
            # Windows resolves an instrumented executable/DLL's ASan dependency
            # beside that binary. Copying the runtime per target also lets managed
            # workers load an instrumented dragonpixel.dll without a developer shell.
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dpe_msvc_asan_runtime}"
                    "$<TARGET_FILE_DIR:${target}>"
                VERBATIM
            )
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(DPE_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address)
        endif()
    endif()
endfunction()

function(dpe_configure_managed_asan_test test_name)
    if(DPE_ENABLE_ASAN AND MSVC)
        # A managed test is hosted by dotnet.exe, so Windows does not include the
        # instrumented plugin directory in its initial DLL search path. Expose the
        # compiler runtime directory explicitly before NativeLibrary.Load runs.
        get_filename_component(_dpe_msvc_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_dpe_msvc_compiler_directory}"
        )
    elseif(DPE_ENABLE_ASAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            set(_dpe_asan_library_name "libclang_rt.asan-x86_64.so")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            set(_dpe_asan_library_name "libclang_rt.asan-aarch64.so")
        else()
            message(FATAL_ERROR "Unsupported Linux ASan processor: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()

        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} "-print-file-name=${_dpe_asan_library_name}"
            OUTPUT_VARIABLE _dpe_asan_runtime
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
        )
        if(NOT IS_ABSOLUTE "${_dpe_asan_runtime}" OR NOT EXISTS "${_dpe_asan_runtime}")
            message(FATAL_ERROR "Clang ASan shared runtime was not found: ${_dpe_asan_runtime}")
        endif()

        if(ARGC GREATER 1 AND ARGV1 STREQUAL "NATIVE_HOST")
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
                "DPE_ASAN_RUNTIME=${_dpe_asan_runtime}"
                "ASAN_OPTIONS=detect_leaks=0"
            )
        else()
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
                "LD_PRELOAD=${_dpe_asan_runtime}"
                "ASAN_OPTIONS=detect_leaks=0"
            )
        endif()
    elseif(DPE_ENABLE_ASAN AND APPLE)
        set(_dpe_asan_library_name "libclang_rt.asan_osx_dynamic.dylib")
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} "-print-file-name=${_dpe_asan_library_name}"
            OUTPUT_VARIABLE _dpe_asan_runtime
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
        )
        if(NOT IS_ABSOLUTE "${_dpe_asan_runtime}" OR NOT EXISTS "${_dpe_asan_runtime}")
            message(FATAL_ERROR "Apple Clang ASan dynamic runtime was not found: ${_dpe_asan_runtime}")
        endif()

        if(ARGC GREATER 1 AND ARGV1 STREQUAL "NATIVE_HOST")
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
                "DPE_ASAN_RUNTIME=${_dpe_asan_runtime}"
                "ASAN_OPTIONS=detect_leaks=0"
            )
        else()
            set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
                "DYLD_INSERT_LIBRARIES=${_dpe_asan_runtime}"
                "ASAN_OPTIONS=detect_leaks=0"
                "DPE_ASAN_RUNTIME=${_dpe_asan_runtime}"
            )
        endif()
    endif()
endfunction()
