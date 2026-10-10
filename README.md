# WASM 驱动的文本编辑器（DTK）

基于 Qt/DTK6 的文本编辑器。**插件源码是 C++ 代码**，用 Emscripten 编译为 WASM。
WASM 插件以**进程插件**形式运行——在独立的 Node.js 子进程中执行，
通过 JSON IPC 与 DTK 宿主通信。**插件崩溃不影响主程序。**

采用**事件驱动架构**——WASM 插件通过通用事件接口与宿主通信，宿主源码不包含任何插件业务知识。
三方可以开发自己的 WASM 插件（计算器、图片浏览器等），客户端源码零修改即可加载。

## 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                    DTK 客户端进程（纯展示宿主）                 │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  WASM 文本编辑器插件 (text_editor_plugin.wasm)         │    │
│  │  C++ 源码 → Emscripten 编译 → WASM                    │    │
│  │  ─ 全部编辑器功能逻辑在 WASM 内闭环 ─                  │    │
│  │  导入 20 个 DTK 宿主函数 (env 模块)                    │    │
│  │  导出 3 个函数 (build_ui/on_event/alloc)              │    │
│  │  ─ 事件驱动：subscribe + on_event ─                   │    │
│  │  没有 DTK 宿主，插件无法实例化！                        │    │
│  └────────────────────┬────────────────────────────────┘    │
│                       │ JSON IPC (stdin/stdout 行分隔)        │
│  ┌────────────────────┴────────────────────────────────┐    │
│  │  WasmProcessBridge (QProcess)                        │    │
│  │    → 启动 node wasm_dtk_host.js 子进程                 │    │
│  │    ← 接收 import 请求 → 调用 DTK 宿主回调              │    │
│  │    → 返回 import_result (+ result_str)               │    │
│  │  DtkTextEditor (纯展示宿主):                           │    │
│  │    事件触发 → callExport_ii("on_event", eventId, wId)│    │
│  │    不承担任何业务逻辑——不知道有哪些按钮/功能            │    │
│  │  DTK 宿主提供: DLabel, DTextEdit,                     │    │
│  │  DSuggestButton, DMenu, DFileDialog                   │    │
│  │  崩溃检测: callExport 返回后检查 isAlive()             │    │
│  │           → 崩溃则清理旧控件 → 重新加载 WASM 插件       │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### 事件驱动架构

**核心思想**：宿主源码不硬编码任何插件业务函数名。三方换插件时，宿主源码零修改。

**通信机制**：

1. **WASM 插件订阅事件**：在 `build_ui()` 中创建控件后调用 `dtk_subscribe_event(widgetId, eventType, eventId)`
   - 宿主记录映射 `(widgetId, eventType) → eventId`，连接 Qt 信号
   - 事件类型：`1=clicked`、`2=textChanged`
   - `eventId` 由 WASM 插件定义

2. **宿主转发事件**：Qt 信号触发时，宿主调用 `callExport_ii("on_event", eventId, widgetId)` ——唯一的事件入口
   - 宿主不关心 `eventId` 对应什么功能

3. **WASM 插件注册右键菜单项**：调用 `dtk_add_context_menu_item(label, len, eventId)`
   - 宿主在右键菜单中显示这些项，用户选择后调用 `on_event(eventId, 0)`

4. **WASM 插件处理事件**：`on_event(eventId, widgetId)` 内部根据 `eventId` 分发到自己的处理逻辑

**对比旧方案**：

| | 旧方案（导出函数映射） | 新方案（事件驱动） |
|---|---|---|
| 宿主调用 | `callExport("on_new_clicked")` | `callExport_ii("on_event", eventId, widgetId)` |
| 宿主知识 | 知道每个按钮对应哪个导出函数 | 只知道事件 ID，不知道含义 |
| 导出函数 | 8 个（每个功能一个） | 3 个（`build_ui`/`on_event`/`alloc`） |
| 三方换插件 | 需要修改宿主源码 | 宿主源码零修改 |

### 进程隔离

WASM 插件在**独立 Node.js 子进程**中运行，与主程序隔离：

