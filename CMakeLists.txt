cmake_minimum_required(VERSION 3.16)
project(fits_toolkit VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/c_core/include)

set(LIBFITS_SOURCES
    c_core/src/fits_endian.c
    c_core/src/fits_header.c
    c_core/src/fits_core.c
)

add_library(fits SHARED ${LIBFITS_SOURCES})

set_target_properties(fits PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "fits"
)

add_executable(fits_cli c_core/src/main_cli.c)
target_link_libraries(fits_cli PRIVATE fits)