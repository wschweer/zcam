cmake_minimum_required(VERSION 3.31)

project(sniffer LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_BUILD_TYPE Release)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_executable(sniffer
    src/sniffer.c
    src/os_common.c
)

target_link_libraries(sniffer PRIVATE PkgConfig::LIBUSB)
target_include_directories(sniffer PRIVATE src)

target_compile_options(sniffer PRIVATE -D_GNU_SOURCE -DOS_LINUX -Wall -Wextra)