- **插件崩溃不影响主程序**——子进程崩溃后主程序仍可运行，可重新加载插件
- 主程序通过 `QProcess` 管理 WASM 子进程的 stdin/stdout
- IPC 协议为行分隔 JSON（每行一个 JSON 对象，以 `\n` 分隔）
- **崩溃恢复**：`on_event(crashEventId, 0)` 触发 WASM `unreachable` 指令，
  Worker 线程抛出 `RuntimeError`，子进程退出。C++ 宿主检测到 `isAlive() == false`
  后清理旧控件、重新加载 WASM 插件

### 进程插件同步桥接（前端）

WASM 插件需要同步调用 DTK 宿主函数并获取返回值。
Node.js 主线程是异步的，WASM 在 Worker 线程中同步执行。
通过 **SharedArrayBuffer + Atomics.wait/notify** 跨线程同步：

```
DTK 客户端进程                     Node.js 子进程 (wasm_dtk_host.js)
┌──────────────────┐             ┌──────────────────────────────────┐
│ DtkTextEditor    │  stdin →    │ 主线程                            │
│  (纯展示宿主)      │ ←stdout    │  readline 异步处理 stdin/stdout   │
│                  │             │  pollImport() 轮询 SharedArrayBuffer│
│  handleDtkHostCall│ ←import    │  ↕ postMessage                   │
│  → new DLabel(str)│ result→    │ Worker 线程                       │
│  → 返回 widget_id │            │  WebAssembly.instantiate         │
│  → 返回 file_path │            │  text_editor_plugin.wasm         │
│  → 返回 file_data │            │  ─ 全部逻辑在 WASM 内 ─            │
│  → subscribe_event│            │  import 函数 (20 个):              │
│  → connect signal │            │    预读 WASM 内存字符串            │
│  (无业务逻辑)      │            │    → 写入 SharedArrayBuffer       │
│                  │             │    → Atomics.notify(主线程)        │
│                  │             │    → Atomics.wait(阻塞等结果)      │
│                  │             │    ← 主线程写入 result + result_str│
│                  │             │    → Atomics.notify(唤醒)          │
│                  │             │  崩溃处理:                         │
│                  │             │    try/catch → {type:'error',crash:true}│
└──────────────────┘             └──────────────────────────────────┘
```

**预读字符串避免死锁**：带字符串参数的 import 函数在 Worker 端预读 WASM 线性内存中的字符串，
附带在 import 请求中通过 SharedArrayBuffer 传给 C++ 宿主。
这样 C++ 不需要回读 WASM 内存（那会导致死锁）。

**结果字符串返回**：`dtk_get_editor_text`、`dtk_open_file_dialog`、`dtk_save_file_dialog`、
`dtk_read_file` 需要将字符串从 C++ 传回 WASM 内存。
C++ 通过 `import_result` 的 `str` 字段返回文本，主线程写入 SAB 的 `RESULT_STR` 区域，
Worker 收到后将其复制到 WASM 内存中目标 `ptr` 位置。
`dtk_read_file` 的目标 `ptr` 在 `a3`（而非 `a1`），Worker 特殊处理。

## 仓库结构

```
plugins/                WASM 插件 C++ 源码 → Emscripten 编译 → .wasm
  text_editor_plugin.cpp  文本编辑器插件（20 个 DTK import，3 个导出）
scripts/                WASM 运行时与编译脚本
  compile_plugins.sh      Emscripten 编译脚本
  wasm_dtk_host.js        WASM 进程插件运行时（Worker + SharedArrayBuffer）
src/                    C++ 源代码
  text_editor_main.cpp    DTK 文本编辑器入口
  DtkTextEditor.h         DTK 宿主核心（纯展示 + 事件转发 + 崩溃恢复）
  WasmProcessBridge.h     QProcess WASM 桥接（IPC 通信）
tests/
  test_editor.cpp         WASM 插件冒烟测试（事件驱动 + 崩溃隔离验证）
CMakeLists.txt          CMake 构建配置（2 个目标）
.agents/notes/          架构决策记录（Agent Notes）
```

## 构建与运行

