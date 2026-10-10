# Agent Note: 插件源码使用 C++ 经 Emscripten 编译为 WASM

Status: implemented

## Problem

WASM 插件需要可维护的源码。手写 WAT（WebAssembly Text Format）汇编代码可读性差、
难以维护，且无法使用 C++ 的类型系统和抽象能力。项目最初使用手写 WAT，
但维护成本高且容易出错。

## Decision

插件源码改为**标准 C++ 文件**，用 **Emscripten (emcc)** 编译为 WASM。

- `plugins/chat_ui_plugin.cpp` — 前端 UI 插件，186 行 C++ 源码
- `plugins/random_reply_plugin.cpp` — 后端回复插件，78 行 C++ 源码
- `scripts/compile_plugins.sh` — Emscripten 编译脚本

编译参数：`emcc -O2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -nostdlib -Wl,--no-entry`，
前端额外使用 `-Wl,--import-undefined` 将未定义的 DTK 宿主函数编译为 WASM import。

C++ 源码中使用 `extern "C"` 声明 DTK 宿主函数（前端插件），
`__attribute__((visibility("default")))` 标记导出函数，
`__builtin_wasm_memory_size`/`__builtin_wasm_memory_grow` 管理线性内存。

## Alternatives considered

**继续使用手写 WAT** — 已有 WAT 文件，但可读性极差，添加新功能或修改字符串都需要
手工计算内存偏移。C++ 源码可读性高一个数量级，且编译器优化生成更紧凑的 WASM。

**使用 Rust 编译为 WASM** — Rust 有更好的 WASM 工具链支持（wasm-bindgen），
但项目环境已安装 Emscripten，C++ 与 Qt/DTK 技术栈一致，无需额外工具链。

## Consequences

- 插件源码可读性和可维护性大幅提升——C++ 类型系统、静态字符串常量、函数抽象。
- 需要编译步骤——`bash scripts/compile_plugins.sh` 在 C++ 构建前运行。
- 编译器可能优化手写字符串操作为 libc 调用——参见 [无 libc 依赖](../simplification/2026-10-10-no-libc-dependency.md)。
- 生成的 WASM 模块更紧凑——前端 1.3KB，后端 775B（WAT 编译版本更大）。
- DTK 宿主函数通过 `extern "C"` + `--import-undefined` 自然成为 WASM import——
  参见 [DTK 依赖强制机制](../architecture/2026-10-10-dtk-dependency-enforcement.md)。
