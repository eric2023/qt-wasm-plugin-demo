# Agent Note: 手写字符串/内存函数避免 libc 依赖

Status: implemented

## Problem

使用 Emscripten 将 C++ 编译为 WASM 时，编译器会将 `while (s[len]) len++` 这样的
手写字符串长度循环优化为对 `strlen` 的调用。`-nostdlib` 编译时，`strlen` 成为
未定义符号，通过 `--import-undefined` 变成 WASM import。后端插件因此出现了一个
不需要的 `env.strlen` import，破坏了"自包含（零 import）"的设计目标。

## Decision

在 `my_strlen` 函数上添加 `__attribute__((noinline, optnone))`，
阻止编译器将循环模式识别并替换为 `strlen` 调用。

```cpp
__attribute__((noinline, optnone))
static uint32_t my_strlen(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}
```

`my_memcpy` 同样手写实现，但不需要 `optnone`——编译器不会将逐字节复制循环
优化为 `memcpy` 调用。

两个插件源码（`chat_ui_plugin.cpp`、`random_reply_plugin.cpp`）均使用
`-nostdlib` 编译，不链接任何标准库。

验证：
- `random_reply_plugin.wasm`：零 import（完全自包含）
- `chat_ui_plugin.wasm`：仅 11 个 DTK import，无 libc import

## Alternatives considered

**提供 strlen/memcpy 的 WASM import 实现** — 在 JS 端提供 `env.strlen` 实现。
可行但增加了不必要的 import 依赖，后端插件不再是"自包含"的，
`wasm_runner.js` 需要提供 import 对象。

**使用 `-O0` 编译** — 禁用所有优化。但 `-O0` 生成的 WASM 体积更大且效率更低。
`optnone` 仅针对 `my_strlen` 函数，其余代码仍享受 `-O2` 优化。

**使用 volatile 指针访问** — `while (*(volatile char*)(s + len)) len++`。
可行但 `volatile` 语义比 `optnone` 更重，影响生成代码质量。

## Consequences

- 后端插件完全自包含（零 import），`wasm_runner.js` 用空 import 对象 `{}` 实例化即可。
- 前端插件仅依赖 11 个 DTK import，无意外 libc import。
- `optnone` 仅作用于 `my_strlen`，不影响其余代码的 `-O2` 优化。
- `noinline` 防止函数被内联后优化器在调用点重新识别模式。
- 这是一个编译器特定的解决方案——如果更换编译器（如 Rust）需要重新验证。
