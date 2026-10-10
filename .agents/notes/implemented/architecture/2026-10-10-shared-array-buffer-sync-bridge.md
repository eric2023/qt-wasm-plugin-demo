# Agent Note: Worker 线程 + SharedArrayBuffer 同步桥接 WASM import 调用

Status: implemented

## Problem

前端 WASM 插件的 DTK 宿主函数（如 `dtk_create_label`）需要同步返回值（控件 ID），
但这些函数的实际实现位于 C++ 主进程中。WASM 在 Node.js 子进程的 Worker 线程中同步执行，
当 WASM 调用 import 函数时，需要跨进程向 C++ 主进程请求返回值并阻塞等待。

Node.js 主线程是异步的——`process.stdin` 的 readline 接口使 fd 0 变为非阻塞模式，
`fs.readSync` 无法在 Worker 线程中使用。Worker 线程需要一种机制同步等待主线程
转发 import 请求到 C++ 并接收返回值。

## Decision

使用 **Worker 线程 + SharedArrayBuffer + Atomics.wait/notify** 实现跨线程同步：

- **主线程**：异步处理 stdin/stdout JSON 行协议（与 C++ 父进程通信），
  `pollImport()` 轮询 SharedArrayBuffer 检测 import 请求。
- **Worker 线程**：同步执行 WASM 导出函数。import 函数通过 `makeDtkImport()` 创建，
  执行时预读 WASM 内存中的字符串参数，写入 SharedArrayBuffer，
  `Atomics.notify` 唤醒主线程，然后 `Atomics.wait` 阻塞等待结果。

SharedArrayBuffer 布局（4120 字节）：

| 偏移 | 大小 | 用途 |
|------|------|------|
| 0 | 4 | TAG: 0=idle, 1=import 请求就绪 |
| 4 | 4 | 函数名长度 |
| 8 | 32 | 函数名（UTF-8） |
| 40 | 4 | 参数 1 (arg1) |
| 44 | 4 | 参数 2 (arg2) |
| 48 | 4 | 字符串长度 |
| 52 | 4096 | 字符串数据（UTF-8） |
| 4116 | 4 | 结果值 (result) |

**预读字符串避免死锁**：带字符串参数的 import 函数（`dtk_create_label`、
`dtk_create_lineedit`、`dtk_create_pushbutton`、`dtk_set_window_title`、
`dtk_append_message`）在 Worker 端预读 WASM 线性内存中的字符串，
附带在 import 请求中通过 SharedArrayBuffer 传给 C++ 宿主。
C++ 直接使用预读的 `str` 参数，不需要回读 WASM 内存——
那会导致死锁（C++ 阻塞等 stdout 响应，Node 阻塞等 import_result）。

## Alternatives considered

**使用 `fs.readSync` 在 Worker 中同步读取 stdin** — 不可行。
Node.js 主线程的 `readline` 接口使 fd 0 变为非阻塞模式，
`fs.readSync` 在非阻塞 fd 上返回 EAGAIN 错误。

**使用 postMessage 异步回调** — Worker 的 import 函数需要同步返回值，
异步 postMessage 无法满足 WASM import 的同步调用契约。

**使用 `Atomics.wait` + `MessageChannel`** — SharedArrayBuffer 更简单，
所有数据（函数名、参数、字符串、结果）在单个缓冲区中传递，
无需序列化/反序列化开销。

## Consequences

- WASM import 函数可以同步返回值——Worker 线程阻塞等待，主线程转发请求并写入结果。
- 预读字符串策略使 C++ 不需要回读 WASM 内存，避免了死锁。
- SharedArrayBuffer 要求 Node.js 运行在支持 `--harmony-sharedarraybuffer` 的模式下
  （Node.js 18+ 默认支持）。
- 轮询 `pollImport()` 使用 `setImmediate` 避免阻塞主线程事件循环。
- 每次 import 调用有跨线程 + 跨进程往返延迟，但 UI 构建等低频操作可接受。
