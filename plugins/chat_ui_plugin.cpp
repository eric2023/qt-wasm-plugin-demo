/**
 * chat_ui_plugin.cpp — WASM 进程插件源码（C++ 编译为 WASM）
 *
 * 架构：WASM 插件负责全部界面交互与数据逻辑，客户端仅嵌入展示。
 *   - build_ui()           : 创建全部 UI 控件，保存控件 ID
 *   - on_send_clicked()    : 用户点击发送 → 读取输入 → 格式化 → 显示 → 发送到服务器 → 清空输入
 *   - on_server_message()  : 收到服务器消息 → 格式化 → 显示
 *   - on_connected()       : 连接成功 → 更新状态标签 → 启用输入
 *   - on_disconnected()    : 连接断开 → 更新状态标签 → 禁用输入
 *   - toggle_theme()       : 切换主题
 *   - alloc(size)          : 线性内存 bump 分配器
 *
 * 宿主导入函数（DTK host imports，15 个）：
 *   - dtk_create_label, dtk_create_textedit, dtk_create_lineedit,
 *     dtk_create_pushbutton, dtk_create_hbox, dtk_add_to_layout,
 *     dtk_set_window_title, dtk_set_window_size,
 *     dtk_get_theme, dtk_set_theme, dtk_append_message,
 *     dtk_get_input_text(ptr, maxlen), dtk_clear_input(),
 *     dtk_set_label_text(labelId, ptr, len),
 *     dtk_set_widget_enabled(widgetId, enabled)
 *     dtk_send_to_server(ptr, len)
 */

#include <cstdint>

// 手写字符串长度和内存复制（避免 libc 导入）
// __attribute__((optnone)) 防止编译器将循环优化为 strlen 调用
__attribute__((noinline, optnone))
static uint32_t my_strlen(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}
static void my_memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

// ═══════════════════════════════════════════════════════════════
// DTK 宿主导入函数声明（15 个）
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

    // ── 新增：数据交互导入 ──

    // 读取输入框文本，写入 WASM 内存 ptr（最多 maxlen 字节），返回实际写入长度
    int32_t dtk_get_input_text(uint32_t ptr, int32_t maxlen);
    // 清空输入框
    int32_t dtk_clear_input();
    // 设置标签文本
    int32_t dtk_set_label_text(int32_t labelId, const char* text, int32_t len);
    // 启用/禁用控件
    int32_t dtk_set_widget_enabled(int32_t widgetId, int32_t enabled);
    // 发送数据到服务器（TCP）
    int32_t dtk_send_to_server(const char* data, int32_t len);
}

// ═══════════════════════════════════════════════════════════════
// 线性内存管理 — bump allocator
// ═══════════════════════════════════════════════════════════════

static uint8_t* heap_base = nullptr;
static uint32_t heap_offset = 0;

// 静态字符串数据
static const char STR_WINDOW_TITLE[]   = "WASM-Powered Chat (DTK)";
static const char STR_TITLE_LABEL[]    = "WASM-Powered Chat Client (DTK)";
static const char STR_STATUS_INIT[]    = "Initializing...";
static const char STR_PLACEHOLDER[]    = "Type your message here...";
static const char STR_SEND[]           = "Send";
static const char STR_PREFIX_ME[]      = "[Me] ";
static const char STR_PREFIX_SERVER[]  = "[Server] ";
static const char STR_CONNECTED[]      = "Connected to server (127.0.0.1:12345)";
static const char STR_DISCONNECTED[]   = "Disconnected from server";
static const char STR_SYS_CONNECTED[]  = "[System] Connected to chat server!";
static const char STR_SYS_DISCONNECTED[] = "[System] Disconnected. Reconnecting...";

// 控件 ID — WASM 管理全部 UI 状态
static int32_t g_mainLayout  = 0;
static int32_t g_titleLabel  = 0;
static int32_t g_statusLabel = 0;
static int32_t g_textEdit    = 0;
static int32_t g_hbox        = 0;
static int32_t g_inputField  = 0;
static int32_t g_sendButton  = 0;

// ═══════════════════════════════════════════════════════════════
// 导出函数
// ═══════════════════════════════════════════════════════════════

/**
 * alloc(size) — 在 WASM 线性内存中分配 size 字节。
 */
extern "C" __attribute__((visibility("default")))
uint32_t alloc(uint32_t size) {
    if (!heap_base) {
        uint32_t pages = (uint32_t)__builtin_wasm_memory_size(0);
        heap_base = (uint8_t*)((uintptr_t)pages * 65536);
        heap_offset = 0;
        if (__builtin_wasm_memory_grow(0, (size + 65535) / 65536) == -1) {
            return 0;
        }
    }
    uint32_t ptr = (uint32_t)(uintptr_t)(heap_base + heap_offset);
    heap_offset += size;
    heap_offset = (heap_offset + 3) & ~3u;
    return ptr;
}

/**
 * build_ui() — 创建全部 UI 控件，保存控件 ID。
 * 返回主布局 ID。
 */
