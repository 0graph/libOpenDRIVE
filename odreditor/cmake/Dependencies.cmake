include(FetchContent)

set(ODREDITOR_EMSCRIPTEN_VERSION "6.0.5")
set(ODREDITOR_IMGUI_VERSION "v1.91.9b")
# SDL 2.32.10 is hash-pinned by the Emscripten 6.0.5 port.
set(ODREDITOR_PUGIXML_VERSION "v1.14")
set(ODREDITOR_FMT_VERSION "12.1.0")
set(ODREDITOR_MAGIC_ENUM_VERSION "v0.9.8")

FetchContent_Declare(pugixml GIT_REPOSITORY https://github.com/zeux/pugixml.git GIT_TAG ${ODREDITOR_PUGIXML_VERSION} GIT_SHALLOW TRUE)
FetchContent_Declare(fmtlib GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG ${ODREDITOR_FMT_VERSION} GIT_SHALLOW TRUE)
FetchContent_Declare(magic_enum GIT_REPOSITORY https://github.com/Neargye/magic_enum.git GIT_TAG ${ODREDITOR_MAGIC_ENUM_VERSION} GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG ${ODREDITOR_IMGUI_VERSION} GIT_SHALLOW TRUE)
