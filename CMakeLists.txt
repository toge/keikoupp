cmake_minimum_required(VERSION 3.25)

set(KEIKOUPP_VERSION "0.1.0")
project(keikoupp VERSION ${KEIKOUPP_VERSION} LANGUAGES CXX)

option(BUILD_TEST "Enable tests" ON)

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3")
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

add_library(keikoupp INTERFACE)
add_library(keikoupp::keikoupp ALIAS keikoupp)
target_compile_features(keikoupp INTERFACE cxx_std_23)

target_include_directories(keikoupp
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(BUILD_TEST)
    enable_testing()
    add_subdirectory(test)
    add_subdirectory(example)
endif()
