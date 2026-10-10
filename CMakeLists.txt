cmake_minimum_required(VERSION 3.16)
project(WasmTextEditor VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_AUTOMOC ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# DTK6 (Deepin Tool Kit)
find_package(Dtk6Widget REQUIRED)
find_package(Dtk6Gui REQUIRED)
find_package(Dtk6Core REQUIRED)

# --- DTK6 Text Editor executable (WASM process plugin via Node.js IPC) ---
add_executable(dtktexteditor
    src/text_editor_main.cpp
    src/DtkTextEditor.h
    src/WasmProcessBridge.h
)
target_link_libraries(dtktexteditor PRIVATE
    Qt6::Core Qt6::Widgets
    Dtk6::Widget Dtk6::Gui Dtk6::Core
)
target_include_directories(dtktexteditor PRIVATE src)

# --- Editor plugin smoke test (WASM crash isolation test) ---
add_executable(test_editor
    tests/test_editor.cpp
)
target_link_libraries(test_editor PRIVATE Qt6::Core)
