cmake_minimum_required(VERSION 3.16)

project(FileCollector LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(file_collector STATIC
    file_collector.cpp
)

add_library(FileCollector::FileCollector ALIAS file_collector)
target_compile_features(file_collector PUBLIC cxx_std_20)
target_include_directories(file_collector PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(file_collector PUBLIC Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(file_collector_tests tests/file_collector_test.cpp)
    target_link_libraries(file_collector_tests PRIVATE FileCollector::FileCollector GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(file_collector_tests)
endif()