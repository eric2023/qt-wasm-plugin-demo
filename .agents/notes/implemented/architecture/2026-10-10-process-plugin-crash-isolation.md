# Agent Note: WASM 插件以进程插件形式运行实现崩溃隔离

Status: implemented

## Problem

WASM 插件需要在主应用（DTK 文本编辑器）中运行，但如果插件代码崩溃或产生异常，
不应影响主应用的稳定性。直接在主进程内嵌入 WASM 运行时意味着插件错误
可能导致主进程崩溃。需要验证 WASM 进程插件崩溃后主程序仍可正常运行。

## Decision

WASM 插件以**进程插件**形式运行——在独立的 Node.js 子进程中执行，
主应用通过 `QProcess` 管理子进程的 stdin/stdout 进行 JSON IPC 通信。

- **长驻子进程**：使用 `wasm_dtk_host.js`，主应用启动时创建，持续运行直到应用退出。
  `WasmProcessBridge`（`src/WasmProcessBridge.h`）管理生命周期。
- **崩溃恢复**：`crash_test()` 导出函数触发 WASM `unreachable` 指令，
  Worker 线程抛出 `RuntimeError`，子进程退出。C++ 宿主检测到 `isAlive() == false` 后，
  清理旧控件并重新加载 WASM 插件。`buildFallbackUi()` 在插件加载失败时用 C++ 直接创建 UI。

## Alternatives considered

**在主进程内嵌入 WASM 运行时（如 wasm3）** — 更简单，无需 IPC，
但插件错误直接影响主进程。进程隔离是刻意的设计选择。

**使用 WASI 运行时（如 wasmer）** — WASI 不支持同步 import 调用返回值，
与 Qt 事件循环集成复杂。Node.js + Worker 线程方案更灵活。

## Consequences

- 插件崩溃不影响主程序——子进程退出后主应用仍可运行，可重新加载插件。
- **自动崩溃恢复**——`crash_test()` 崩溃后，C++ 自动清理旧控件、重新加载 WASM 插件。
- IPC 引入延迟——每次 import 调用需要跨进程往返，但对 UI 操作可接受。
- 长驻进程的同步 import 需要 SharedArrayBuffer + Atomics 跨线程同步。
- 需要项目根目录运行——程序内部使用相对路径查找 `plugins/*.wasm` 和 `scripts/*.js`。
- 验证：冒烟测试 `test_editor` 通过——build_ui 正常、crash_test 崩溃后主程序存活。
