cmake_minimum_required(VERSION 3.16)
project(ZapretGui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Download Dear ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90
)
FetchContent_MakeAvailable(imgui)

# Gather ImGui source files for Win32 and DX11
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# App sources
set(APP_SOURCES
    src/main.cpp
    src/SelfInstaller.cpp
    src/ShortcutUtils.cpp
    src/HttpTester.cpp
    src/ZapretManager.cpp
    src/AutoUpdater.cpp
    src/resources.rc
)

# Add executable
add_executable(Zapret WIN32 ${APP_SOURCES} ${IMGUI_SOURCES})

# Include directories
target_include_directories(Zapret PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link libraries
target_link_libraries(Zapret PRIVATE d3d11 d3dcompiler winhttp shell32 ole32 userenv)

# Subsystem windows
set_target_properties(Zapret PROPERTIES WIN32_EXECUTABLE TRUE)

if(MSVC)
    target_link_options(Zapret PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()
