cmake_minimum_required(VERSION 3.25)

set(KEIKOUPP_VERSION "0.1.0")
project(keikoupp VERSION ${KEIKOUPP_VERSION} LANGUAGES CXX)

option(BUILD_TEST "Enable tests" ON)
option(ENABLE_WASI_MINIMAL "Enable WASI minimal (no exceptions) mode - wasi-sdk wasm32-wasip1 assumed" OFF)

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

add_library(keikoupp INTERFACE)
add_library(keikoupp::keikoupp ALIAS keikoupp)
target_compile_features(keikoupp INTERFACE cxx_std_23)

if(ENABLE_WASI_MINIMAL)
    target_compile_definitions(keikoupp INTERFACE KEIKOUPP_WASI_MINIMAL=1)
    target_compile_options(keikoupp INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
endif()

target_include_directories(keikoupp
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/keikoupp)
install(TARGETS keikoupp EXPORT keikouppTargets)
install(EXPORT keikouppTargets
    NAMESPACE keikoupp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keikoupp
)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/keikouppConfigVersion.cmake
    VERSION ${KEIKOUPP_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/keikouppConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/keikouppConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keikoupp
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/keikouppConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/keikouppConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keikoupp
)

if(BUILD_TEST)
    enable_testing()
    add_subdirectory(test)
    add_subdirectory(example)
endif()
