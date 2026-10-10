# Agent Note: WASM 插件拥有全部交互与数据逻辑，客户端仅嵌入展示

Status: implemented

## Problem

之前的架构中，聊天交互逻辑分散在 C++ 客户端和 WASM 插件两处：C++ 客户端负责读取输入框文本、
格式化消息、发送 TCP 数据、接收服务器消息、更新状态标签、启用/禁用控件；WASM 插件仅负责
创建 UI 控件和格式化消息。这导致 WASM 插件无法独立验证界面交互闭环——交互逻辑不在 WASM 内，
无法证明 WASM 插件能完成独立模块的界面交互和数据交互。

## Decision

将**全部交互与数据逻辑移入 WASM 插件**，C++ 客户端变为纯展示宿主——仅嵌入界面展示，不承担任何逻辑。

### 新增 5 个 DTK 宿主导入函数（共 16 个）

| 函数 | 签名 | 用途 |
|------|------|------|
| `dtk_get_input_text` | `(ptr, maxlen) → len` | 读取输入框文本，宿主通过 result-string 返回 |
| `dtk_clear_input` | `() → 0` | 清空输入框 |
| `dtk_set_label_text` | `(labelId, ptr, len) → 0` | 设置标签文本 |
| `dtk_set_widget_enabled` | `(widgetId, enabled) → 0` | 启用/禁用控件 |
| `dtk_send_to_server` | `(ptr, len) → 0` | 发送数据到服务器（TCP） |

### 新增 4 个 WASM 导出函数（共 7 个）

| 函数 | 触发时机 | WASM 逻辑 |
|------|---------|-----------|
| `on_send_clicked()` | 用户点击发送/按回车 | 读取输入 → 格式化 `"[Me] "` 前缀 → 显示 → 发送到服务器 → 清空输入 |
| `on_server_message(ptr, len)` | 收到服务器消息 | 格式化 `"[Server] "` 前缀 → 显示 |
| `on_connected()` | TCP 连接成功 | 更新状态标签 → 启用输入框和按钮 → 显示连接消息 |
| `on_disconnected()` | TCP 连接断开 | 更新状态标签 → 禁用按钮 → 显示断开消息 |

### C++ 客户端变为纯转发层

| 事件 | 之前（C++ 处理逻辑） | 之后（转发给 WASM） |
|------|---------------------|-------------------|
| 用户点击发送 | 读取输入、格式化、显示、发送、清空 | `callExport("on_send_clicked")` |
| 收到服务器消息 | 解析 JSON、格式化、显示 | 分配 WASM 内存 → 写入消息 → `callExport_ii("on_server_message", ptr, len)` |
| TCP 连接成功 | 更新状态、启用控件、显示消息 | `callExport("on_connected")` |
| TCP 连接断开 | 更新状态、禁用控件、显示消息、重连 | `callExport("on_disconnected")` + 重连 |

### SharedArrayBuffer 扩展

为支持 `dtk_get_input_text`（宿主返回字符串到 WASM 内存）和 `dtk_set_label_text`（3 个参数），
SAB 布局扩展为 8244 字节：

- 新增 `ARG3_OFFSET`（偏移 48）：第 3 个 i32 参数
- 新增 `RESULT_STRLEN_OFFSET` + `RESULT_STR_OFFSET`：C++ → JS 方向的结果字符串

## Alternatives considered

**保持逻辑在 C++ 客户端，仅增加 WASM 导出用于验证** — 不满足需求。
需求明确要求"WASM 插件中完成独立模块界面交互、数据交互等闭环"，
逻辑不在 WASM 内则无法验证 WASM 模块的独立交互能力。

**将 TCP 通信也移入 WASM** — WASM 无法直接访问网络（无 socket import）。
通过 `dtk_send_to_server` 导入函数由宿主代理发送是合理的分层：
WASM 负责交互逻辑（何时发送、发送什么），宿主负责传输（如何发送）。

**使用 Emscripten 的 `EM_JS` 直接调用 JS** — 与 `-nostdlib` 纯模块目标冲突。
import 机制是更干净的方案，保持 WASM 模块独立性。

## Consequences

- WASM 插件拥有全部交互逻辑——发送消息、接收消息、状态更新均在 WASM 内闭环完成。
- C++ 客户端变为纯展示宿主——仅处理 UI 原语（创建控件、设置文本）和事件转发。
- 交互逻辑变更只需修改 WASM 插件源码（`chat_ui_plugin.cpp`），无需修改 C++ 客户端。
- 新增 5 个 DTK 导入需要同步更新三处（见 AGENTS.md 修改规则）。
- SAB 布局扩展支持 3 参数导入和结果字符串返回——`dtk_get_input_text` 利用 result-string 机制
  将输入框文本从 C++ 传回 WASM 内存。
- `dtk_set_label_text` 有 3 个参数（labelId, ptr, len），STR_FUNCS 预读逻辑特殊处理：
  字符串在 a2/a3 而非 a1/a2。
- 验证：E2E 测试通过——5 条消息发送成功，回复正确接收，连接/断开状态正确更新。
