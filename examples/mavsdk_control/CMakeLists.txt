cmake_minimum_required(VERSION 3.10.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(mavsdk_control)

add_executable(mavsdk_control
    mavsdk_control.cpp
)

find_package(MAVSDK REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SFML2 REQUIRED sfml-graphics>=2.4 sfml-window>=2.4 sfml-system>=2.4)

target_link_libraries(mavsdk_control
    MAVSDK::mavsdk
)
target_link_libraries(mavsdk_control
MAVSDK::mavsdk
sfml-graphics
sfml-window
sfml-system
)

if(NOT MSVC)
    add_compile_options(mavsdk_control PRIVATE -Wall -Wextra)
else()
    add_compile_options(mavsdk_control PRIVATE -W2)
endif()
