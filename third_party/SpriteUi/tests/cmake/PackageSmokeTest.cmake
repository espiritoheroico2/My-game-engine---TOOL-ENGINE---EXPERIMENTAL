if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR OR NOT DEFINED PROJECT_BINARY_DIR OR
   NOT DEFINED GENERATOR)
    message(FATAL_ERROR "Package smoke test requires SOURCE_DIR, BINARY_DIR, PROJECT_BINARY_DIR, and GENERATOR")
endif()

set(install_dir "${BINARY_DIR}/install")
set(consumer_build_dir "${BINARY_DIR}/consumer-build")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}" --prefix "${install_dir}" --config "${CONFIG}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "SpriteUI install failed:\n${install_stdout}\n${install_stderr}")
endif()

set(configure_args
    -S "${SOURCE_DIR}/tests/package_consumer"
    -B "${consumer_build_dir}"
    -G "${GENERATOR}"
    "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${EXE_LINKER_FLAGS}"
)
set(package_prefix_path "${install_dir}")
if(DEPENDENCY_PREFIX_PATH)
    list(APPEND package_prefix_path ${DEPENDENCY_PREFIX_PATH})
endif()
string(REPLACE ";" "\\;" package_prefix_path "${package_prefix_path}")
list(APPEND configure_args "-DCMAKE_PREFIX_PATH=${package_prefix_path}")
if(PACKAGE_HAS_SDL3)
    list(APPEND configure_args "-DPACKAGE_HAS_SDL3=ON")
endif()
if(CXX_COMPILER AND NOT GENERATOR MATCHES "Visual Studio")
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(CONFIG)
    list(APPEND configure_args "-DCMAKE_BUILD_TYPE=${CONFIG}")
endif()
if(GENERATOR_PLATFORM)
    list(APPEND configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
    list(APPEND configure_args -T "${GENERATOR_TOOLSET}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "SpriteUI consumer configure failed:\n${configure_stdout}\n${configure_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build_dir}" --config "${CONFIG}" --parallel
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "SpriteUI consumer build failed:\n${build_stdout}\n${build_stderr}")
endif()

message(STATUS "Installed SpriteUI package was found and linked by a separate consumer project")
