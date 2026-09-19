cmake_minimum_required(VERSION 3.20)
project(oar_esp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Target: oar_esp.dll ──────────────────────────────────────────────────────
add_library(oar_esp SHARED
    src/dllmain.cpp
)

target_include_directories(oar_esp PRIVATE include)

# Windows-only, x64
target_compile_definitions(oar_esp PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# Optimise + strip debug info in Release
target_compile_options(oar_esp PRIVATE
    $<$<CONFIG:Release>:/O2 /Oi /GL /GR- /EHa>
    $<$<CONFIG:Debug>:/Od /Zi /EHa>
)

target_link_options(oar_esp PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
)

# Link Win32 libs
target_link_libraries(oar_esp PRIVATE
    user32
    gdi32
    psapi
)

# Output name
set_target_properties(oar_esp PROPERTIES
    OUTPUT_NAME "oar_esp"
    SUFFIX ".dll"
)
