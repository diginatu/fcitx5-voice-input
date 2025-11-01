add_library(voiceinput SHARED
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

set_target_properties(voiceinput PROPERTIES PREFIX "")

install(TARGETS voiceinput
  LIBRARY DESTINATION ${FCITX_INSTALL_LIBDIR}/fcitx5)

configure_file(voiceinput-addon.conf.in voiceinput-addon.conf @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/voiceinput-addon.conf
  DESTINATION ${FCITX_INSTALL_PKGDATADIR}/addon)

install(FILES voiceinput.conf
  DESTINATION ${FCITX_INSTALL_PKGDATADIR}/configdesc)
