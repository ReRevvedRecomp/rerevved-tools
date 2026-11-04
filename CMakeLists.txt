cmake_minimum_required(VERSION 3.25)
project(rerevved_tools VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

include(cmake/dependencies.cmake)

# Fonts and the wordmark are embedded so the app renders identically without loose asset files.
set(REREVVED_TOOLS_FONT_SOURCES)
foreach(font IN ITEMS
        "kInterRegular|${inter_SOURCE_DIR}/extras/ttf/Inter-Regular.ttf"
        "kInterSemiBold|${inter_SOURCE_DIR}/extras/ttf/Inter-SemiBold.ttf"
        "kLucideIcons|${lucide_SOURCE_DIR}/font/lucide.ttf"
        "kRerevvedWordmark|${CMAKE_CURRENT_SOURCE_DIR}/res/rerevved_wordmark.png")
    string(REPLACE "|" ";" parts "${font}")
    list(GET parts 0 symbol)
    list(GET parts 1 input)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/fonts/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${input} -DOUTPUT=${output} -DSYMBOL=${symbol}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
        DEPENDS "${input}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake"
        VERBATIM
    )
    list(APPEND REREVVED_TOOLS_FONT_SOURCES "${output}")
endforeach()
add_library(rerevved_tools_fonts STATIC ${REREVVED_TOOLS_FONT_SOURCES})

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_library(rerevved_tools_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    src/app/smoke/ui_probe.cpp
)
target_include_directories(rerevved_tools_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_include_directories(rerevved_tools_imgui PRIVATE src/app)
# Item hooks feed scripted smoke input (src/app/smoke/ui_probe.cpp). They stay off unless enabled.
target_compile_definitions(rerevved_tools_imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
target_link_libraries(rerevved_tools_imgui PUBLIC glfw OpenGL::GL)

add_library(rerevved_tools_dr_mp3 STATIC src/third_party/dr_mp3.cpp)
target_include_directories(rerevved_tools_dr_mp3 SYSTEM PRIVATE ${dr_libs_SOURCE_DIR})

add_library(rerevved_tools_stb_dxt STATIC src/third_party/stb_dxt.cpp)
target_include_directories(rerevved_tools_stb_dxt SYSTEM PRIVATE ${stb_SOURCE_DIR})

add_library(rerevved_tools_stb_image STATIC src/third_party/stb_image.cpp)
target_include_directories(rerevved_tools_stb_image SYSTEM PRIVATE ${stb_SOURCE_DIR})

add_library(rerevved_tools_stb_image_write STATIC src/third_party/stb_image_write.cpp)
target_include_directories(rerevved_tools_stb_image_write SYSTEM PUBLIC ${stb_SOURCE_DIR})

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

add_executable(rerevved-tools
    src/app/application.cpp
    src/app/inspect/archive_explorer.cpp
    src/app/inspect/archive_navigation.cpp
    src/app/inspect/asset_inspector.cpp
    src/app/inspect/asset_inspector_format.cpp
    src/app/map/creator_workspace.cpp
    src/app/inspect/dds_viewer.cpp
    src/app/inspect/document_workspace.cpp
    src/app/map/export_panel.cpp
    src/app/map/map_pack_export.cpp
    src/app/map/map_images.cpp
    src/app/inspect/source_browser.cpp
    src/app/inspect/sources_pane.cpp
    src/app/inspect/gfx_inspector.cpp
    src/app/inspect/inspection_format.cpp
    src/app/inspect/inspection_pane.cpp
    src/app/inspect/inspector_navigation.cpp
    src/app/inspect/inspector_navigation_ui.cpp
    src/app/map/map_edit_model.cpp
    src/app/map/map_draft_store.cpp
    src/app/map/map_editor.cpp
    src/app/map/map_preview.cpp
    src/app/map/map_wizard.cpp
    src/app/main.cpp
    src/app/inspect/mp3_preview.cpp
    src/app/inspect/nif_inspector.cpp
    src/app/inspect/nif_inspector_format.cpp
    src/app/inspect/nif_preview.cpp
    src/app/inspect/nif_preview_layout.cpp
    src/app/ui/path_modal.cpp
    src/app/map/scenario_editor.cpp
    src/app/map/scenario_form.cpp
    src/app/smoke/smoke_capture.cpp
    src/app/smoke/smoke_input.cpp
    src/app/ui/ui_theme.cpp
    src/app/ui/ui_widgets.cpp
    src/app/ui/native_dialog.cpp
    src/app/ui/window_chrome.cpp
    src/app/ui/wordmark.cpp
)

if(WIN32)
    target_link_libraries(rerevved-tools PRIVATE ole32 shell32 uuid dwmapi)
    set(REREVVED_TOOLS_RC_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/res/rerevved_tools.ico")
    configure_file(res/rerevved_tools.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/rerevved_tools.rc" @ONLY)
    target_sources(rerevved-tools PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/rerevved_tools.rc")
endif()
target_include_directories(rerevved-tools PRIVATE src/app)
target_link_libraries(rerevved-tools PRIVATE
    rerevved_tools_core
    rerevved_tools_imgui
    rerevved_tools_stb_image_write
    rerevved_tools_fonts
    Threads::Threads
)
target_compile_definitions(rerevved-tools PRIVATE
    REREVVED_TOOLS_VERSION="${PROJECT_VERSION}"
)

function(rerevved_tools_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

rerevved_tools_enable_warnings(rerevved_tools_core)
rerevved_tools_enable_warnings(rerevved-tools)

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
