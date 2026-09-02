# WASI minimal 向けスモークは常時ビルド可能（hosted 依存なし）
add_executable(smoke_wasi_minimal smoke_wasi_minimal.cpp)
target_link_libraries(smoke_wasi_minimal PRIVATE keikoupp::keikoupp)
target_compile_features(smoke_wasi_minimal PUBLIC cxx_std_23)
target_compile_definitions(smoke_wasi_minimal PRIVATE KEIKOUPP_WASI_MINIMAL=1)
# WASI minimal の契約 (例外なしでビルド可能) を hosted でも検証する
target_compile_options(smoke_wasi_minimal PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-exceptions>)
add_test(NAME smoke_wasi_minimal COMMAND smoke_wasi_minimal)

if(ENABLE_WASI_MINIMAL)
  # WASI minimal ビルドでは hosted なテストはスキップ (Catch2 が <signal.h> 等の WASI 未対応ヘッダに依存するため)
  return()
endif()

find_package(Catch2 REQUIRED CONFIG)

file(GLOB test_src test_*.cpp)

add_executable(all_test ${test_src})
target_link_libraries(all_test PRIVATE keikoupp::keikoupp Catch2::Catch2 Catch2::Catch2WithMain)
target_compile_features(all_test PUBLIC cxx_std_23)
add_test(NAME all_test COMMAND all_test)
