cmake_minimum_required(VERSION 3.25)
project(rerevved_tools VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

include(cmake/dependencies.cmake)

add_library(rerevved_tools_dr_mp3 STATIC src/third_party/dr_mp3.cpp)
target_include_directories(rerevved_tools_dr_mp3 SYSTEM PRIVATE ${dr_libs_SOURCE_DIR})

add_library(rerevved_tools_stb_dxt STATIC src/third_party/stb_dxt.cpp)
target_include_directories(rerevved_tools_stb_dxt SYSTEM PRIVATE ${stb_SOURCE_DIR})

add_library(rerevved_tools_stb_image STATIC src/third_party/stb_image.cpp)
target_include_directories(rerevved_tools_stb_image SYSTEM PRIVATE ${stb_SOURCE_DIR})

add_library(rerevved_tools_core STATIC
    src/core/asset_pack_writer.cpp
    src/core/dds_document.cpp
    src/core/fpk_table.cpp
    src/core/dds_metadata.cpp
    src/core/dds_writer.cpp
    src/core/file_inspection.cpp
    src/core/file_kind.cpp
    src/core/fpk_document.cpp
    src/core/fpk_extraction.cpp
    src/core/fpk_index.cpp
    src/core/fpk_writer.cpp
    src/core/gfx_document.cpp
    src/core/heights_dds.cpp
    src/core/map_bundle_writer.cpp
    src/core/map_document.cpp
    src/core/mp3_document.cpp
    src/core/nif_document.cpp
    src/core/nif_model.cpp
    src/core/scenario_document.cpp
    src/core/terrain_dds.cpp
    src/core/terrain_generator.cpp
    src/core/texture_import.cpp
    src/core/xbox_dds.cpp
    src/core/xbox_texture.cpp
)
target_include_directories(rerevved_tools_core PUBLIC api)
target_include_directories(rerevved_tools_core SYSTEM PRIVATE ${dr_libs_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(rerevved_tools_core PRIVATE rerevved_tools_dr_mp3 rerevved_tools_stb_dxt rerevved_tools_stb_image)

function(rerevved_tools_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

rerevved_tools_enable_warnings(rerevved_tools_core)

set(REREVVED_TOOLS_MAP_SUITE_SOURCES
    src/app/map/creator_workspace.cpp
    src/app/map/map_draft_store.cpp
    src/app/map/map_edit_model.cpp
    src/app/map/map_images.cpp
    src/app/map/map_pack_export.cpp
    src/app/map/map_suite.cpp
    src/app/map/map_wizard.cpp
    src/app/map/scenario_form.cpp
)
add_executable(rerevved-map-suite ${REREVVED_TOOLS_MAP_SUITE_SOURCES} src/app/map/map_suite_main.cpp)
target_include_directories(rerevved-map-suite PRIVATE src/app)
target_link_libraries(rerevved-map-suite PRIVATE rerevved_tools_core)
rerevved_tools_enable_warnings(rerevved-map-suite)

if(BUILD_TESTING)
    include(tests/rerevved_tools_tests.cmake)
endif()