```bash
# 1. 激活 Emscripten
source /home/ut000520@uos/Desktop/emsdk/emsdk_env.sh

# 2. 编译 WASM 插件
bash scripts/compile_plugins.sh

# 3. 编译 C++ 目标
mkdir -p build && cd build && cmake .. && make -j$(nproc) && cd ..

# 4. 运行冒烟测试（验证事件驱动 + WASM 崩溃隔离）
./build/test_editor

# 5. 启动文本编辑器
DISPLAY=:0 QT_QPA_PLATFORM=xcb ./build/dtktexteditor
```

> **重要**：所有程序必须从**项目根目录**运行——程序内部使用相对路径查找 `plugins/*.wasm` 和 `scripts/*.js`。

## 关键设计决策

### 1. 事件驱动架构（核心）

WASM 插件通过通用事件接口与宿主通信：
- 导出 `build_ui`/`on_event`/`alloc` 三个函数——固定不变，三方插件也只导出这三个
- 宿主通过 `dtk_subscribe_event` 记录事件映射，通过 `on_event` 通用入口转发事件
- 宿主源码不包含任何插件业务知识（不知道有哪些按钮、有哪些功能）

### 2. 插件源码是 C++ 代码

插件源码是标准 C++，用 Emscripten 编译为 WASM。不使用手写 WAT/汇编。

### 3. WASM 插件依赖 DTK

前端 WASM 插件通过 `extern "C"` 声明 20 个 DTK 宿主函数。
Emscripten 编译时，这些未定义符号通过 `-Wl,--import-undefined`
自动成为 WASM import（位于 `env` 模块下）。

没有 DTK 宿主提供这些 import，WASM 模块**无法实例化**——这就是 DTK 依赖的强制机制。

验证：`text_editor_plugin.wasm` 有 20 个 import，全部是 `env.dtk_*` 函数。

### 4. 进程插件（进程隔离）

WASM 插件在**独立 Node.js 子进程**中运行：
- 插件崩溃不影响主程序
- 主程序通过 `QProcess` 管理 WASM 子进程
- **崩溃恢复**：主程序检测到子进程退出后，清理旧控件并重新加载 WASM 插件

### 5. 无 libc 依赖

插件使用手写 `my_strlen`/`my_memcpy`，用 `__attribute__((noinline, optnone))`
防止编译器优化为 libc 调用。编译时用 `-nostdlib`，生成的 WASM 模块无 libc import。

### 6. DTK 控件

UI 控件均为 DTK 控件——DLabel、DTextEdit、DSuggestButton、DMenu、DFileDialog。
深色/浅色主题通过 WASM 插件在 `on_event` 中调用 `dtk_get_theme`/`dtk_set_theme` 驱动。

### 7. 同步 import 桥接

Worker 线程 + SharedArrayBuffer + Atomics.wait/notify 实现跨线程同步。

## DTK 宿主函数

WASM 文本编辑器插件从 `env` 模块导入以下函数（C++ `extern "C"` 声明，Emscripten 编译为 import）：

### 事件驱动核心导入

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_subscribe_event` | `(widgetId, eventType, eventId) → eventId` | 订阅控件事件，宿主连接 Qt 信号，事件触发时调用 `on_event(eventId, widgetId)` |
| `dtk_add_context_menu_item` | `(labelPtr, labelLen, eventId) → eventId` | 注册右键菜单项，用户选择后调用 `on_event(eventId, 0)` |

### 控件创建

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_create_label` | `(ptr, len) → id` | 创建 DLabel → 控件 ID |
| `dtk_create_textedit` | `(readonly, _) → id` | 创建 DTextEdit → 控件 ID |
| `dtk_create_pushbutton` | `(ptr, len) → id` | 创建 DSuggestButton → 控件 ID |
| `dtk_create_hbox` | `(flag, _) → id` | 创建布局（0=主垂直布局, 1=水平布局）→ 布局 ID |
| `dtk_add_to_layout` | `(layoutId, widgetId) → 0` | 将控件/子布局添加到布局 |

### 窗口与主题

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_set_window_title` | `(ptr, len) → 0` | 设置窗口标题 |
| `dtk_set_window_size` | `(width, height) → 0` | 设置窗口尺寸 |
| `dtk_get_theme` | `() → theme` | 获取当前主题（0=浅色, 1=深色） |
| `dtk_set_theme` | `(theme, _) → 0` | 设置主题 |

### 控件操作

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_set_label_text` | `(labelId, ptr, len) → 0` | 设置标签文本 |
| `dtk_set_widget_enabled` | `(widgetId, enabled) → 0` | 启用/禁用控件 |

