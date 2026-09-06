# smoke test は常時ビルド可能（hosted 依存なし）。wasip1 では Catch2 の代わりに smoke のみ検証する。
add_executable(smoke smoke.cpp)
target_link_libraries(smoke PRIVATE keikoupp::keikoupp)
target_compile_features(smoke PUBLIC cxx_std_23)
add_test(NAME smoke COMMAND smoke)

# wasip1/wasip2 ターゲット時は Catch2 をスキップ（Catch2 が <signal.h> 等の WASI 未対応ヘッダに依存するため）
if(VCPKG_TARGET_TRIPLET MATCHES "wasm32-wasip[12]")
  return()
endif()

if(EMSCRIPTEN)
  # Emscripten: vcpkg の Catch2 は -sNO_DISABLE_EXCEPTION_CATCHING なしでビルドされているため、
  # 例外ベースのテストが正しく動作しない。wasip1 と同様 smoke のみを検証し all_test はスキップする。
  # 完全な Emscripten テストが必要になったら frozenchars/injamm の fetchContent 方式を導入する。
  return()
endif()

find_package(Catch2 REQUIRED CONFIG)

file(GLOB test_src test_*.cpp)

add_executable(all_test ${test_src})
target_link_libraries(all_test PRIVATE keikoupp::keikoupp Catch2::Catch2 Catch2::Catch2WithMain)
target_compile_features(all_test PUBLIC cxx_std_23)
add_test(NAME all_test COMMAND all_test)