extern "C" __attribute__((visibility("default")))
int32_t build_ui() {
    dtk_set_window_title(STR_WINDOW_TITLE, (int32_t)my_strlen(STR_WINDOW_TITLE));
    dtk_set_window_size(600, 480);

    g_mainLayout = dtk_create_hbox(0, 0);

    g_titleLabel = dtk_create_label(STR_TITLE_LABEL, (int32_t)my_strlen(STR_TITLE_LABEL));
    dtk_add_to_layout(g_mainLayout, g_titleLabel);

    g_statusLabel = dtk_create_label(STR_STATUS_INIT, (int32_t)my_strlen(STR_STATUS_INIT));
    dtk_add_to_layout(g_mainLayout, g_statusLabel);

    g_textEdit = dtk_create_textedit(1, 0);
    dtk_add_to_layout(g_mainLayout, g_textEdit);

    g_hbox = dtk_create_hbox(1, 0);
    dtk_add_to_layout(g_mainLayout, g_hbox);

    g_inputField = dtk_create_lineedit(STR_PLACEHOLDER, (int32_t)my_strlen(STR_PLACEHOLDER));
    dtk_add_to_layout(g_hbox, g_inputField);

    g_sendButton = dtk_create_pushbutton(STR_SEND, (int32_t)my_strlen(STR_SEND));
    dtk_add_to_layout(g_hbox, g_sendButton);

    // 初始禁用输入（等待连接成功）
    dtk_set_widget_enabled(g_inputField, 0);
    dtk_set_widget_enabled(g_sendButton, 0);

    return g_mainLayout;
}

/**
 * on_send_clicked() — 用户点击发送按钮或按回车。
 * WASM 负责全部逻辑：读取输入 → 格式化 → 显示 → 发送到服务器 → 清空输入。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_send_clicked() {
    // 1. 从宿主读取输入框文本（宿主将文本写入 WASM 内存）
    uint32_t bufSize = 4096;
    uint32_t bufPtr = alloc(bufSize);
    int32_t inputLen = dtk_get_input_text(bufPtr, (int32_t)bufSize);
    if (inputLen <= 0) return 0;

    // 2. 格式化："[Me] " + 消息
    uint32_t prefixLen = my_strlen(STR_PREFIX_ME);
    uint32_t totalLen = prefixLen + (uint32_t)inputLen;
    uint32_t msgPtr = alloc(totalLen);
    my_memcpy((void*)(uintptr_t)msgPtr, STR_PREFIX_ME, prefixLen);
    my_memcpy((void*)(uintptr_t)(msgPtr + prefixLen),
              (void*)(uintptr_t)bufPtr, (uint32_t)inputLen);

    // 3. 显示在消息区
    dtk_append_message((const char*)(uintptr_t)msgPtr, (int32_t)totalLen);

    // 4. 发送原始消息到服务器
    dtk_send_to_server((const char*)(uintptr_t)bufPtr, inputLen);

    // 5. 清空输入框
    dtk_clear_input();

    return 0;
}

/**
 * on_server_message(ptr, len) — 收到服务器消息。
 * WASM 负责格式化并显示。
 * ptr/len 指向 WASM 线性内存中的消息文本。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_server_message(uint32_t ptr, uint32_t len) {
    uint32_t prefixLen = my_strlen(STR_PREFIX_SERVER);
    uint32_t totalLen = prefixLen + len;
    uint32_t msgPtr = alloc(totalLen);
    my_memcpy((void*)(uintptr_t)msgPtr, STR_PREFIX_SERVER, prefixLen);
    my_memcpy((void*)(uintptr_t)(msgPtr + prefixLen),
              (void*)(uintptr_t)ptr, len);

    dtk_append_message((const char*)(uintptr_t)msgPtr, (int32_t)totalLen);
    return 0;
}

/**
 * on_connected() — TCP 连接成功。
 * WASM 更新状态标签、启用输入。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_connected() {
    dtk_set_label_text(g_statusLabel, STR_CONNECTED, (int32_t)my_strlen(STR_CONNECTED));
    dtk_set_widget_enabled(g_inputField, 1);
    dtk_set_widget_enabled(g_sendButton, 1);
    dtk_append_message(STR_SYS_CONNECTED, (int32_t)my_strlen(STR_SYS_CONNECTED));
    return 0;
}

/**
 * on_disconnected() — TCP 连接断开。
 * WASM 更新状态标签、禁用输入。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_disconnected() {
    dtk_set_label_text(g_statusLabel, STR_DISCONNECTED, (int32_t)my_strlen(STR_DISCONNECTED));
    dtk_set_widget_enabled(g_sendButton, 0);
    dtk_append_message(STR_SYS_DISCONNECTED, (int32_t)my_strlen(STR_SYS_DISCONNECTED));
    return 0;
}

/**
 * toggle_theme() — 获取当前主题并切换。
 */
extern "C" __attribute__((visibility("default")))
int32_t toggle_theme() {
    int32_t current = dtk_get_theme();
    if (current == 0) {
        dtk_set_theme(1, 0);
    } else {
        dtk_set_theme(0, 0);
    }
    return current;
}
