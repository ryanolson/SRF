

add_executable(test_sre
    test_main.cpp
    test_coroutines.cpp
    test_tracing.cpp
)

target_link_libraries(test_sre
    PRIVATE
    ${PROJECT_NAME}::libsre
    GTest::gtest
)

# Necessary include to prevent IWYU from showing absolute paths
target_include_directories(test_sre
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

add_test(
    NAME test_sre
    COMMAND $<TARGET_FILE:test_sre>
)
