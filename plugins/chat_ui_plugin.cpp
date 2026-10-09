/**
 * chat_ui_plugin.cpp — WASM 进程插件源码（C++ 编译为 WASM）
 *
 * 用 Emscripten (emcc) 编译为 chat_ui_plugin.wasm。
 * 通过 Emscripten 的 import 机制声明对 DTK 宿主的依赖：
 *   - 这些函数在编译时声明为 import，运行时由宿主通过 JS 桥接提供。
 *   - 没有 DTK 宿主提供这些 import，WASM 模块无法实例化。
 *
 * 导出函数（供宿主通过 IPC 调用）：
 *   - build_ui()        : 调用 DTK 宿主创建全部 UI 控件
 *   - format_message()  : 格式化消息（添加 "[Me] " 前缀）并显示
 *   - toggle_theme()    : 切换深色/浅色主题
 *   - alloc(size)       : 线性内存 bump 分配器
 *
 * 宿主导入函数（DTK host imports，11 个）：
 *   - dtk_create_label, dtk_create_textedit, dtk_create_lineedit,
 *     dtk_create_pushbutton, dtk_create_hbox, dtk_add_to_layout,
 *     dtk_set_window_title, dtk_set_window_size,
 *     dtk_get_theme, dtk_set_theme, dtk_append_message
 */

#include <cstdint>
#include <cstring>

// ═══════════════════════════════════════════════════════════════
// DTK 宿主导入函数声明
// 这些函数由 DTK 宿主提供。WASM 模块通过 import 机制依赖它们。
// 没有这些 import，模块无法实例化 — 这就是 DTK 依赖的强制机制。
// ═══════════════════════════════════════════════════════════════

extern "C" {
    // 创建控件，返回控件 ID
    int32_t dtk_create_label(const char* text, int32_t len);
    int32_t dtk_create_textedit(int32_t readonly, int32_t unused);
    int32_t dtk_create_lineedit(const char* placeholder, int32_t len);
    int32_t dtk_create_pushbutton(const char* text, int32_t len);
    int32_t dtk_create_hbox(int32_t flag, int32_t unused);

    // 将控件添加到布局
    int32_t dtk_add_to_layout(int32_t layoutId, int32_t widgetId);

    // 窗口属性
    int32_t dtk_set_window_title(const char* title, int32_t len);
    int32_t dtk_set_window_size(int32_t width, int32_t height);

    // 主题
    int32_t dtk_get_theme();
    int32_t dtk_set_theme(int32_t theme, int32_t unused);

    // 追加消息到显示区
    int32_t dtk_append_message(const char* msg, int32_t len);
}

// ═══════════════════════════════════════════════════════════════
// 线性内存管理 — bump allocator
// ═══════════════════════════════════════════════════════════════

static uint8_t* heap_base = nullptr;
static uint32_t heap_offset = 0;

// 静态字符串数据（编译时放入 linear memory 的 data segment）
static const char STR_WINDOW_TITLE[]   = "WASM-Powered Chat (DTK)";
static const char STR_TITLE_LABEL[]    = "WASM-Powered Chat Client (DTK)";
static const char STR_STATUS_LABEL[]   = "Initializing...";
static const char STR_PLACEHOLDER[]    = "Type your message here...";
static const char STR_SEND[]           = "Send";
static const char STR_PREFIX[]         = "[Me] ";

// ═══════════════════════════════════════════════════════════════
// 导出函数
// ═══════════════════════════════════════════════════════════════

/**
 * alloc(size) — 在 WASM 线性内存中分配 size 字节。
 * 宿主通过 IPC 调用此函数来分配内存，然后写入消息文本。
 */
extern "C" __attribute__((visibility("default")))
uint32_t alloc(uint32_t size) {
    if (!heap_base) {
        // 首次调用：初始化堆基址到静态数据之后
        heap_base = (uint8_t*)__builtin_wasm_memory_size(0) * 65536;
        heap_offset = 0;
        // 扩展内存以容纳堆
        if (__builtin_wasm_memory_grow(0, (size + 65535) / 65536) == -1) {
            return 0;
        }
    }
    uint32_t ptr = (uint32_t)(uintptr_t)(heap_base + heap_offset);
    heap_offset += size;
    // 对齐到 4 字节
    heap_offset = (heap_offset + 3) & ~3u;
    return ptr;
}

/**
 * build_ui() — 通过调用 DTK 宿主函数创建整个 UI。
 * 返回主布局 ID。
 */
extern "C" __attribute__((visibility("default")))
int32_t build_ui() {
    // 设置窗口标题
    dtk_set_window_title(STR_WINDOW_TITLE, (int32_t)strlen(STR_WINDOW_TITLE));

    // 设置窗口尺寸
    dtk_set_window_size(600, 480);

    // 创建主垂直布局
    int32_t mainLayout = dtk_create_hbox(0, 0);

    // 创建标题标签
    int32_t titleLabel = dtk_create_label(STR_TITLE_LABEL, (int32_t)strlen(STR_TITLE_LABEL));
    dtk_add_to_layout(mainLayout, titleLabel);

    // 创建状态标签
    int32_t statusLabel = dtk_create_label(STR_STATUS_LABEL, (int32_t)strlen(STR_STATUS_LABEL));
    dtk_add_to_layout(mainLayout, statusLabel);

    // 创建消息显示区（只读）
    int32_t textEdit = dtk_create_textedit(1, 0);
    dtk_add_to_layout(mainLayout, textEdit);

    // 创建输入行（水平布局）
    int32_t hbox = dtk_create_hbox(1, 0);
    dtk_add_to_layout(mainLayout, hbox);

    // 创建输入框
    int32_t inputField = dtk_create_lineedit(STR_PLACEHOLDER, (int32_t)strlen(STR_PLACEHOLDER));
    dtk_add_to_layout(hbox, inputField);

    // 创建发送按钮
    int32_t sendButton = dtk_create_pushbutton(STR_SEND, (int32_t)strlen(STR_SEND));
    dtk_add_to_layout(hbox, sendButton);

    return mainLayout;
}

/**
 * format_message(ptr, len) — 将消息格式化（添加 "[Me] " 前缀）并显示。
 * ptr/len 指向 WASM 线性内存中的消息文本。
 */
extern "C" __attribute__((visibility("default")))
int32_t format_message(uint32_t ptr, uint32_t len) {
    // 分配 prefix + message 的空间
    uint32_t prefixLen = (uint32_t)strlen(STR_PREFIX);
    uint32_t totalLen = prefixLen + len;
    uint32_t resultPtr = alloc(totalLen);

    // 复制前缀
    memcpy((void*)(uintptr_t)resultPtr, STR_PREFIX, prefixLen);
    // 复制消息
    memcpy((void*)(uintptr_t)(resultPtr + prefixLen), (void*)(uintptr_t)ptr, len);

    // 调用 DTK 宿主显示
    dtk_append_message((const char*)(uintptr_t)resultPtr, (int32_t)totalLen);

    return 0;
}

/**
 * toggle_theme() — 获取当前主题并切换。
 * 返回旧主题值（0=浅色, 1=深色）。
 */
extern "C" __attribute__((visibility("default")))
int32_t toggle_theme() {
    int32_t current = dtk_get_theme();
    if (current == 0) {
        dtk_set_theme(1, 0);  // 切换到深色
    } else {
        dtk_set_theme(0, 0);  // 切换到浅色
    }
    return current;
}
