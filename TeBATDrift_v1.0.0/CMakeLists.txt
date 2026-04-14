cmake_minimum_required(VERSION 3.16)
project(GarfieldDriftRes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{GARFIELD_INSTALL})
  message(FATAL_ERROR "GARFIELD_INSTALL environment variable is not set.")
endif()

set(GARFIELD_ROOT $ENV{GARFIELD_INSTALL})

find_library(GARFIELD_LIBRARY
  NAMES Garfield libGarfield.so
  PATHS
    ${GARFIELD_ROOT}/lib
    ${GARFIELD_ROOT}/lib64
  NO_DEFAULT_PATH
)

find_path(GARFIELD_INCLUDE_DIR
  NAMES Garfield/MediumMagboltz.hh
  PATHS
    ${GARFIELD_ROOT}/include
  NO_DEFAULT_PATH
)

if(NOT GARFIELD_LIBRARY)
  message(FATAL_ERROR "Could not find libGarfield.so under ${GARFIELD_ROOT}/lib or lib64")
endif()

if(NOT GARFIELD_INCLUDE_DIR)
  message(FATAL_ERROR "Could not find Garfield headers under ${GARFIELD_ROOT}/include")
endif()

# ROOT flags for TApplication / TCanvas / GUI classes.
execute_process(
  COMMAND root-config --cflags
  OUTPUT_VARIABLE ROOT_CFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND root-config --glibs
  OUTPUT_VARIABLE ROOT_GLIBS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

separate_arguments(ROOT_CFLAGS_LIST UNIX_COMMAND "${ROOT_CFLAGS}")
separate_arguments(ROOT_GLIBS_LIST UNIX_COMMAND "${ROOT_GLIBS}")

message(STATUS "GARFIELD_ROOT        = ${GARFIELD_ROOT}")
message(STATUS "GARFIELD_LIBRARY     = ${GARFIELD_LIBRARY}")
message(STATUS "GARFIELD_INCLUDE_DIR = ${GARFIELD_INCLUDE_DIR}")
message(STATUS "ROOT_CFLAGS          = ${ROOT_CFLAGS}")
message(STATUS "ROOT_GLIBS           = ${ROOT_GLIBS}")

add_executable(GarfieldDriftRes
  src/main.cpp
)

target_include_directories(GarfieldDriftRes PRIVATE ${GARFIELD_INCLUDE_DIR})
target_compile_options(GarfieldDriftRes PRIVATE ${ROOT_CFLAGS_LIST})
target_link_libraries(GarfieldDriftRes PRIVATE ${GARFIELD_LIBRARY} ${ROOT_GLIBS_LIST})

# GCC 8 needs explicit filesystem linkage.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(GarfieldDriftRes PRIVATE stdc++fs)
endif()
