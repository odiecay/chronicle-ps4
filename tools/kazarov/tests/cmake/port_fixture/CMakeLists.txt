# Consumer fixture for tests/cmake/test_port.py: configures the shared port the way a game does.
cmake_minimum_required(VERSION 3.28)

if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()
project(port_fixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT KAZAROV_DIR)
    message(FATAL_ERROR "pass -DKAZAROV_DIR=<kazarov root>")
endif()
include(${KAZAROV_DIR}/cmake/KazarovPort.cmake)

option(FIXTURE_BAD_FAMILY "Name an SDK family with no sources" OFF)
if(FIXTURE_BAD_FAMILY)
    kz_port_sce_sources(BAD LIBS libnope)
endif()

kz_port_host_library(fixture_host)

kz_port_sce_sources(FIXTURE_SCE LIBS libvu0 libpad libmc sifdev sifrpc eekernel libcdvd libgraph libdma libpkt)
list(JOIN FIXTURE_SCE "\n" sce_text)
file(WRITE ${CMAKE_BINARY_DIR}/sce_sources.txt "${sce_text}\n")

add_executable(fixture_tests ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
kz_port_tests(fixture_tests)
get_target_property(test_sources fixture_tests SOURCES)
list(JOIN test_sources "\n" test_text)
file(WRITE ${CMAKE_BINARY_DIR}/test_sources.txt "${test_text}\n")
