/**
 * text_editor_plugin.cpp — WASM 进程插件源码（C++ 编译为 WASM）
 *
 * 文本编辑器的全部功能逻辑在 WASM 内实现：
 *   - UI 布局构建（build_ui）
 *   - 文本修改处理（on_text_changed）
 *   - 新建文件（on_new_clicked）
 *   - 打开文件（on_open_clicked）
 *   - 保存文件（on_save_clicked）
 *   - 主题切换（toggle_theme）
 *   - 崩溃测试（crash_test — 验证进程隔离）
 *
 * 编译：emcc -nostdlib -noentry --import-undefined
 * WASM 模块声明 17 个 DTK 宿主导入函数（env 模块），
 * 无 DTK 宿主则无法实例化（LinkError）。
 */

#include <cstdint>

// 手写字符串长度和内存复制（避免 libc 导入）
__attribute__((noinline, optnone))
static uint32_t my_strlen(const char* s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

__attribute__((noinline, optnone))
static void my_memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

// ═══════════════════════════════════════════════════════════════
// DTK 宿主导入函数声明（17 个）
// ═══════════════════════════════════════════════════════════════

extern "C" {
    // 创建控件，返回控件 ID
    int32_t dtk_create_label(const char* text, int32_t len);
    int32_t dtk_create_textedit(int32_t readonly, int32_t unused);
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

    // 设置标签文本（3 参数：labelId, ptr, len）
    int32_t dtk_set_label_text(int32_t labelId, const char* text, int32_t len);

    // 启用/禁用控件
    int32_t dtk_set_widget_enabled(int32_t widgetId, int32_t enabled);

    // ── 文本编辑器专用导入 ──

    // 读取编辑器全部文本，写入 WASM 内存 ptr（最多 maxlen 字节），返回实际写入长度
    int32_t dtk_get_editor_text(uint32_t ptr, int32_t maxlen);
    // 设置编辑器全部文本
    int32_t dtk_set_editor_text(const char* text, int32_t len);
    // 获取编辑器文本长度
    int32_t dtk_get_editor_text_length();
    // 清空编辑器
    int32_t dtk_clear_editor();
    // 打开文件对话框，返回文件路径长度（路径写入 WASM 内存 ptr），0 表示取消
    int32_t dtk_open_file_dialog(uint32_t ptr, int32_t maxlen);
    // 保存文件对话框，返回文件路径长度（路径写入 WASM 内存 ptr），0 表示取消
    int32_t dtk_save_file_dialog(uint32_t ptr, int32_t maxlen);
    // 读取文件内容到 WASM 内存 ptr（最多 maxlen），返回实际读取长度，-1 表示错误
    int32_t dtk_read_file(const char* path, int32_t pathLen, uint32_t ptr, int32_t maxlen);
    // 将 WASM 内存 ptr 处 len 字节写入文件，返回 0 成功，-1 失败
    int32_t dtk_write_file(const char* path, int32_t pathLen, const char* data, int32_t dataLen);
}

// ═══════════════════════════════════════════════════════════════
// 线性内存管理 — bump allocator
// ═══════════════════════════════════════════════════════════════

static uint8_t* heap_base = nullptr;
static uint32_t heap_offset = 0;

// 静态字符串数据
static const char STR_WINDOW_TITLE[]  = "WASM Text Editor (DTK)";
static const char STR_TITLE_LABEL[]   = "WASM-Powered Text Editor";
static const char STR_STATUS_READY[]  = "Ready";
static const char STR_NEW[]           = "New";
static const char STR_OPEN[]          = "Open";
static const char STR_SAVE[]          = "Save";
static const char STR_CRASH[]         = "Crash Test";
static const char STR_STATUS_NEW[]    = "New file — unsaved";
static const char STR_STATUS_OPENED[] = "Opened: ";
static const char STR_STATUS_SAVED[]  = "Saved: ";
static const char STR_STATUS_OPEN_CANCEL[] = "Open cancelled";
static const char STR_STATUS_SAVE_CANCEL[] = "Save cancelled";
static const char STR_STATUS_OPEN_ERR[]    = "Error opening file";
static const char STR_STATUS_SAVE_ERR[]    = "Error saving file";
static const char STR_STATUS_EMPTY[]  = "Editor is empty — nothing to save";
static const char STR_STATUS_CRASHED[] = "WASM plugin crashed! But main app survived. Reloading plugin...";
static const char STR_CRASH_MSG[]     = "[CRASH TEST] About to trigger WASM unreachable instruction...";

// 控件 ID — WASM 管理全部 UI 状态
static int32_t g_mainLayout   = 0;
static int32_t g_titleLabel   = 0;
static int32_t g_statusLabel  = 0;
static int32_t g_editor       = 0;
static int32_t g_hbox         = 0;
static int32_t g_newButton    = 0;
static int32_t g_openButton   = 0;
static int32_t g_saveButton   = 0;
static int32_t g_crashButton  = 0;

// 文件状态
static bool g_modified = false;

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
    dtk_set_window_size(800, 600);

    g_mainLayout = dtk_create_hbox(0, 0);

    g_titleLabel = dtk_create_label(STR_TITLE_LABEL, (int32_t)my_strlen(STR_TITLE_LABEL));
    dtk_add_to_layout(g_mainLayout, g_titleLabel);

    g_statusLabel = dtk_create_label(STR_STATUS_READY, (int32_t)my_strlen(STR_STATUS_READY));
    dtk_add_to_layout(g_mainLayout, g_statusLabel);

    // 工具栏（水平布局）
    g_hbox = dtk_create_hbox(1, 0);
    dtk_add_to_layout(g_mainLayout, g_hbox);

    g_newButton = dtk_create_pushbutton(STR_NEW, (int32_t)my_strlen(STR_NEW));
    dtk_add_to_layout(g_hbox, g_newButton);

    g_openButton = dtk_create_pushbutton(STR_OPEN, (int32_t)my_strlen(STR_OPEN));
    dtk_add_to_layout(g_hbox, g_openButton);

    g_saveButton = dtk_create_pushbutton(STR_SAVE, (int32_t)my_strlen(STR_SAVE));
    dtk_add_to_layout(g_hbox, g_saveButton);

    g_crashButton = dtk_create_pushbutton(STR_CRASH, (int32_t)my_strlen(STR_CRASH));
    dtk_add_to_layout(g_hbox, g_crashButton);

    // 文本编辑区（可编辑）
    g_editor = dtk_create_textedit(0, 0);
    dtk_add_to_layout(g_mainLayout, g_editor);

    return g_mainLayout;
}

