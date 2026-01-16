cmake_minimum_required(VERSION 3.16)
project(MGS2AnimCapture)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build as a DLL
add_library(d3d11 SHARED
    d3d11_proxy.cpp
    bone_capture.cpp
    bone_capture.h
)

# Link against DirectX libraries
target_link_libraries(d3d11 PRIVATE
    d3d11
    dxgi
)

# Export the D3D11 create functions
set_target_properties(d3d11 PROPERTIES
    OUTPUT_NAME "d3d11"
    PREFIX ""
)

# DEF file for exports
set_target_properties(d3d11 PROPERTIES
    LINK_FLAGS "/DEF:${CMAKE_SOURCE_DIR}/d3d11.def"
)

# Installation target (copy to MGS2 directory)
install(TARGETS d3d11 RUNTIME DESTINATION "D:/Metal Gear Solid 2")
