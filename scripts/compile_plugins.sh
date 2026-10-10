#!/usr/bin/env bash
set -e

# compile_plugins.sh — 用 Emscripten 编译 C++ 插件源码为 WASM

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PLUGINS_DIR="$PROJECT_DIR/plugins"

EMSRC="/home/ut000520@uos/Desktop/emsdk/emsdk_env.sh"
if [ -f "$EMSRC" ]; then
    source "$EMSRC" 2>/dev/null
fi

if ! command -v emcc &> /dev/null; then
    echo "ERROR: emcc not found. Install Emscripten first."
    exit 1
fi

echo "=== Compiling chat_ui_plugin.cpp ==="

emcc "$PLUGINS_DIR/chat_ui_plugin.cpp" \
    -o "$PLUGINS_DIR/chat_ui_plugin.wasm" \
    -O2 \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_build_ui","_on_send_clicked","_on_server_message","_on_connected","_on_disconnected","_toggle_theme","_alloc"]' \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -nostdlib \
    -Wl,--no-entry \
    -Wl,--import-undefined

echo "  → $PLUGINS_DIR/chat_ui_plugin.wasm"

echo ""
echo "=== Compiling random_reply_plugin.cpp ==="

emcc "$PLUGINS_DIR/random_reply_plugin.cpp" \
    -o "$PLUGINS_DIR/random_reply_plugin.wasm" \
    -O2 \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_generate_reply","_get_reply_len"]' \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -nostdlib \
    -Wl,--no-entry

echo "  → $PLUGINS_DIR/random_reply_plugin.wasm"

echo ""
echo "=== Done! ==="
ls -la "$PLUGINS_DIR"/*.wasm 2>/dev/null