/**
 * on_text_changed() — 编辑器文本发生变化。
 * WASM 标记为已修改，更新状态标签。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_text_changed() {
    g_modified = true;
    // 状态由具体操作更新，这里仅标记
    return 0;
}

/**
 * on_new_clicked() — 新建文件。
 * WASM 负责全部逻辑：清空编辑器、更新状态。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_new_clicked() {
    dtk_clear_editor();
    g_modified = false;
    dtk_set_label_text(g_statusLabel, STR_STATUS_NEW, (int32_t)my_strlen(STR_STATUS_NEW));
    return 0;
}

/**
 * on_open_clicked() — 打开文件。
 * WASM 负责全部逻辑：文件对话框 → 读取文件 → 设置编辑器文本 → 更新状态。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_open_clicked() {
    // 1. 弹出文件对话框，获取文件路径
    uint32_t pathBuf = alloc(4096);
    int32_t pathLen = dtk_open_file_dialog(pathBuf, 4096);
    if (pathLen <= 0) {
        dtk_set_label_text(g_statusLabel, STR_STATUS_OPEN_CANCEL,
                           (int32_t)my_strlen(STR_STATUS_OPEN_CANCEL));
        return 0;
    }

    // 2. 读取文件内容到 WASM 内存
    uint32_t contentBuf = alloc(65536);
    int32_t contentLen = dtk_read_file((const char*)(uintptr_t)pathBuf, pathLen,
                                       contentBuf, 65536);
    if (contentLen < 0) {
        dtk_set_label_text(g_statusLabel, STR_STATUS_OPEN_ERR,
                           (int32_t)my_strlen(STR_STATUS_OPEN_ERR));
        return 0;
    }

    // 3. 设置编辑器文本
    dtk_set_editor_text((const char*)(uintptr_t)contentBuf, contentLen);

    // 4. 更新状态：显示 "Opened: <路径>"
    uint32_t prefixLen = my_strlen(STR_STATUS_OPENED);
    uint32_t statusLen = prefixLen + (uint32_t)pathLen;
    uint32_t statusPtr = alloc(statusLen);
    my_memcpy((void*)(uintptr_t)statusPtr, STR_STATUS_OPENED, prefixLen);
    my_memcpy((void*)(uintptr_t)(statusPtr + prefixLen),
              (void*)(uintptr_t)pathBuf, (uint32_t)pathLen);
    dtk_set_label_text(g_statusLabel, (const char*)(uintptr_t)statusPtr, (int32_t)statusLen);

    g_modified = false;
    return 0;
}

/**
 * on_save_clicked() — 保存文件。
 * WASM 负责全部逻辑：获取编辑器文本 → 文件对话框 → 写入文件 → 更新状态。
 */
