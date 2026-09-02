foreach(ex sensor bandwidth demo)
    add_executable(example_${ex} ${ex}.cpp)
    target_link_libraries(example_${ex} PRIVATE keikoupp::keikoupp)
    target_compile_features(example_${ex} PRIVATE cxx_std_23)
    add_test(NAME example_${ex} COMMAND example_${ex})
endforeach()

# ベンチマーク (ctest には含めない。時間のかかる計測なので手動実行)
add_executable(bench bench.cpp)
target_link_libraries(bench PRIVATE keikoupp::keikoupp)
target_compile_features(bench PRIVATE cxx_std_23)

# WASI minimal (wasip1) では例外を使わない bench/demo がリンクできるよう -fno-exceptions を付与
# sensor/bandwidth は wasip1 でもそのままリンクできるが、統一して例外なしでビルドしても動作する
if(ENABLE_WASI_MINIMAL)
    if(NOT MSVC)
        target_compile_options(example_demo PRIVATE -fno-exceptions)
        target_compile_options(bench PRIVATE -fno-exceptions)
    endif()
endif()
