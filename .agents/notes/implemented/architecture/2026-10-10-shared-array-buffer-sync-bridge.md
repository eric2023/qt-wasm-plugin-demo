# Agent Note: Worker 线程 + SharedArrayBuffer 同步桥接 WASM import 调用

Status: implemented

## Problem

前端 WASM 插件的 DTK 宿主函数（如 `dtk_create_label`、`dtk_read_file`）需要同步返回值，
但这些函数的实际实现位于 C++ 主进程中。WASM 在 Node.js 子进程的 Worker 线程中同步执行，
当 WASM 调用 import 函数时，需要跨进程向 C++ 主进程请求返回值并阻塞等待。

## Decision

使用 **Worker 线程 + SharedArrayBuffer + Atomics.wait/notify** 实现跨线程同步：

- **主线程**：异步处理 stdin/stdout JSON 行协议（与 C++ 父进程通信），
  `pollImport()` 轮询 SharedArrayBuffer 检测 import 请求。
- **Worker 线程**：同步执行 WASM 导出函数。import 函数通过 `makeDtkImport()` 创建，
  执行时预读 WASM 内存中的字符串参数，写入 SharedArrayBuffer，
  `Atomics.notify` 唤醒主线程，然后 `Atomics.wait` 阻塞等待结果。

SharedArrayBuffer 布局（131108 字节）：

| 偏移 | 大小 | 用途 |
|------|------|------|
| 0 | 4 | TAG: 0=idle, 1=import 请求就绪 |
| 4 | 4 | 函数名长度 |
| 8 | 32 | 函数名（UTF-8） |
| 40 | 4 | 参数 1 (arg1) |
| 44 | 4 | 参数 2 (arg2) |
| 48 | 4 | 参数 3 (arg3) |
| 52 | 4 | 字符串长度 |
| 56 | 65536 | 字符串数据（UTF-8，64KB） |
| 65592 | 4 | 结果值 (result) |
| 65596 | 4 | 结果字符串长度 |
| 65600 | 65536 | 结果字符串数据（64KB） |

**预读字符串避免死锁**：带字符串参数的 import 函数在 Worker 端预读 WASM 线性内存中的字符串，
附带在 import 请求中通过 SharedArrayBuffer 传给 C++ 宿主。
C++ 直接使用预读的 `str` 参数，不需要回读 WASM 内存——
那会导致死锁（C++ 阻塞等 stdout 响应，Node 阻塞等 import_result）。

**结果字符串返回**：`dtk_get_editor_text`、`dtk_open_file_dialog`、`dtk_save_file_dialog`、
`dtk_read_file` 需要将字符串从 C++ 传回 WASM 内存。C++ 通过 `import_result` 的 `str` 字段返回，
主线程写入 SAB `RESULT_STR` 区域，Worker 复制到 WASM 内存目标位置。
`dtk_read_file` 的目标 `ptr` 在 `a3`（而非 `a1`），Worker 特殊处理。

## Alternatives considered

**使用 `fs.readSync` 在 Worker 中同步读取 stdin** — 不可行。`readline` 使 fd 0 非阻塞。

**使用 postMessage 异步回调** — import 函数需要同步返回值，异步无法满足 WASM 契约。

## Consequences

- WASM import 函数可以同步返回值——Worker 线程阻塞等待，主线程转发请求并写入结果。
- 预读字符串策略使 C++ 不需要回读 WASM 内存，避免了死锁。
- SAB 扩展至 131KB 支持 64KB 文件内容传输。
- `dtk_read_file` 目标 ptr 在 `a3`，Worker 特殊处理 result-str 复制目标。
- 崩溃处理：Worker `try/catch` 捕获 WASM `RuntimeError`，发送 `{type:'error',crash:true}`。
