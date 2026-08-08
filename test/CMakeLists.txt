add_executable(keikoupp_smoke smoke_test.cpp)
target_link_libraries(keikoupp_smoke PRIVATE keikoupp::keikoupp)
add_test(NAME smoke COMMAND keikoupp_smoke)