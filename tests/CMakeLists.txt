include(GoogleTest)

# Each C++ source, including sources in subdirectories, is a separate test executable.
# CONFIGURE_DEPENDS reruns CMake when test files are added or removed.
file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)

foreach(test_source IN LISTS test_sources)
    string(REGEX REPLACE "\\.[^.]+$" "" test_name "${test_source}")
    string(REPLACE "/" "__" test_name "${test_name}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" test_name "${test_name}")
    set(test_target "onejson_${test_name}")

    if(TARGET "${test_target}")
        message(FATAL_ERROR "Duplicate test target ${test_target} generated from ${test_source}.")
    endif()

    add_executable("${test_target}" "${test_source}")
    target_link_libraries("${test_target}" PRIVATE onejson::onejson GTest::gtest_main)
    gtest_discover_tests("${test_target}"
        TEST_PREFIX "${test_target}."
        DISCOVERY_TIMEOUT 30
    )
endforeach()
