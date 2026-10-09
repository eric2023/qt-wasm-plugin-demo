/**
 * random_reply_plugin.cpp — 后端 WASM 插件源码（C++ 编译为 WASM）
 *
 * 用 Emscripten (emcc) 编译为 random_reply_plugin.wasm。
 * 自包含（无 DTK 依赖），导出 generate_reply(seed) 函数。
 *
 * 10 条预设回复，基于 seed % 10 选择。
 */

#include <cstdint>
#include <cstring>

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
    uint32_t len = (uint32_t)strlen(reply);

    // 分配内存并复制回复
    if (!heap_base) {
        heap_base = (uint8_t*)(__builtin_wasm_memory_size(0) * 65536);
        heap_offset = 0;
        __builtin_wasm_memory_grow(0, 1);
    }

    uint32_t ptr = (uint32_t)(uintptr_t)(heap_base + heap_offset);
    memcpy((void*)(uintptr_t)ptr, reply, len);
    heap_offset += len + 1;  // +1 for null terminator

    return ptr;
}

/**
 * get_reply_len() — 返回最近一次 generate_reply 的回复长度。
 * 简化实现：每次调用重算。
 */
extern "C" __attribute__((visibility("default")))
uint32_t get_reply_len(uint32_t seed) {
    int index = (int)(seed % REPLY_COUNT);
    return (uint32_t)strlen(REPLIES[index]);
}
