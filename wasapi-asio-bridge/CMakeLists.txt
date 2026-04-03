cmake_minimum_required(VERSION 3.20)
project(WasapiAsioBridge VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(MSVC)
  add_compile_options(/W3 /MP /utf-8 /EHsc)
  # Strip /GL and /LTCG - incompatible with Ninja generator
  string(REPLACE "/GL" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "/LTCG" "" CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}")
  add_compile_definitions(
    _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
  )
endif()

set(BRIDGE_CORE_SOURCES
    src/AudioEngine.cpp
    src/WasapiCapture.cpp
    src/AsioOutput.cpp
    src/RingBuffer.cpp
    src/DeviceEnumerator.cpp
    src/Resampler.cpp
    src/Logger.cpp
)

# CLI
add_executable(bridge_cli
    src/main_cli.cpp
    ${BRIDGE_CORE_SOURCES}
)
target_include_directories(bridge_cli PRIVATE include)
target_link_libraries(bridge_cli PRIVATE
    ole32 oleaut32 winmm avrt uuid ksuser
)
set_target_properties(bridge_cli PROPERTIES
    OUTPUT_NAME "bridge"
    WIN32_EXECUTABLE FALSE
)

# GUI
add_executable(bridge_gui
    src/main_gui.cpp
    src/MainWindow.cpp
    src/LevelMeter.cpp
    ${BRIDGE_CORE_SOURCES}
    resources/resources.rc
)
target_include_directories(bridge_gui PRIVATE include)
target_link_libraries(bridge_gui PRIVATE
    ole32 oleaut32 winmm avrt uuid ksuser
    msimg32  # <--- Add this line
    comctl32 gdi32 user32 dwmapi uxtheme
)
set_target_properties(bridge_gui PROPERTIES
    OUTPUT_NAME "bridge_gui"
    WIN32_EXECUTABLE TRUE
)

install(TARGETS bridge_cli bridge_gui RUNTIME DESTINATION bin)
install(FILES README.md DESTINATION .)
