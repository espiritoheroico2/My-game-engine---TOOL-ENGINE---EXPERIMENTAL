# Fuzzing. Dois motores:
#  - libFuzzer (clang com compiler-rt):  -DTOOL_FUZZ_ENGINE=libfuzzer
#  - driver proprio (qualquer compilador, com ASan/UBSan):  -DTOOL_FUZZ_ENGINE=standalone  (padrao)
# cmake -S . -B build/fuzz -DTOOL_BUILD_FUZZ=ON -DTOOL_BUILD_TESTS=OFF && cmake --build build/fuzz
# ./build/fuzz/bin/fuzz_json 1000000 42
set(TOOL_FUZZ_ENGINE "standalone" CACHE STRING "libfuzzer | standalone")
foreach(t json wav pack net narrative mod)
  string(TOUPPER ${t} T)
  add_executable(fuzz_${t} fuzz_targets.cpp)
  target_compile_definitions(fuzz_${t} PRIVATE FUZZ_${T})
  target_link_libraries(fuzz_${t} PRIVATE tool::audio tool::narrative tool::mod tool::net tool::pack)
  if(TOOL_FUZZ_ENGINE STREQUAL "libfuzzer")
    target_compile_options(fuzz_${t} PRIVATE -fsanitize=fuzzer,address,undefined -g -O1)
    target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer,address,undefined)
  else()
    target_compile_definitions(fuzz_${t} PRIVATE FUZZ_STANDALONE)
    target_compile_options(fuzz_${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    target_link_options(fuzz_${t} PRIVATE -fsanitize=address,undefined)
  endif()
endforeach()
