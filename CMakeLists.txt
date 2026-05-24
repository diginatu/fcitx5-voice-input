cmake_minimum_required(VERSION 3.21)
project(fcitx5-voice-input VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)

pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple)
find_package(CURL)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
