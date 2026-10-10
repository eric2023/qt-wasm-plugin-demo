# Agent Note: 插件源码使用 C++ 经 Emscripten 编译为 WASM

Status: implemented

## Problem

WASM 插件需要可维护的源码。手写 WAT（WebAssembly Text Format）汇编代码可读性差、
难以维护，且无法使用 C++ 的类型系统和抽象能力。

## Decision

插件源码改为**标准 C++ 文件**，用 **Emscripten (emcc)** 编译为 WASM。

- `plugins/text_editor_plugin.cpp` — 文本编辑器插件，C++ 源码
- `scripts/compile_plugins.sh` — Emscripten 编译脚本

编译参数：`emcc -O2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -nostdlib -Wl,--no-entry`，
使用 `-Wl,--import-undefined` 将未定义的 DTK 宿主函数编译为 WASM import。

C++ 源码中使用 `extern "C"` 声明 DTK 宿主函数，
`__attribute__((visibility("default")))` 标记导出函数，
`__builtin_wasm_memory_size`/`__builtin_wasm_memory_grow` 管理线性内存。

## Alternatives considered

**继续使用手写 WAT** — 可读性极差，添加新功能需手工计算内存偏移。

**使用 Rust 编译为 WASM** — 项目环境已安装 Emscripten，C++ 与 Qt/DTK 技术栈一致。

## Consequences

- 插件源码可读性和可维护性大幅提升。
- 需要编译步骤——`bash scripts/compile_plugins.sh` 在 C++ 构建前运行。
- 编译器可能优化手写字符串操作为 libc 调用——参见 [无 libc 依赖](../simplification/2026-10-10-no-libc-dependency.md)。
- DTK 宿主函数通过 `extern "C"` + `--import-undefined` 自然成为 WASM import。
