find_package(Catch2 REQUIRED CONFIG)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE keikoupp::keikoupp Catch2::Catch2 Catch2::Catch2WithMain)
target_compile_features(test_config PUBLIC cxx_std_23)
add_test(NAME test_config COMMAND test_config)

add_executable(test_analyzer test_analyzer.cpp)
target_link_libraries(test_analyzer PRIVATE keikoupp::keikoupp Catch2::Catch2 Catch2::Catch2WithMain)
target_compile_features(test_analyzer PUBLIC cxx_std_23)
add_test(NAME test_analyzer COMMAND test_analyzer)