add_executable(OpenDriveWasm Embind.cpp)
target_link_libraries(OpenDriveWasm PRIVATE OpenDrive::OpenDrive)
target_compile_features(OpenDriveWasm PRIVATE cxx_std_17)
set_target_properties(OpenDriveWasm PROPERTIES
    OUTPUT_NAME libOpenDRIVE
    SUFFIX ".mjs"
)

# The parser catches C++ exceptions internally, so enable them in the library too.
target_compile_options(OpenDrive PRIVATE -fexceptions)
target_compile_options(OpenDriveWasm PRIVATE -fexceptions -Wall -Wextra)
target_link_options(OpenDriveWasm PRIVATE
    --bind
    -fexceptions
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createOpenDrive
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
)

install(FILES
    "$<TARGET_FILE:OpenDriveWasm>"
    "${CMAKE_CURRENT_BINARY_DIR}/libOpenDRIVE.wasm"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/OpenDrive/wasm"
)
