cmake_minimum_required(VERSION 3.15)
project(VoiceChat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Оптимизация
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -ffast-math")

# Библиотеки
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# Один файл
add_executable(voice
    src/main.cpp
)

# Директории
target_include_directories(voice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPUS_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
)

# Линковка
target_link_libraries(voice PRIVATE
    ${OPUS_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    pthread
)
