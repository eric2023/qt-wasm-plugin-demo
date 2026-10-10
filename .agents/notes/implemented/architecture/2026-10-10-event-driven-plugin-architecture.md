# Agent Note: 事件驱动架构取代导出函数映射

Status: implemented

## Problem

旧方案中 C++ 宿主（`DtkTextEditor.h`）硬编码了具体导出函数名：
`callExport("on_new_clicked")`、`callExport("on_open_clicked")`、`callExport("on_save_clicked")`、
`callExport("crash_test")`、`callExport("toggle_theme")`、`callExport("on_text_changed")`。

这意味着宿主源码包含了插件业务知识——知道每个按钮对应哪个导出函数。
三方换一个插件（计算器、图片浏览器），宿主代码就得改——不满足"客户端只负责加载插件"的需求。

## Decision

改为**事件驱动架构**：WASM 插件通过通用事件接口与宿主通信，宿主源码不包含任何插件业务知识。

### WASM 导出函数从 8 个减为 3 个

| 导出 | 用途 | 宿主调用方式 |
|------|------|-------------|
| `build_ui()` | 创建 UI + 订阅事件 + 注册菜单项 | `callExport("build_ui")` — 仅在初始化时 |
| `on_event(eventId, widgetId)` | 通用事件入口 | `callExport_ii("on_event", eventId, widgetId)` — 唯一的事件分发 |
| `alloc(size)` | 内存分配 | `callExport_i32("alloc", size)` — 供宿主写 WASM 内存 |

### 新增 2 个 DTK 导入函数

```cpp
// WASM 订阅控件事件：当 widgetId 的 eventType 事件发生时，宿主调用 on_event(eventId, widgetId)
// eventType: 1=clicked, 2=textChanged
// eventId 由 WASM 插件定义
int32_t dtk_subscribe_event(int32_t widget_id, int32_t event_type, int32_t event_id);

// WASM 注册右键菜单项：用户选择后宿主调用 on_event(eventId, 0)
int32_t dtk_add_context_menu_item(const char* label, int32_t len, int32_t event_id);
```

### 通信流程

1. **WASM 插件订阅事件**：在 `build_ui()` 中创建控件后调用 `dtk_subscribe_event(widget, type, eventId)`
2. **宿主记录映射 + 连接 Qt 信号**：宿主存储 `(widget, type) → eventId` 映射，`connect` 对应 Qt 信号
3. **事件触发**：Qt 信号触发时，宿主调用 `callExport_ii("on_event", eventId, widgetId)` —— 通用入口
4. **WASM 分发**：`on_event` 内部根据 `eventId` 分发到自己的处理逻辑（switch/case）
5. **右键菜单**：WASM 调用 `dtk_add_context_menu_item` 注册菜单项，宿主在右键菜单中显示，用户选择后调 `on_event(eventId, 0)`

### 对比

| | 旧方案 | 新方案 |
|---|---|---|
| 宿主调用 | `callExport("on_new_clicked")` | `callExport_ii("on_event", 1, widgetId)` |
| 宿主知识 | 知道每个按钮对应哪个导出函数 | 只知道事件 ID，不知道含义 |
| 导出函数 | 8 个 | 3 个（固定不变） |
| 三方换插件 | 需要修改宿主源码 | 宿主源码零修改 |

## Alternatives considered

**保持导出函数映射，宿主通过配置文件管理映射** — 增加配置层复杂度，且仍然需要宿主知道有哪些导出函数。事件驱动方案更干净：宿主只调 `on_event`，完全不知道插件有哪些功能。

**WASM 插件通过字符串名注册事件回调** — 需要额外的字符串比较逻辑和映射表。整数 eventId 更高效，且 `-nostdlib` 环境下字符串操作受限。

**宿主自动发现 WASM 导出函数** — WASM 导出列表是模块元数据，但宿主仍需知道如何连接 Qt 信号到哪个导出函数。事件驱动方案让 WASM 主动订阅，宿主被动转发，更符合"宿主无业务知识"原则。

## Consequences

- 宿主源码完全无业务知识——`DtkTextEditor.h` 中没有 `onNewClicked`/`onOpenClicked`/`onSaveClicked` 等 slot。
- WASM 导出固定为 3 个函数（`build_ui`/`on_event`/`alloc`），三方插件也只需导出这三个。
- DTK 导入从 18 个增加到 20 个（新增 `dtk_subscribe_event` 和 `dtk_add_context_menu_item`）。
- 事件 ID 由 WASM 插件定义，宿主仅转发——换插件时事件 ID 可以完全不同，宿主不受影响。
- 崩溃检测集成在 `on_event` 调用后——`callExport_ii` 返回后检查 `isAlive()`，不需要单独的 `crash_test` 导出。
- 右键菜单完全由 WASM 插件控制——宿主只提供内置的 Copy/Paste（宿主原语），其余菜单项由 WASM 动态注册。
- `dtk_subscribe_event` 中使用 `dynamic_cast`（而非 `qobject_cast`）因为 DTK 的 DTextEdit 没有暴露 Q_OBJECT 宏给 qobject_cast。
- 验证：冒烟测试通过——build_ui 产生 25 次 import（含 5 次 subscribe + 1 次 add_context_menu_item），on_event(6,0) 切换主题，on_event(4,0) 触发崩溃。