extern "C" __attribute__((visibility("default")))
int32_t on_save_clicked() {
    // 1. 获取编辑器文本长度
    int32_t textLen = dtk_get_editor_text_length();
    if (textLen <= 0) {
        dtk_set_label_text(g_statusLabel, STR_STATUS_EMPTY,
                           (int32_t)my_strlen(STR_STATUS_EMPTY));
        return 0;
    }

    // 2. 获取编辑器文本到 WASM 内存
    uint32_t textBuf = alloc((uint32_t)textLen + 1);
    int32_t actualLen = dtk_get_editor_text(textBuf, textLen);
    if (actualLen < 0) actualLen = 0;

    // 3. 弹出保存文件对话框，获取文件路径
    uint32_t pathBuf = alloc(4096);
    int32_t pathLen = dtk_save_file_dialog(pathBuf, 4096);
    if (pathLen <= 0) {
        dtk_set_label_text(g_statusLabel, STR_STATUS_SAVE_CANCEL,
                           (int32_t)my_strlen(STR_STATUS_SAVE_CANCEL));
        return 0;
    }

    // 4. 写入文件
    int32_t result = dtk_write_file((const char*)(uintptr_t)pathBuf, pathLen,
                                    (const char*)(uintptr_t)textBuf, actualLen);
    if (result != 0) {
        dtk_set_label_text(g_statusLabel, STR_STATUS_SAVE_ERR,
                           (int32_t)my_strlen(STR_STATUS_SAVE_ERR));
        return 0;
    }

    // 5. 更新状态：显示 "Saved: <路径>"
    uint32_t prefixLen = my_strlen(STR_STATUS_SAVED);
    uint32_t statusLen = prefixLen + (uint32_t)pathLen;
    uint32_t statusPtr = alloc(statusLen);
    my_memcpy((void*)(uintptr_t)statusPtr, STR_STATUS_SAVED, prefixLen);
    my_memcpy((void*)(uintptr_t)(statusPtr + prefixLen),
              (void*)(uintptr_t)pathBuf, (uint32_t)pathLen);
    dtk_set_label_text(g_statusLabel, (const char*)(uintptr_t)statusPtr, (int32_t)statusLen);

    g_modified = false;
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

/**
 * crash_test() — 触发 WASM 崩溃（unreachable 指令）。
 * 验证进程隔离：WASM 子进程崩溃后主程序仍可运行。
 */
extern "C" __attribute__((visibility("default")))
int32_t crash_test() {
    // 先显示提示消息（通过 dtk_set_label_text）
    dtk_set_label_text(g_statusLabel, STR_CRASH_MSG,
                       (int32_t)my_strlen(STR_CRASH_MSG));
    // 触发 unreachable 指令 — WASM Runtime Error
    __builtin_unreachable();
    return 0;
}
