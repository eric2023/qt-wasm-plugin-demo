# Agent Note: WASM 插件完整实现文本编辑器全部功能，客户端仅嵌入展示

Status: implemented

## Problem

需要验证 Qt 开发的 WASM 进程插件可以正常运行，且插件崩溃不会影响主程序。
文本编辑器作为一个完整的功能模块，其全部交互和数据逻辑（UI 构建、新建、打开、保存、编辑、主题切换、崩溃测试）
需要在 WASM 插件内闭环完成。C++ 客户端仅作为纯展示宿主——嵌入界面展示，不承担任何逻辑。

## Decision

将**文本编辑器的全部功能逻辑移入 WASM 插件**，C++ 客户端变为纯展示宿主。

### DTK 宿主导入函数（18 个）

| 类别 | 函数 | 用途 |
|------|------|------|
| 控件创建 | `dtk_create_label`, `dtk_create_textedit`, `dtk_create_pushbutton`, `dtk_create_hbox` | 创建 DTK 控件 |
| 布局 | `dtk_add_to_layout` | 将控件添加到布局 |
| 窗口 | `dtk_set_window_title`, `dtk_set_window_size` | 设置窗口属性 |
| 主题 | `dtk_get_theme`, `dtk_set_theme` | 获取/设置主题 |
| 控件操作 | `dtk_set_label_text`, `dtk_set_widget_enabled` | 更新控件状态 |
| 编辑器 | `dtk_get_editor_text`, `dtk_set_editor_text`, `dtk_get_editor_text_length`, `dtk_clear_editor` | 读写编辑器文本 |
| 文件 I/O | `dtk_open_file_dialog`, `dtk_save_file_dialog`, `dtk_read_file`, `dtk_write_file` | 文件对话框和读写 |

### WASM 导出函数（8 个）

| 函数 | WASM 逻辑 |
|------|-----------|
| `build_ui()` | 创建全部 UI 控件（标题/状态标签、工具栏、4 个按钮、编辑区） |
| `on_text_changed()` | 标记已修改 |
| `on_new_clicked()` | 清空编辑器 → 更新状态 |
| `on_open_clicked()` | 文件对话框 → 读取文件 → 设置编辑器文本 → 更新状态 |
| `on_save_clicked()` | 获取文本 → 文件对话框 → 写入文件 → 更新状态 |
| `toggle_theme()` | 获取当前主题 → 切换 |
| `crash_test()` | 触发 `unreachable` 指令崩溃 |
| `alloc(size)` | WASM 线性内存 bump 分配器 |

### C++ 客户端变为纯转发层

| 事件 | C++ 动作（仅转发） |
|------|-------------------|
| 用户点击 New | `callExport("on_new_clicked")` |
| 用户点击 Open | `callExport("on_open_clicked")` |
| 用户点击 Save | `callExport("on_save_clicked")` |
| 用户点击 Crash Test | `callExport("crash_test")` + 检测 `isAlive()` |
| 编辑器文本变化 | `callExport("on_text_changed")` |
| 主题切换 | `callExport("toggle_theme")` |

### 崩溃恢复机制

`crash_test()` 触发 WASM `unreachable` 指令 → Worker `RuntimeError` → 子进程退出。
C++ 宿主检测到 `isAlive() == false` 后：
1. 清理旧控件（删除所有 DLabel/DTextEdit/按钮）
2. 重置控件 ID 计数器
3. 重新调用 `loadWasmPlugin()` 加载 WASM 插件
4. 显示恢复消息

### SharedArrayBuffer 布局（131108 字节）

扩展 STR 和 RESULT_STR 区域至各 64KB，支持大文件内容传输：

```
[0..3]       TAG
[4..7]       FUNCLEN
[8..39]      FUNC name
[40..43]     ARG1
[44..47]     ARG2
[48..51]     ARG3
[52..55]     STRLEN
[56..65591]  STR data (64KB)
[65592..65595] RESULT
[65596..65599] RESULT_STRLEN
[65600..131095] RESULT_STR data (64KB)
```

## Alternatives considered

**保持逻辑在 C++ 客户端** — 不满足需求。需求明确要求 WASM 插件完整实现编辑器全部功能。

**将文件 I/O 也移入 WASM** — WASM 无法直接访问文件系统（无 fs import）。
通过 `dtk_read_file`/`dtk_write_file` 导入函数由宿主代理是合理的分层：
WASM 负责交互逻辑（何时读取/保存、读取/保存什么），宿主负责文件 I/O 原语。

## Consequences

- WASM 插件拥有全部编辑器功能——新建/打开/保存/编辑均在 WASM 内闭环完成。
- C++ 客户端变为纯展示宿主——仅处理 UI 原语和事件转发。
- 功能变更只需修改 WASM 插件源码（`text_editor_plugin.cpp`），无需修改 C++ 客户端。
- SAB 扩展至 131KB 支持 64KB 文件内容传输。
- `dtk_read_file` 的目标 `ptr` 在 `a3`（而非 `a1`），Worker 端特殊处理 result-str 复制目标。
- `dtk_set_editor_text` 设置文本时阻塞 `textChanged` 信号，避免回调 WASM 形成循环。
- 崩溃恢复机制确保 WASM 崩溃后主程序可自动恢复。
- 验证：冒烟测试通过——build_ui（19 次 import）、toggle_theme（2 次 import）、crash_test（崩溃后主程序存活）。