### 文本编辑器

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_get_editor_text` | `(ptr, maxlen) → len` | 读取编辑器文本，宿主通过 result-str 返回 |
| `dtk_set_editor_text` | `(ptr, len) → 0` | 设置编辑器文本 |
| `dtk_get_editor_text_length` | `() → len` | 获取编辑器文本长度 |
| `dtk_clear_editor` | `() → 0` | 清空编辑器 |

### 文件 I/O

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_open_file_dialog` | `(ptr, maxlen) → len` | 打开文件对话框，返回文件路径 |
| `dtk_save_file_dialog` | `(ptr, maxlen) → len` | 保存文件对话框，返回文件路径 |
| `dtk_read_file` | `(pathPtr, pathLen, destPtr, maxlen) → len` | 读取文件内容，通过 result-str 返回 |
| `dtk_write_file` | `(pathPtr, pathLen, dataPtr, dataLen) → 0/-1` | 写入文件 |

> `ptr`/`len` 参数指向 WASM 线性内存中的 UTF-8 字符串。
> JS 端在发送 import 请求前预读这些字符串，通过 IPC 的 `str` 字段传递给 C++，
> C++ 直接使用 `str` 而不需要回读 WASM 内存。

## WASM 导出函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `build_ui()` | `() → i32` | 创建全部 UI 控件，订阅事件，注册右键菜单项。返回主布局 ID |
| `on_event(eventId, widgetId)` | `(i32, i32) → i32` | 通用事件入口——WASM 内部根据 eventId 分发到处理逻辑 |
| `alloc(size)` | `(i32) → i32` | WASM 线性内存 bump 分配器 |

> 宿主只调用这三个导出函数，不包含任何插件业务函数名。
> 三方插件也只需导出这三个函数，宿主源码零修改即可加载。

## 运行流程

### 启动流程

```
1. 客户端启动
   text_editor_main.cpp → DtkTextEditor 构造函数
     → WasmProcessBridge::load("plugins/text_editor_plugin.wasm")
         → QProcess 启动 node wasm_dtk_host.js
         → Node 主线程创建 SharedArrayBuffer + Worker 线程
         → Worker: WebAssembly.instantiate(wasm, {env: dtkFuncs})
         → Worker: postMessage({type:'loaded'})
         → 主线程: stderr 输出就绪信号
         → C++ 等待 stderr 就绪信号
     → callExport("build_ui")
         → 25 次 import 往返：
           - 创建控件 + 布局（19 次）
           - dtk_subscribe_event × 5（订阅按钮 clicked + 编辑器 textChanged）
           - dtk_add_context_menu_item × 1（注册 Toggle Theme 菜单项）
         → C++ 在自己进程内 new DLabel/DTextEdit/DSuggestButton... 并加入布局
         → C++ 连接 Qt 信号 → 事件触发时 callExport_ii("on_event", eventId, widgetId)
         → WASM 保存全部控件 ID 和事件 ID
         → 返回 call_result
     → applyThemeColors()
```

### 事件触发流程

```
用户点击 "New" 按钮
  │
  └─ DPushButton::clicked (Qt 信号)
       │  宿主在 dtk_subscribe_event 时连接的 lambda
       │
       └─ callExport_ii("on_event", 1, newButtonId)  ← 通用入口，宿主不知道含义
            │
            └─ WASM on_event(1, widgetId):
                 └─ switch(1) → handle_new():
                      ├─ dtk_clear_editor()
                      └─ dtk_set_label_text(statusLabel, "New file — unsaved")
```

### 打开文件流程

```
用户点击 "Open" 按钮
  │
  └─ callExport_ii("on_event", 2, openButtonId)  ← 通用入口
       │
       └─ WASM on_event(2, widgetId):
            └─ switch(2) → handle_open():
                 ├─ dtk_open_file_dialog(pathBuf, 4096) → C++ 弹出 DFileDialog
                 ├─ dtk_read_file(path, pathLen, contentBuf, 65536) → C++ 读取文件
                 ├─ dtk_set_editor_text(contentBuf, contentLen) → C++ 设置文本
                 └─ dtk_set_label_text(statusLabel, "Opened: <路径>")
```

