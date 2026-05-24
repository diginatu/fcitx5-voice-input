add_executable(wav_header_test wav_header_test.cpp)
target_include_directories(wav_header_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME wav_header_test COMMAND wav_header_test)
