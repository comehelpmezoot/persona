cmake_minimum_required(VERSION 3.21)
project(PersonaShell VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Persona Shell for Windows only builds on Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(PERSONA_ENABLE_SMTC "Now-playing widget through Windows media sessions (C++/WinRT, MSVC only)" ON)
option(PERSONA_PRECOMPILE_SHADERS "Compile the wallpaper shaders to bytecode at build time (needs fxc.exe on PATH)" OFF)
option(PERSONA_BUILD_TESTS "Build the logic unit tests" ON)
set(PERSONA_ASSETS_DIR "" CACHE PATH "Assets folder of the original Persona-Quickshell repo")

find_package(Qt6 6.6 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Widgets Network Concurrent Multimedia Svg ShaderTools)

# ---- where are the original images / videos? -------------------------------------------------
if(NOT PERSONA_ASSETS_DIR)
    foreach(candidate
            "${CMAKE_SOURCE_DIR}/Assets"
            "${CMAKE_SOURCE_DIR}/../Assets"
            "${CMAKE_SOURCE_DIR}/../Persona-Quickshell-main/Assets"
            "${CMAKE_SOURCE_DIR}/../Persona-Quickshell/Assets")
        if(EXISTS "${candidate}/p3r imgs/bg.png")
            get_filename_component(PERSONA_ASSETS_DIR "${candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()
endif()
if(PERSONA_ASSETS_DIR)
    message(STATUS "Persona assets: ${PERSONA_ASSETS_DIR}")
else()
    message(WARNING "Could not find the original repo's Assets folder. Pass -DPERSONA_ASSETS_DIR=<path>, "
                    "or put an 'Assets' folder next to PersonaShell.exe (or set PERSONA_ASSETS) before running.")
endif()

# ---- program --------------------------------------------------------------------------------------
set(PERSONA_SOURCES
    src/main.cpp
    src/shell.cpp
    src/services.cpp
    src/wmi_brightness.cpp
    src/connectivity.cpp
    src/cava.cpp
    src/spectrum.cpp
    src/media.cpp
    src/apps.cpp
)

qt_add_executable(PersonaShell WIN32 ${PERSONA_SOURCES})

target_include_directories(PersonaShell PRIVATE src)
target_compile_definitions(PersonaShell PRIVATE NOMINMAX UNICODE _UNICODE)
if(PERSONA_ASSETS_DIR)
    target_compile_definitions(PersonaShell PRIVATE PERSONA_DEFAULT_ASSETS="${PERSONA_ASSETS_DIR}")
endif()
if(MSVC)
    target_compile_options(PersonaShell PRIVATE /permissive- /Zc:__cplusplus /utf-8 /EHsc /W3)
endif()

if(PERSONA_ENABLE_SMTC AND MSVC)
    target_compile_definitions(PersonaShell PRIVATE PERSONA_WINRT=1)
    target_link_libraries(PersonaShell PRIVATE windowsapp)
elseif(PERSONA_ENABLE_SMTC)
    message(WARNING "Media controls need MSVC (C++/WinRT); building without them.")
endif()

target_link_libraries(PersonaShell PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
    Qt6::Network Qt6::Concurrent Qt6::Multimedia Qt6::Svg
    ole32 oleaut32 uuid shell32 user32 gdi32 advapi32
    wlanapi wtsapi32 Bthprops Magnification wbemuuid)

# ---- QML (plain resources + hand-written qmldir for the singletons) ------------------------
file(GLOB PERSONA_QML CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qml/*.qml" "${CMAKE_SOURCE_DIR}/qml/qmldir")
qt_add_resources(PersonaShell "persona_qml"
    PREFIX "/Persona"
    BASE "${CMAKE_SOURCE_DIR}/qml"
    FILES ${PERSONA_QML})

# ---- wallpaper shaders: compiled from GLSL for Direct3D (HLSL), OpenGL, Vulkan ------------
set(PERSONA_SHADER_OPTIONS)
if(PERSONA_PRECOMPILE_SHADERS)
    list(APPEND PERSONA_SHADER_OPTIONS PRECOMPILE OPTIMIZED)
endif()
qt_add_shaders(PersonaShell "persona_shaders"
    ${PERSONA_SHADER_OPTIONS}
    PREFIX "/shaders"
    BASE "${CMAKE_SOURCE_DIR}/shaders"
    FILES
        shaders/motion/motion.vert
        shaders/motion/motion.frag
        shaders/parallax/parallax.vert
        shaders/parallax/parallax.frag
        shaders/ripple/ripple.vert
        shaders/ripple/ripple.frag
        shaders/stars/stars.vert
        shaders/stars/stars.frag)

# ---- "cmake --build <dir> --target deploy" copies the Qt DLLs / QML plugins next to the exe ----
get_target_property(_qmake_location Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_location}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --qmldir "${CMAKE_SOURCE_DIR}/qml" --no-translations
                "$<TARGET_FILE:PersonaShell>"
        DEPENDS PersonaShell
        COMMENT "Deploying Qt runtime next to PersonaShell.exe")
endif()

# ---- tests of the pure logic (spectrum analyzer, hotkey parser, colour matrices) -------------
if(PERSONA_BUILD_TESTS)
    enable_testing()
    add_executable(logic_test tests/logic_test.cpp src/spectrum.cpp)
    target_include_directories(logic_test PRIVATE src)
    add_test(NAME logic_test COMMAND logic_test)
endif()
