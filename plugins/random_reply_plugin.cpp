/**
 * random_reply_plugin.cpp — 后端 WASM 插件源码（C++ 编译为 WASM）
 *
 * 用 Emscripten (emcc) 编译为 random_reply_plugin.wasm。
 * 自包含（无外部依赖），导出 generate_reply(seed) 和 get_reply_len(seed)。
 *
 * 10 条预设回复，基于 seed % 10 选择。
 * 使用手写字符串长度计算和内存复制，避免 libc 依赖。
 */

#include <cstdint>

// 预设回复
static const char* REPLIES[] = {
    "Hello! Nice to meet you.",
    "That's interesting!",
    "WASM plugins are awesome!",
    "Tell me more about it.",
    "I'm a WASM-powered server.",
    "Qt and WASM work great together!",
    "Let me think about that...",
    "Great question!",
    "Processing your request...",
    "Goodbye! See you next time."
};
static const int REPLY_COUNT = 10;

// 手写字符串长度（避免 strlen 导入）
// __attribute__((noinline)) 防止编译器将其优化为 strlen 调用
__attribute__((noinline, optnone))
static uint32_t my_strlen(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

// 手写内存复制（避免 memcpy 导入）
static void my_memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

// 堆分配器
static uint8_t* heap_base = nullptr;
static uint32_t heap_offset = 0;

/**
 * generate_reply(seed) — 根据 seed 选择回复并返回指针。
 */
extern "C" __attribute__((visibility("default")))
uint32_t generate_reply(uint32_t seed) {
    int index = (int)(seed % REPLY_COUNT);
    const char* reply = REPLIES[index];
    uint32_t len = my_strlen(reply);

    if (!heap_base) {
        uint32_t pages = (uint32_t)__builtin_wasm_memory_size(0);
        heap_base = (uint8_t*)((uintptr_t)pages * 65536);
        heap_offset = 0;
        __builtin_wasm_memory_grow(0, 1);
    }

    uint32_t ptr = (uint32_t)(uintptr_t)(heap_base + heap_offset);
    my_memcpy((void*)(uintptr_t)ptr, reply, len);
    heap_offset += len + 1;

    return ptr;
}

/**
 * get_reply_len(seed) — 返回指定 seed 对应回复的长度。
 */
extern "C" __attribute__((visibility("default")))
uint32_t get_reply_len(uint32_t seed) {
    int index = (int)(seed % REPLY_COUNT);
    return my_strlen(REPLIES[index]);
}
