cmake_minimum_required(VERSION 3.21)
project(ZapretGui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include(FetchContent)
FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG release/1
    SOURCE_SUBDIR api/cpp
)
FetchContent_MakeAvailable(Slint)

set(APP_SOURCES
    src/main.cpp
    src/SelfInstaller.cpp
    src/ShortcutUtils.cpp
    src/HttpTester.cpp
    src/ZapretManager.cpp
    src/AutoUpdater.cpp
    src/resources.rc
)

add_executable(Zapret WIN32 ${APP_SOURCES})

target_include_directories(Zapret PRIVATE src)

slint_target_sources(Zapret src/ui.slint)

target_link_libraries(Zapret PRIVATE 
    Slint::Slint
    winhttp 
    shell32 
    ole32 
    userenv
)

if(MSVC)
    target_link_options(Zapret PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

if(WIN32)
    add_custom_command(
        TARGET Zapret POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:Zapret> $<TARGET_FILE_DIR:Zapret>
        COMMAND_EXPAND_LISTS
    )
endif()
