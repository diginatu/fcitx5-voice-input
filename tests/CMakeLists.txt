add_executable(wav_header_test wav_header_test.cpp)
target_include_directories(wav_header_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME wav_header_test COMMAND wav_header_test)

add_executable(speech_recognizer_test speech_recognizer_test.cpp)
target_include_directories(speech_recognizer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME speech_recognizer_test COMMAND speech_recognizer_test)

add_executable(voiceinput_config_test voiceinput_config_test.cpp)
target_include_directories(voiceinput_config_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voiceinput_config_test PRIVATE Fcitx5::Core Fcitx5::Utils)
add_test(NAME voiceinput_config_test COMMAND voiceinput_config_test)

add_executable(capture_delivery_test capture_delivery_test.cpp)
target_include_directories(capture_delivery_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME capture_delivery_test COMMAND capture_delivery_test)

add_executable(history_test history_test.cpp ${CMAKE_SOURCE_DIR}/src/history.cpp)
target_include_directories(history_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME history_test COMMAND history_test)
