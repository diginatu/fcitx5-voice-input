cmake_minimum_required(VERSION 3.21)
project(fcitx5-voiceinput VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)

pkg_check_modules(PULSEAUDIO libpulse-simple)
find_package(CURL)

add_subdirectory(src)

# Optional: install schemas and icons
install(FILES data/org.example.fcitx5.voiceinput.gschema.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/glib-2.0/schemas)
