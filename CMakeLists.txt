cmake_minimum_required(VERSION 3.20)

project(YimMenu CXX ASM_MASM)

set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

# Git commit embed
include(scripts/git.cmake)

# Fetch modules
message("\nFetching modules")
include(scripts/minhook.cmake)
include(scripts/async-logger.cmake)
include(scripts/pugixml.cmake)
include(scripts/json.cmake)
include(scripts/cpr.cmake)
include(scripts/zydis.cmake)

message("\nFetching custom modules")
include(scripts/imgui.cmake)
include(scripts/gtav-classes.cmake)


# YimMenu
message(STATUS "YimMenu")
file(GLOB_RECURSE SRC_MAIN
    "${SRC_DIR}/**.hpp"
    "${SRC_DIR}/**.h"
    "${SRC_DIR}/**.cpp"
    "${SRC_DIR}/**.cc"
    "${SRC_DIR}/**.cxx"
    "${SRC_DIR}/**.asm"
)
add_library(YimMenu MODULE "${SRC_MAIN}"
            src/features.h
        src/features.h
        src/rgsc/rlpc.hpp
        src/rgsc/paragon/RgscRegistration.cpp
        src/rgsc/paragon/RgscRegistration.hpp)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(TARGET YimMenu PROPERTY CXX_STANDARD 23) # 23 Because std::format is not avalible in std:c++20 for some reason. Maybe it's because i use v142 toolset.

source_group(TREE ${SRC_DIR} PREFIX "src" FILES ${SRC_MAIN} )

target_include_directories(YimMenu PRIVATE 
    "${SRC_DIR}"
    "${json_SOURCE_DIR}/single_include"
    "${gtav_classes_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}"
)

set_target_properties(YimMenu PROPERTIES SUFFIX ".ipe")
set_target_properties(YimMenu PROPERTIES OUTPUT_NAME "Paragon.YimMenu")
set_target_properties(YimMenu PROPERTIES
        COMPILE_PDB_NAME "Paragon.YimMenu"
        COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb"
        LINK_FLAGS_RELEASE "/DEBUG /PDB:${CMAKE_BINARY_DIR}/Paragon.YimMenu.pdb /OPT:REF /OPT:ICF"
)

target_precompile_headers(YimMenu PRIVATE "${SRC_DIR}/common.hpp")
target_compile_options(YimMenu PRIVATE /FI${SRC_DIR}/features.h)
target_link_libraries(YimMenu PRIVATE pugixml minhook AsyncLogger dbghelp imgui cpr Zydis)

# Warnings as errors
set_property(TARGET YimMenu PROPERTY COMPILE_WARNING_AS_ERROR ON)

add_compile_definitions(YimMenu 
    "_CRT_SECURE_NO_WARNINGS"
    "NOMINMAX"
    "WIN32_LEAN_AND_MEAN"
)