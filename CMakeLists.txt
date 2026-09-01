cmake_minimum_required(VERSION 3.16)

project(moneta LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MONETA_TUI "Build with ncurses TUI support" OFF)

add_subdirectory(external/monero-cpp)

set(MONETA_SOURCES
    src/main.cpp
)

if(MONETA_TUI)
    list(APPEND MONETA_SOURCES
        src/tui.cpp
    )
endif()

add_executable(moneta ${MONETA_SOURCES})

target_include_directories(moneta PRIVATE
    src
)

target_link_libraries(moneta PRIVATE
    monero-cpp
)

target_compile_options(moneta PRIVATE
    -Wall
    -Wextra
)

if(MONETA_TUI)
    find_package(Curses REQUIRED)

    target_include_directories(moneta PRIVATE
        ${CURSES_INCLUDE_DIR}
    )

    target_link_libraries(moneta PRIVATE
        ${CURSES_LIBRARIES}
    )

    target_compile_definitions(moneta PRIVATE
        MONETA_TUI
    )
endif()
