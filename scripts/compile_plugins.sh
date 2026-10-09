#!/usr/bin/env bash
set -e

# compile_plugins.sh — 用 Emscripten 编译 C++ 插件源码为 WASM
#
# 前端插件 chat_ui_plugin.cpp:
#   声明 11 个 DTK 宿主导入函数（extern "C" + IMPORT）
#   导出 build_ui, format_message, toggle_theme, alloc
#
# 后端插件 random_reply_plugin.cpp:
#   自包含，导出 generate_reply, get_reply_len

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PLUGINS_DIR="$PROJECT_DIR/plugins"

# 激活 Emscripten 环境
EMSRC="/home/ut000520@uos/Desktop/emsdk/emsdk_env.sh"
if [ -f "$EMSRC" ]; then
    source "$EMSRC"
fi

if ! command -v emcc &> /dev/null; then
    echo "ERROR: emcc not found. Install Emscripten first:"
    echo "  cd /home/ut000520@uos/Desktop/emsdk && ./emsdk install latest && ./emsdk activate latest"
    exit 1
fi

echo "=== Compiling chat_ui_plugin.cpp → chat_ui_plugin.wasm ==="

# 前端 UI 插件
# -s EXPORTED_FUNCTIONS: 导出 build_ui, format_message, toggle_theme, alloc, malloc, free
# -s IMPORTED_FUNCTIONS: 声明 11 个 DTK 宿主导入函数
# -s ALLOW_MEMORY_GROWTH: 允许内存增长
# -s EXPORTED_RUNTIME_METHODS: 导出运行时辅助方法
emcc "$PLUGINS_DIR/chat_ui_plugin.cpp" \
    -o "$PLUGINS_DIR/chat_ui_plugin.js" \
    -O2 \
    -s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=0 \
    -s EXPORT_NAME="createChatUiPlugin" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_build_ui","_format_message","_toggle_theme","_alloc"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","getValue","setValue","UTF8ToString","stringToUTF8","lengthBytesUTF8","HEAPU8","HEAP32"]' \
    -s IMPORTED_FUNCTIONS='["dtk_create_label","dtk_create_textedit","dtk_create_lineedit","dtk_create_pushbutton","dtk_create_hbox","dtk_add_to_layout","dtk_set_window_title","dtk_set_window_size","dtk_get_theme","dtk_set_theme","dtk_append_message"]' \
    -s WARN_ON_UNDEFINED_SYMBOLS=0 \
    -nostdlib \
    -noentry

echo "  → $PLUGINS_DIR/chat_ui_plugin.js + chat_ui_plugin.wasm"

echo ""
echo "=== Compiling random_reply_plugin.cpp → random_reply_plugin.wasm ==="

# 后端回复插件（自包含，无 import）
emcc "$PLUGINS_DIR/random_reply_plugin.cpp" \
    -o "$PLUGINS_DIR/random_reply_plugin.js" \
    -O2 \
    -s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=0 \
    -s EXPORT_NAME="createRandomReplyPlugin" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_generate_reply","_get_reply_len"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","getValue","setValue","UTF8ToString","stringToUTF8","lengthBytesUTF8","HEAPU8","HEAP32"]' \
    -s WARN_ON_UNDEFINED_SYMBOLS=0 \
    -nostdlib \
    -noentry

echo "  → $PLUGINS_DIR/random_reply_plugin.js + random_reply_plugin.wasm"

echo ""
echo "=== Done! ==="
