cmake_minimum_required(VERSION 3.24)
project(OdrEditor VERSION 0.1.0 LANGUAGES C CXX)

include(cmake/Dependencies.cmake)
set(ODREDITOR_OPENDRIVE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "libOpenDRIVE checkout")
option(ODREDITOR_USE_INSTALLED_OPENDRIVE "Use an installed OpenDrive target" OFF)
option(ODREDITOR_BUILD_APP "Build the browser application" ${EMSCRIPTEN})
option(ODREDITOR_BUILD_TESTS "Build editor behavior tests" OFF)

if(NOT TARGET OpenDrive::OpenDrive)
    if(ODREDITOR_USE_INSTALLED_OPENDRIVE)
        find_package(magic_enum CONFIG REQUIRED)
        find_package(OpenDrive CONFIG REQUIRED)
    else()
        FetchContent_MakeAvailable(pugixml fmtlib magic_enum)
        add_subdirectory("${ODREDITOR_OPENDRIVE_SOURCE}" upstream EXCLUDE_FROM_ALL)
    endif()
endif()

add_library(OdrEditorCore STATIC src/document/XodrDocument.cpp src/edit/Commands.cpp)
target_include_directories(OdrEditorCore PUBLIC src)
target_compile_features(OdrEditorCore PUBLIC cxx_std_17)
target_link_libraries(OdrEditorCore PUBLIC pugixml::pugixml)
if(EMSCRIPTEN)
    target_compile_options(OdrEditorCore PUBLIC -fexceptions)
endif()
if(ODREDITOR_BUILD_TESTS)
    enable_testing()
    add_executable(DocumentHistoryTests tests/DocumentHistory.cpp)
    target_link_libraries(DocumentHistoryTests PRIVATE OdrEditorCore)
    add_test(NAME DocumentHistory COMMAND DocumentHistoryTests)
endif()

if(ODREDITOR_BUILD_APP)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "The app requires Emscripten; disable ODREDITOR_BUILD_APP for native core tests.")
    endif()
    if(NOT EMSCRIPTEN_VERSION VERSION_EQUAL ODREDITOR_EMSCRIPTEN_VERSION)
        message(STATUS "Tested with Emscripten ${ODREDITOR_EMSCRIPTEN_VERSION}; using ${EMSCRIPTEN_VERSION}")
    endif()
    FetchContent_MakeAvailable(imgui)
    add_library(OdrEditorUi STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(OdrEditorUi PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
    target_compile_options(OdrEditorUi PRIVATE -sUSE_SDL=2)
    target_compile_definitions(OdrEditorUi PRIVATE IMGUI_IMPL_OPENGL_ES3)

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" ODREDITOR_DEPENDENCY_LICENSES)
    file(READ "${ODREDITOR_OPENDRIVE_SOURCE}/LICENSE" ODREDITOR_LICENSE)
    configure_file(web/Notices.h.in generated/Notices.h @ONLY)
    add_executable(OdrEditor src/platform/Main.cpp src/platform/FileBridge.cpp src/ui/Theme.cpp src/ui/EditorApp.cpp)
    target_include_directories(OdrEditor PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(OdrEditor PRIVATE cxx_std_17)
    target_compile_options(OdrEditor PRIVATE -Wall -Wextra -sUSE_SDL=2 -fexceptions)
    target_link_libraries(OdrEditor PRIVATE OdrEditorUi OdrEditorCore OpenDrive::OpenDrive)
    target_link_options(OdrEditor PRIVATE
        -sUSE_SDL=2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
        -sSINGLE_FILE=1 -sMINIFY_HTML=0 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web
        -sEXIT_RUNTIME=0 -fexceptions -sDISABLE_EXCEPTION_CATCHING=0
        "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/web/file-bridge.js"
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    set_target_properties(OdrEditor PROPERTIES OUTPUT_NAME odreditor SUFFIX .html)
    set_property(TARGET OdrEditor APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html" "${CMAKE_CURRENT_SOURCE_DIR}/web/file-bridge.js")
endif()
