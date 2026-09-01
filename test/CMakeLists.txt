find_package(Catch2 REQUIRED CONFIG)

file(GLOB test_src test_*.cpp)

add_executable(all_test ${test_src})
target_link_libraries(all_test PRIVATE keikoupp::keikoupp Catch2::Catch2 Catch2::Catch2WithMain)
target_compile_features(all_test PUBLIC cxx_std_23)
add_test(NAME all_test COMMAND all_test)

if(ENABLE_FREESTANDING)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # freestanding 検証: Catch2 を使わず libstdc++ リンクなしでビルド・実行する。
        # ライブラリが <algorithm>/<array>/<cmath>/<functional> や動的確保に逆戻りすると
        # コンパイルエラーまたはリンクエラー (operator new 等) で失敗する。
        add_executable(freestanding_check freestanding_check.cpp)
        target_link_libraries(freestanding_check PRIVATE keikoupp::keikoupp)
        target_compile_options(freestanding_check PRIVATE -ffreestanding -fno-exceptions -fno-rtti)
        target_link_options(freestanding_check PRIVATE -nostdlib++)
        add_test(NAME freestanding_check COMMAND freestanding_check)
    else()
        message(WARNING "ENABLE_FREESTANDING check test requires GNU/Clang")
    endif()
endif()