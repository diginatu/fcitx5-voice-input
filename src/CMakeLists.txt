add_fcitx5_addon(voiceinput
  voiceinput-module.cpp
  speech_recognizer.cpp
  audio_capture.cpp
)

target_link_libraries(voiceinput PRIVATE
  Fcitx5::Core
  ${PULSEAUDIO_LIBRARIES}
  CURL::libcurl
)

target_include_directories(voiceinput PRIVATE ${PULSEAUDIO_INCLUDE_DIRS})

install(TARGETS voiceinput
  LIBRARY DESTINATION ${FCITX_INSTALL_ADDONDIR})

configure_file(voiceinput.conf.in voiceinput.conf @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/voiceinput.conf
  DESTINATION ${FCITX_INSTALL_PKGDATADIR}/addon)

install(FILES voiceinput.conf
  DESTINATION ${FCITX_INSTALL_PKGDATADIR}/configdesc)
