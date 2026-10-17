cmake_minimum_required(VERSION 3.16)
project(At-Work LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/gui.cpp
    src/discord_ipc.cpp
    src/calendar_sync.cpp
    src/config.cpp
)

add_executable(At-Work WIN32 ${SOURCES})

target_link_libraries(At-Work PRIVATE
    winhttp
    comctl32
    user32
    gdi32
    shell32
    advapi32
    gdiplus
    ole32
)

target_compile_definitions(At-Work PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

