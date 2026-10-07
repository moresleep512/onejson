cmake_minimum_required(VERSION 3.24)

project(base LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

if("$ENV{CXX_THIRD_PARTY_ROOT}" STREQUAL "")
    message(FATAL_ERROR "Set CXX_THIRD_PARTY_ROOT to the directory containing googletest and abseil-cpp.")
endif()

file(TO_CMAKE_PATH "$ENV{CXX_THIRD_PARTY_ROOT}" THIRD_PARTY_ROOT)
set(ABSEIL_SOURCE_DIR "${THIRD_PARTY_ROOT}/abseil-cpp")
set(GOOGLETEST_SOURCE_DIR "${THIRD_PARTY_ROOT}/googletest")

foreach(dependency_dir IN ITEMS "${ABSEIL_SOURCE_DIR}" "${GOOGLETEST_SOURCE_DIR}")
    if(NOT EXISTS "${dependency_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Dependency source directory is missing CMakeLists.txt: ${dependency_dir}")
    endif()
endforeach()

set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "Propagate Abseil's C++ standard")
set(ABSL_BUILD_TESTING OFF CACHE BOOL "Build Abseil's own tests")
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "Install Abseil")
add_subdirectory("${ABSEIL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/abseil-cpp" EXCLUDE_FROM_ALL)

add_library(onejson INTERFACE)
add_library(onejson::onejson ALIAS onejson)
target_include_directories(onejson INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(onejson INTERFACE cxx_std_23)
target_link_libraries(onejson INTERFACE absl::base absl::strings absl::flat_hash_map)

if(BUILD_TESTING)
    set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock")
    set(INSTALL_GTEST OFF CACHE BOOL "Install GoogleTest")
    # Match the dynamic MSVC runtime used by Abseil on Windows.
    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime" FORCE)
    add_subdirectory("${GOOGLETEST_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/googletest" EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
