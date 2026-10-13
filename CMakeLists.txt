cmake_minimum_required(VERSION 3.20)
project(ArgonUI VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(ARGONUI_BUILD_D3D11    ON  CACHE BOOL "Build the Direct3D 11 backend")
set(ARGONUI_BUILD_D3D12    ON  CACHE BOOL "Build the Direct3D 12 backend")
set(ARGONUI_BUILD_VULKAN   ON  CACHE BOOL "Build the Vulkan backend")
set(ARGONUI_BUILD_OPENGL   ON  CACHE BOOL "Build the OpenGL backend")
set(ARGONUI_BUILD_DEMO     ON  CACHE BOOL "Build the demo application")
set(ARGONUI_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat warnings as errors")
set(ARGONUI_SHARED         OFF CACHE BOOL "Build ArgonUI as a shared library")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Put binaries somewhere predictable regardless of generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Core library - contains no GPU SDK include anywhere.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ARGONUI_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Draw/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Text/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Input/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Anim/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Widgets/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Backend/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Context.cpp
)

add_library(argonui_core ${ARGONUI_CORE_SOURCES})
add_library(ArgonUI::Core ALIAS argonui_core)

target_include_directories(argonui_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

target_compile_features(argonui_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(argonui_core PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(ARGONUI_WARNINGS_AS_ERRORS)
        target_compile_options(argonui_core PRIVATE /WX)
    endif()
    target_compile_definitions(argonui_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(argonui_core PRIVATE -Wall -Wextra -Wpedantic)
    if(ARGONUI_WARNINGS_AS_ERRORS)
        target_compile_options(argonui_core PRIVATE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Backend libraries. Each is independent: drop one out of the link line and the
# core still builds and runs against whichever ones remain.
# ---------------------------------------------------------------------------

# ---- Direct3D 11 ----
if(ARGONUI_BUILD_D3D11)
    add_library(argonui-d3d11 STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d11/D3D11Backend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d11/D3D11Backend.h)
    target_link_libraries(argonui-d3d11 PUBLIC argonui_core PRIVATE d3dcompiler dxgi d3d11)
    target_compile_definitions(argonui-d3d11 PRIVATE ARGON_WITH_D3D11=1)
    set(ARGONUI_D3D11_TARGET argonui-d3d11)
endif()

# ---- Direct3D 12 ----
if(ARGONUI_BUILD_D3D12)
    add_library(argonui-d3d12 STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d12/D3D12Backend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d12/D3D12Backend.h)
    target_link_libraries(argonui-d3d12 PUBLIC argonui_core PRIVATE d3d12 dxgi dxguid)
    target_compile_definitions(argonui-d3d12 PRIVATE ARGON_WITH_D3D12=1)
endif()

# ---- Vulkan ----
# Which window-system surface to compile for. Leave empty and the backend picks
# from VK_USE_PLATFORM_*, which the backend source defines for the current OS.
# Override with the ..._EXTENSION_NAME macro, not the bare token:
#   cmake -DARGON_VK_SURFACE_EXT=VK_KHR_XCB_SURFACE_EXTENSION_NAME ..
set(ARGON_VK_SURFACE_EXT "" CACHE STRING
    "Vulkan surface extension macro; empty means pick from the platform")

if(ARGONUI_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    add_library(argonui-vulkan STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/VulkanBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/VulkanBackend.h)
    target_link_libraries(argonui-vulkan PRIVATE argonui_core Vulkan::Vulkan)
    set(ARGONUI_VULKAN_TARGET argonui-vulkan)
    target_compile_definitions(argonui-vulkan PRIVATE ARGON_WITH_VULKAN=1)
    if(ARGON_VK_SURFACE_EXT)
        target_compile_definitions(argonui-vulkan PRIVATE
            ARGON_VK_SURFACE_EXT=${ARGON_VK_SURFACE_EXT})
    endif()
endif()

# ---- OpenGL ----
if(ARGONUI_BUILD_OPENGL)
    add_library(argonui-opengl STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/GLBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/GLBackend.h
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/GLLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/backends/opengl/GLLoader.h)
    target_compile_definitions(argonui-opengl PRIVATE ARGON_WITH_OPENGL=1)
    # argonui_core first: it carries the repo root on the include path, which is
    # what makes "backends/opengl/GLBackend.h" resolve. The GL sources also need
    # the interface headers from it.
    if(WIN32)
        target_link_libraries(argonui-opengl PRIVATE argonui_core opengl32 gdi32 user32)
    else()
        target_link_libraries(argonui-opengl PRIVATE argonui_core)
    endif()
    set(ARGONUI_OPENGL_TARGET argonui-opengl)
endif()

# ---------------------------------------------------------------------------
# Demo
# ---------------------------------------------------------------------------
if(ARGONUI_BUILD_DEMO AND WIN32)
    add_executable(argonui_demo WIN32
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo/DemoMain.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo/DemoMenu.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo/Win32Host.cpp)
    target_link_libraries(argonui_demo PRIVATE argonui_core user32 gdi32)
    if(ARGONUI_D3D11_TARGET)
        target_link_libraries(argonui_demo PRIVATE ${ARGONUI_D3D11_TARGET})
    endif()
    if(ARGONUI_OPENGL_TARGET)
        target_link_libraries(argonui_demo PRIVATE ${ARGONUI_OPENGL_TARGET})
    endif()
    if(ARGONUI_VULKAN_TARGET)
        target_link_libraries(argonui_demo PRIVATE ${ARGONUI_VULKAN_TARGET})
    endif()
    # The entry point is wWinMain. MSVC resolves the wide one from the
    # /SUBSYSTEM:WINDOWS that add_executable(WIN32) implies; MinGW needs to be
    # told explicitly, or the link fails looking for WinMain.
    if(MINGW)
        target_link_options(argonui_demo PRIVATE -municode)
    endif()
    set_target_properties(argonui_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()

# ---------------------------------------------------------------------------
# Tests
#
# Headless: the core library against the null backend, no GPU and no window.
# The reference menu from examples/demo is driven for several frames, so this
# covers layout, widget state, batching and font baking without touching a
# graphics API.
# ---------------------------------------------------------------------------
set(ARGONUI_BUILD_TESTS ON CACHE BOOL "Build the headless tests")
if(ARGONUI_BUILD_TESTS)
    enable_testing()
    add_executable(argonui_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/headless_tests.cpp)
    target_link_libraries(argonui_tests PRIVATE argonui_core)
    target_include_directories(argonui_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo)
    if(WIN32)
        target_link_libraries(argonui_tests PRIVATE user32 gdi32)
    endif()
    # DemoMenu.cpp is the reference layout; the tests drive it directly so a
    # regression in the shipped example fails the test run too.
    target_sources(argonui_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo/DemoMenu.cpp)
    add_test(NAME headless COMMAND argonui_tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS argonui_core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/argonui DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "ArgonUI ${PROJECT_VERSION}")
message(STATUS "  build type ....... ${CMAKE_BUILD_TYPE}")
message(STATUS "  D3D11 ............ ${ARGONUI_BUILD_D3D11}")
message(STATUS "  D3D12 ............ ${ARGONUI_BUILD_D3D12}")
message(STATUS "  Vulkan ........... ${ARGONUI_BUILD_VULKAN}")
message(STATUS "  OpenGL ........... ${ARGONUI_BUILD_OPENGL}")
message(STATUS "  demo ............. ${ARGONUI_BUILD_DEMO}")
message(STATUS "  tests ............ ${ARGONUI_BUILD_TESTS}")
message(STATUS "")
