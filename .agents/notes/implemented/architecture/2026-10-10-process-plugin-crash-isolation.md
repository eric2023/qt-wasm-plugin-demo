# Agent Note: WASM 插件以进程插件形式运行实现崩溃隔离

Status: implemented

## Problem

WASM 插件需要在主应用（DTK 客户端）中运行，但如果插件代码崩溃或产生异常，
不应影响主应用的稳定性。直接在主进程内嵌入 WASM 运行时（如 wasm3）意味着插件错误
可能导致主进程崩溃。

## Decision

WASM 插件以**进程插件**形式运行——在独立的 Node.js 子进程中执行，
主应用通过 `QProcess` 管理子进程的 stdin/stdout 进行 JSON IPC 通信。

- **前端插件**：使用长驻子进程（`wasm_dtk_host.js`），主应用启动时创建，
  持续运行直到应用退出。`WasmProcessBridge`（`src/WasmProcessBridge.h`）管理生命周期。
- **后端插件**：使用一次性子进程（`wasm_runner.js`），每次调用启动新进程，
  适合服务器低频调用场景。`WasmPluginLoader`（`src/WasmPluginLoader.h`）管理。

插件崩溃时，`QProcess` 检测到子进程退出，主应用仍可运行。`isAlive()` 方法检查子进程状态，
`buildFallbackUi()` 在前端插件加载失败时用 C++ 直接创建相同 UI。

## Alternatives considered

**在主进程内嵌入 WASM 运行时（如 wasm3）** — 更简单，无需 IPC，
但插件错误（如内存越界、无限循环）直接影响主进程。进程隔离是刻意的设计选择，
以牺牲 IPC 延迟换取崩溃隔离保证。

**使用 WASI 运行时（如 wasmer）** — 更标准化，但 WASI 不支持同步 import 调用返回值，
需要异步回调模式，与 Qt 事件循环集成复杂。Node.js + Worker 线程方案更灵活。

## Consequences

- 插件崩溃不影响主程序——子进程退出后主应用仍可运行，可重启子进程。
- IPC 引入延迟——每次 import 调用需要跨进程往返（stdin/stdout JSON），
  但对 UI 构建等低频操作可接受。
- 前端长驻进程的同步 import 需要 SharedArrayBuffer + Atomics 跨线程同步——
  参见 [SharedArrayBuffer 同步桥接](../architecture/2026-10-10-shared-array-buffer-sync-bridge.md)。
- 后端一次性进程每次调用有进程启动开销（~100ms），适合低频场景。
- 需要项目根目录运行——程序内部使用相对路径查找 `plugins/*.wasm` 和 `scripts/*.js`。
