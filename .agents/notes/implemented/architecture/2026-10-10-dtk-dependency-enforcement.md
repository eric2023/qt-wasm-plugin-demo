# Agent Note: WASM import 机制强制 DTK 依赖

Status: implemented

## Problem

需要确保前端 WASM 文本编辑器插件无法在没有 DTK 宿主的环境中运行——
插件必须依赖 DTK 提供的控件创建函数。如果插件可以独立运行，
就无法保证 UI 控件是 DTK 控件（DLabel、DTextEdit 等）。

## Decision

前端 WASM 插件通过 `extern "C"` 声明 18 个 DTK 宿主函数。
Emscripten 编译时，这些未定义符号通过 `-Wl,--import-undefined`
自动成为 WASM import（位于 `env` 模块下）。

```cpp
extern "C" {
    int32_t dtk_create_label(const char* text, int32_t len);
    int32_t dtk_create_textedit(int32_t readonly, int32_t unused);
    // ... 共 18 个
}
```

没有 DTK 宿主提供这些 import，WASM 模块**无法实例化**——
`WebAssembly.instantiate` 会抛出 `LinkError`。

C++ 宿主（`DtkTextEditor`）通过 `handleDtkHostCall()` 回调提供全部 18 个函数的实现，
创建对应的 DTK 控件（`DLabel`、`DTextEdit`、`DSuggestButton`）。

验证：`text_editor_plugin.wasm` 有 18 个 import，全部是 `env.dtk_*` 函数。

## Alternatives considered

**运行时检查 DTK 可用性** — 插件仍可在没有 DTK 的环境中实例化，不满足"强制依赖"的设计目标。

**使用 Emscripten 的 `EM_JS` 宏直接调用 JS** — 与 `-nostdlib` 纯模块目标冲突。

**静态链接 DTK 头文件定义** — 不可行，DTK 是 C++ Qt 库，无法编译为 WASM。

## Consequences

- WASM 模块与 DTK 宿主强耦合——无 DTK 宿主则无法实例化，这是刻意的设计。
- 18 个 DTK import 是 WASM 模块的唯一外部依赖（无 libc import）。
- DTK 宿主必须通过 `WasmProcessBridge::s_hostCallback` 提供全部 18 个函数实现。
- 新增 DTK 宿主函数需要同步更新：C++ `extern "C"` 声明 →
  `wasm_dtk_host.js` import 映射 → `DtkTextEditor::handleDtkHostCall()` 实现。
