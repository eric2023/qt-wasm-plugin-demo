cmake_minimum_required(VERSION 3.16)
project(WasmChat VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_AUTOMOC ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

# DTK6 (Deepin Tool Kit)
find_package(Dtk6Widget REQUIRED)
find_package(Dtk6Gui REQUIRED)
find_package(Dtk6Core REQUIRED)

# --- DTK6 Client executable (main demo: WASM process plugin via Node.js IPC) ---
add_executable(dtkchatclient
    src/dtk_client_main.cpp
    src/DtkChatClient.h
    src/WasmProcessBridge.h
)
target_link_libraries(dtkchatclient PRIVATE
    Qt6::Core Qt6::Network Qt6::Widgets
    Dtk6::Widget Dtk6::Gui Dtk6::Core
)
target_include_directories(dtkchatclient PRIVATE src)


# --- Qt6 TCP Chat Server (backend WASM plugin via Node.js) ---
add_executable(wasmchatserver
    src/server_main.cpp
    src/ChatServer.h
    src/ChatServer.cpp
    src/WasmPluginLoader.h
    src/WasmPluginLoader.cpp
)
target_link_libraries(wasmchatserver PRIVATE
    Qt6::Core Qt6::Network
)
target_include_directories(wasmchatserver PRIVATE src)

add_executable(test_e2e
    tests/test_e2e.cpp
)
target_link_libraries(test_e2e PRIVATE Qt6::Core Qt6::Network)