### 崩溃测试流程

```
用户点击 "Crash Test" 按钮
  │
  └─ callExport_ii("on_event", 4, crashButtonId)  ← 通用入口
       │  callExport 返回后检查 isAlive()
       │
       └─ WASM on_event(4, widgetId):
            └─ switch(4) → handle_crash():
                 ├─ dtk_set_label_text(statusLabel, "About to trigger...")
                 └─ __builtin_unreachable() → WASM RuntimeError
                      → Worker try/catch → {type:'error', crash:true}
                      → 子进程退出

  C++ 检测到 isAlive() == false
    → handleWasmCrash():
         ├─ 清理旧控件 + 事件映射 + 菜单项
         ├─ loadWasmPlugin() — 重新加载 WASM 插件
         └─ 显示恢复消息
```

### 右键菜单流程

```
用户右键
  │
  └─ showContextMenu():
       ├─ 内置菜单项: Copy, Paste（宿主原语）
       ├─ WASM 注册的菜单项: "Toggle Theme"（来自 dtk_add_context_menu_item）
       └─ 用户选择 "Toggle Theme"
            │
            └─ callExport_ii("on_event", 6, 0)  ← 通用入口
                 │
                 └─ WASM on_event(6, 0):
                      └─ switch(6) → handle_toggle_theme():
                           ├─ dtk_get_theme() → C++ 返回当前主题
                           └─ dtk_set_theme(新值) → C++ 切换主题
```

## IPC 协议

### 前端：C++ ↔ Node.js（行分隔 JSON，双向）

**C++ → Node.js（stdin）**：

| 消息类型 | 示例 | 说明 |
|---------|------|------|
| `call` | `{"type":"call","func":"build_ui","args":[]}` | 调用 WASM 导出函数 |
| `call` | `{"type":"call","func":"on_event","args":[6,0]}` | 调用 on_event(eventId, widgetId) |
| `import_result` | `{"type":"import_result","value":5,"str":"/home/user/file.txt"}` | 返回 DTK 宿主回调结果 |
| `quit` | `{"type":"quit"}` | 通知子进程退出 |

**Node.js → C++（stdout）**：

| 消息类型 | 示例 | 说明 |
|---------|------|------|
| `import` | `{"type":"import","func":"dtk_subscribe_event","args":[4,1,1]}` | DTK 宿主回调请求 |
| `call_result` | `{"type":"call_result","value":0}` | WASM 导出函数调用结果 |
| `error` | `{"type":"error","error":"WASM crash: unreachable","crash":true}` | 错误/崩溃消息 |

## 功能特性

- **事件驱动架构**：宿主源码不包含任何插件业务知识——三方换插件时宿主零修改
- **WASM 拥有全部编辑器逻辑**：客户端仅嵌入界面展示，不承担任何逻辑
- **3 个通用导出函数**：`build_ui`/`on_event`/`alloc`——固定不变，适用于所有插件
- **C++ 源码编译为 WASM**：插件源码是标准 C++，用 Emscripten 编译
- **WASM 定义 UI**：整个 UI 布局由 WASM 插件通过调用 DTK 宿主函数创建控件
- **进程隔离**：WASM 在独立进程中运行，插件崩溃不影响主程序
- **崩溃恢复**：WASM 崩溃后主程序自动清理旧控件并重新加载插件
- **DTK 依赖强制**：WASM 模块导入 20 个 DTK 宿主函数，无 DTK 则无法实例化
- **无 libc 依赖**：手写字符串/内存函数，`-nostdlib` 编译
- **右键上下文菜单**：复制、粘贴 + WASM 插件动态注册的菜单项
- **深色/浅色主题**：通过右键菜单切换，由 WASM 插件驱动
- **文件操作**：新建、打开、保存——全部逻辑在 WASM 内，C++ 仅提供文件 I/O 原语
- **后备 UI**：WASM 加载失败时用 C++ 直接创建相同的 UI 控件
