# WASM 驱动的聊天应用（DTK）

基于 Qt/DTK6 的聊天应用。**插件源码是 C++ 代码**，用 Emscripten 编译为 WASM。
WASM 插件以**进程插件**形式运行——在独立的 Node.js 子进程中执行，
通过 JSON IPC 与 DTK 宿主通信。**插件崩溃不影响主程序。**

## 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                       DTK 客户端进程（纯展示宿主）              │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  WASM UI 插件 (chat_ui_plugin.wasm)                  │    │
│  │  C++ 源码 → Emscripten 编译 → WASM                    │    │
│  │  ─ 全部交互与数据逻辑在 WASM 内闭环 ─                  │    │
│  │  导入 16 个 DTK 宿主函数 (env 模块)                    │    │
│  │  导出 7 个函数 (build_ui/on_send_clicked/             │    │
│  │    on_server_message/on_connected/                    │    │
│  │    on_disconnected/toggle_theme/alloc)                │    │
│  │  没有 DTK 宿主，插件无法实例化！                        │    │
│  └────────────────────┬────────────────────────────────┘    │
│                       │ JSON IPC (stdin/stdout 行分隔)        │
│  ┌────────────────────┴────────────────────────────────┐    │
│  │  WasmProcessBridge (QProcess)                        │    │
│  │    → 启动 node wasm_dtk_host.js 子进程                 │    │
│  │    ← 接收 import 请求 → 调用 DTK 宿主回调              │    │
│  │    → 返回 import_result (+ result_str)               │    │
│  │  DtkChatClient (纯转发层):                            │    │
│  │    事件 → callExport("on_xxx")                        │    │
│  │    不承担任何交互/数据逻辑                              │    │
│  │  DTK 宿主提供: DLabel, DTextEdit,                     │    │
│  │  DLineEdit, DSuggestButton, DMenu                     │    │
│  └─────────────────────────────────────────────────────┘    │
│                       ↓ TCP (127.0.0.1:12345)                 │
└───────────────────────┬─────────────────────────────────────┘
                        │
┌───────────────────────┴─────────────────────────────────────┐
│                       服务器进程                               │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  后端 WASM 插件 (random_reply_plugin.wasm)           │    │
│  │  C++ 源码 → Emscripten 编译 → WASM                    │    │
│  │  自包含（零 import），10 条预设回复                      │    │
│  │  通过 Node.js 桥接执行 (wasm_runner.js)               │    │
│  └─────────────────────────────────────────────────────┘    │
│  Qt6 TCP 服务器 (ChatServer)                                  │
└─────────────────────────────────────────────────────────────┘
```

### 进程隔离

WASM 插件在**独立 Node.js 子进程**中运行，与主程序隔离：

- **插件崩溃不影响主程序**——子进程崩溃后主程序仍可运行，可重启子进程
- 主程序通过 `QProcess` 管理 WASM 子进程的 stdin/stdout
- IPC 协议为行分隔 JSON（每行一个 JSON 对象，以 `\n` 分隔）

### 进程插件同步桥接（前端）

前端 WASM 插件需要同步调用 DTK 宿主函数并获取返回值。
Node.js 主线程是异步的，WASM 在 Worker 线程中同步执行。
通过 **SharedArrayBuffer + Atomics.wait/notify** 跨线程同步：

```
DTK 客户端进程                     Node.js 子进程 (wasm_dtk_host.js)
┌──────────────────┐             ┌──────────────────────────────────┐
│ DtkChatClient    │  stdin →    │ 主线程                            │
│  (纯转发层)       │ ←stdout    │  readline 异步处理 stdin/stdout   │
│                  │             │  pollImport() 轮询 SharedArrayBuffer│
│  handleDtkHostCall│ ←import    │  ↕ postMessage                   │
│  → new DLabel(str)│ result→    │ Worker 线程                       │
│  → 返回 widget_id │            │  WebAssembly.instantiate         │
│  → 返回 input_str │            │  chat_ui_plugin.wasm             │
│  (无交互逻辑)      │            │  ─ 全部逻辑在 WASM 内 ─            │
│                  │             │  import 函数 (16 个):              │
│                  │             │    预读 WASM 内存字符串            │
│                  │             │    → 写入 SharedArrayBuffer       │
│                  │             │    → Atomics.notify(主线程)        │
│                  │             │    → Atomics.wait(阻塞等结果)      │
│                  │             │    ← 主线程写入 result + result_str│
│                  │             │    → Atomics.notify(唤醒)          │
└──────────────────┘             └──────────────────────────────────┘
```

**预读字符串避免死锁**：带字符串参数的 import 函数（`dtk_create_label`、
`dtk_create_lineedit`、`dtk_create_pushbutton`、`dtk_set_window_title`、
`dtk_append_message`、`dtk_set_label_text`、`dtk_send_to_server`）在 Worker 端
预读 WASM 线性内存中的字符串，附带在 import 请求中通过 SharedArrayBuffer 传给 C++ 宿主。
这样 C++ 不需要回读 WASM 内存（那会导致死锁：C++ 阻塞等 stdout，
Node 阻塞等 import_result）。

**结果字符串返回**：`dtk_get_input_text` 需要将输入框文本从 C++ 传回 WASM 内存。
C++ 通过 `import_result` 的 `str` 字段返回文本，主线程写入 SAB 的 `RESULT_STR` 区域，
Worker 收到后将其复制到 WASM 内存中 `ptr` 指向的位置。

### 服务器端 WASM 桥接（后端）

后端 WASM 插件自包含（零 import），无需同步桥接。
`WasmPluginLoader` 通过 `QProcess` 一次性调用 `wasm_runner.js`：

```
ChatServer (C++)
  │
  ├─ WasmPluginLoader::invoke("generate_reply", {seed: 42})
  │    │
  │    ├─ QProcess 启动 node wasm_runner.js
  │    ├─ stdin 写入: {"wasm_path":"plugins/random_reply_plugin.wasm","action":"generate_reply","seed":42}
  │    ├─ closeWriteChannel() → Node 读取 → 实例化 WASM → 调用 generate_reply(42)
  │    └─ stdout 读取: {"success":true,"result":"WASM plugins are awesome!"}
  │
  └─ 返回回复文本
```

每次调用启动一个新 Node 进程（一次性模式），适合服务器低频调用场景。

## 关键设计决策

### 1. 插件源码是 C++ 代码

WASM 插件源码是标准 C++ 文件（`chat_ui_plugin.cpp`、`random_reply_plugin.cpp`），
用 **Emscripten (emcc)** 编译为 WASM。不是手写 WAT/汇编。

### 2. WASM 插件依赖 DTK

前端 WASM 插件通过 `extern "C"` 声明 16 个 DTK 宿主函数：

```cpp
extern "C" {
    int32_t dtk_create_label(const char* text, int32_t len);
    int32_t dtk_create_textedit(int32_t readonly, int32_t unused);
    // ... 共 16 个
}
```

Emscripten 编译时，这些未定义符号通过 `-Wl,--import-undefined` 自动成为
WASM import（位于 `env` 模块下）。没有 DTK 宿主提供这些 import，
WASM 模块**无法实例化**——这就是 DTK 依赖的强制机制。

验证：`chat_ui_plugin.wasm` 有 16 个 import，全部是 `env.dtk_*` 函数。

### 3. 进程插件（进程隔离）

WASM 运行时**不在 DTK 进程内**——作为独立 Node.js 子进程运行。
插件崩溃不影响主程序，只需重启子进程。
前端使用长驻子进程（`wasm_dtk_host.js`），后端使用一次性子进程（`wasm_runner.js`）。

### 4. 无 libc 依赖

插件使用手写 `my_strlen`/`my_memcpy`，用 `__attribute__((noinline, optnone))`
防止编译器将循环优化为 `strlen`/`memcpy` 的 libc 调用。
编译时用 `-nostdlib`，生成的 WASM 模块无 libc import。

验证：`random_reply_plugin.wasm` 零 import；`chat_ui_plugin.wasm` 仅 16 个 DTK import。

### 5. DTK 控件

所有 UI 控件均为 DTK 控件：

| 控件类 | 用途 |
|--------|------|
| `DMainWindow` | 主窗口 |
| `DLabel` | 标题标签、状态标签 |
| `DTextEdit` | 消息显示区（只读，深色主题） |
| `DLineEdit` | 消息输入框 |
| `DSuggestButton` | 发送按钮 |
| `DMenu` | 右键上下文菜单 |

### 6. 深色/浅色主题

消息显示区使用显式调色板：

- **深色**：`QColor(30, 30, 30)` 背景，`QColor(220, 220, 220)` 文字
- **浅色**：`QColor(250, 250, 250)` 背景，`QColor(30, 30, 30)` 文字

主题切换通过 WASM 插件的 `toggle_theme` 导出函数实现——
该函数调用 `dtk_get_theme`/`dtk_set_theme` 宿主导入完成切换。
`applyThemeColors()` 在 `DTextEdit` 及其 viewport 上设置调色板和样式表，
对所有子控件 `unpolish`/`polish` 强制刷新。

## 文件结构

```
wasm/
├── CMakeLists.txt                  # CMake 构建配置（3 个目标）
├── README.md                       # 本文件
├── package.json                    # npm 脚本（build:plugins → compile_plugins.sh）
│
├── plugins/                        # WASM 插件
│   ├── chat_ui_plugin.cpp          # 前端 UI 插件 C++ 源码
│   ├── chat_ui_plugin.wasm         # 编译产物（Emscripten，~1.3KB）
│   ├── random_reply_plugin.cpp     # 后端回复插件 C++ 源码
│   └── random_reply_plugin.wasm    # 编译产物（Emscripten，~775B）
│
├── scripts/
│   ├── compile_plugins.sh          # Emscripten 编译脚本
│   ├── wasm_dtk_host.js            # 前端 WASM 进程插件运行时（主线程+Worker）
│   └── wasm_runner.js              # 服务器端 WASM 一次性执行桥接
│
├── src/                            # C++ 源代码
│   ├── dtk_client_main.cpp         # DTK 客户端入口
│   ├── DtkChatClient.h             # DTK 客户端核心（仅头文件，584 行）
│   ├── WasmProcessBridge.h         # QProcess WASM 桥接（仅头文件，282 行）
│   ├── server_main.cpp             # 服务器入口
│   ├── ChatServer.h                # Qt6 TCP 服务器（仅头文件，153 行）
│   ├── ChatServer.cpp              # ChatServer 桩文件
│   ├── WasmPluginLoader.h          # 服务器 WASM 桥接（仅头文件，148 行）
│   └── WasmPluginLoader.cpp        # WasmPluginLoader 桩文件
│
└── tests/
    └── test_e2e.cpp                # 端到端测试
```

## 各文件说明

### WASM 插件（`plugins/`）

#### `chat_ui_plugin.cpp` — 前端 UI 插件 C++ 源码

用 Emscripten 编译为 `chat_ui_plugin.wasm`。通过 `extern "C"` 声明 16 个 DTK 宿主函数
（编译为 WASM import），导出 7 个函数供宿主通过 IPC 调用。

**静态字符串数据**（编译时放入 WASM linear memory 的 data segment）：

| 常量 | 值 | 用途 |
|------|-----|------|
| `STR_WINDOW_TITLE` | `"WASM-Powered Chat (DTK)"` | 窗口标题 |
| `STR_TITLE_LABEL` | `"WASM-Powered Chat Client (DTK)"` | 标题标签 |
| `STR_STATUS_LABEL` | `"Initializing..."` | 状态标签 |
| `STR_PLACEHOLDER` | `"Type your message here..."` | 输入框占位文本 |
| `STR_SEND` | `"Send"` | 按钮文本 |
| `STR_PREFIX` | `"[Me] "` | 消息前缀 |

**导出函数**：

| 函数 | 签名 | 说明 |
|------|------|------|
| `build_ui()` | `() → i32` | 调用 DTK 宿主函数创建全部 UI 控件（窗口标题/尺寸、2 个标签、文本编辑器、水平布局、输入框、按钮），保存控件 ID，初始禁用输入，返回主布局 ID |
| `on_send_clicked()` | `() → i32` | 读取输入框文本 → 格式化 `"[Me] "` 前缀 → 显示 → 发送到服务器 → 清空输入（全部逻辑在 WASM 内） |
| `on_server_message(ptr, len)` | `(i32, i32) → i32` | 格式化 `"[Server] "` 前缀 → 显示服务器回复消息 |
| `on_connected()` | `() → i32` | 更新状态标签 → 启用输入框和按钮 → 显示连接消息 |
| `on_disconnected()` | `() → i32` | 更新状态标签 → 禁用按钮 → 显示断开消息 |
| `toggle_theme()` | `() → i32` | 调用 `dtk_get_theme` 获取当前主题，调用 `dtk_set_theme` 切换，返回旧主题值 |
| `alloc(size)` | `(i32) → i32` | WASM 线性内存 bump 分配器，宿主通过 IPC 调用来分配内存 |

**`build_ui()` 执行序列**（17 次 DTK 宿主调用）：

```
1. dtk_set_window_title("WASM-Powered Chat (DTK)")
2. dtk_set_window_size(600, 480)
3. dtk_create_hbox(0, 0)              → mainLayout = 0（主垂直布局）
4. dtk_create_label("WASM-Powered Chat Client (DTK)")  → titleLabel = 1
5. dtk_add_to_layout(0, 1)            → 标题标签加入主布局
6. dtk_create_label("Initializing...")  → statusLabel = 2
7. dtk_add_to_layout(0, 2)            → 状态标签加入主布局
8. dtk_create_textedit(1, 0)          → textEdit = 3（只读）
9. dtk_add_to_layout(0, 3)            → 消息显示区加入主布局
10. dtk_create_hbox(1, 0)             → hbox = 4（输入行水平布局）
11. dtk_add_to_layout(0, 4)           → 输入行加入主布局
12. dtk_create_lineedit("Type your message here...")  → inputField = 5
13. dtk_add_to_layout(4, 5)           → 输入框加入输入行
14. dtk_create_pushbutton("Send")     → sendButton = 6
15. dtk_add_to_layout(4, 6)           → 发送按钮加入输入行
16. dtk_set_widget_enabled(inputField, 0)  → 初始禁用输入框
17. dtk_set_widget_enabled(sendButton, 0)  → 初始禁用发送按钮
返回 mainLayout (0)
```

#### `random_reply_plugin.cpp` — 后端回复插件 C++ 源码

自包含（零 import），导出 `generate_reply(seed)` 和 `get_reply_len(seed)`。
10 条预设回复，基于 `seed % 10` 选择。

| 函数 | 签名 | 说明 |
|------|------|------|
| `generate_reply(seed)` | `(i32) → i32` | 根据 `seed % 10` 选择回复，复制到堆内存，返回指针 |
| `get_reply_len(seed)` | `(i32) → i32` | 返回指定 seed 对应回复的字符串长度 |

预设回复：
```
0: "Hello! Nice to meet you."
1: "That's interesting!"
2: "WASM plugins are awesome!"
3: "Tell me more about it."
4: "I'm a WASM-powered server."
5: "Qt and WASM work great together!"
6: "Let me think about that..."
7: "Great question!"
8: "Processing your request..."
9: "Goodbye! See you next time."
```

### 脚本（`scripts/`）

#### `compile_plugins.sh` — Emscripten 编译脚本

用 `emcc` 将 C++ 插件源码编译为 WASM。

**前端插件编译参数**：

| 参数 | 说明 |
|------|------|
| `-O2` | 优化级别 |
| `-s WASM=1` | 生成独立 .wasm 文件 |
| `-s ALLOW_MEMORY_GROWTH=1` | 允许线性内存动态增长 |
| `-s EXPORTED_FUNCTIONS='["_build_ui","_on_send_clicked","_on_server_message","_on_connected","_on_disconnected","_toggle_theme","_alloc"]'` | 导出函数 |
| `-s ERROR_ON_UNDEFINED_SYMBOLS=0` | 允许未定义符号（DTK 宿主导入） |
| `-nostdlib` | 不链接标准库（避免 libc import） |
| `-Wl,--no-entry` | 无入口点（纯库模块） |
| `-Wl,--import-undefined` | 未定义符号编译为 WASM import |

后端插件参数相同（无 `--import-undefined`，因为自包含）。

#### `wasm_dtk_host.js` — 前端 WASM 进程插件运行时

长驻 Node.js 子进程，主线程 + Worker 线程架构。

**SharedArrayBuffer 布局**（8244 字节）：

| 偏移 | 大小 | 用途 |
|------|------|------|
| 0 | 4 | TAG: 0=idle, 1=import 请求就绪 |
| 4 | 4 | 函数名长度 |
| 8 | 32 | 函数名（UTF-8） |
| 40 | 4 | 参数 1 (arg1) |
| 44 | 4 | 参数 2 (arg2) |
| 48 | 4 | 参数 3 (arg3) |
| 52 | 4 | 字符串长度 |
| 56 | 4096 | 字符串数据（UTF-8） |
| 4152 | 4 | 结果值 (result) |
| 4156 | 4 | 结果字符串长度 (result_strlen) |
| 4160 | 4096 | 结果字符串数据（UTF-8） |

1. 读取 WASM 二进制，构建 16 个 DTK import 函数（`makeDtkImport`）
2. `WebAssembly.instantiate` 实例化（import 对象提供在 `env` 模块下）
3. 收到 `call` 消息时同步执行 WASM 导出函数
4. WASM import 函数执行时：预读字符串 → 写入 SAB → `Atomics.notify` → `Atomics.wait` 阻塞

**主线程**：
1. `readline` 异步处理 stdin/stdout JSON 行协议
2. 收到 C++ 的 `call`/`read_memory`/`write_memory` 请求 → 转发给 Worker
3. `pollImport()` 轮询 SAB，检测到 import 请求 → 通过 stdout 转发给 C++
4. 收到 C++ 的 `import_result` → 写入 SAB → `Atomics.notify` 唤醒 Worker

#### `wasm_runner.js` — 服务器端 WASM 桥接

一次性 Node.js 脚本，由 `WasmPluginLoader` 通过 `QProcess` 调用。
从 stdin 读取完整 JSON 请求，加载 WASM，执行函数，通过 stdout 返回结果。
后端插件零 import，无需同步桥接。

### 客户端源码（`src/`）

#### `dtk_client_main.cpp` — DTK 客户端入口

创建 `DApplication`，配置 DDE 平台集成，实例化 `DtkChatClient`，显示窗口，进入事件循环。

#### `DtkChatClient.h` — DTK 客户端核心（仅头文件，584 行）

继承 `DMainWindow`，是 DTK 宿主和事件转发的核心。**纯展示宿主——不承担任何交互/数据逻辑**。

**构造函数流程**：
1. 创建 centralWidget + `QVBoxLayout`（主布局容器）
2. 注册 `WasmProcessBridge::s_hostCallback` → `handleDtkHostCall`
3. `WasmProcessBridge::load("plugins/chat_ui_plugin.wasm")` 启动 Node 子进程
4. `callExport("build_ui")` 触发 WASM 执行，创建全部 UI 控件
5. `applyThemeColors()` 应用深色/浅色主题
6. 连接 Send 按钮和输入框的信号
7. 创建 `QTcpSocket`，500ms 后自动连接服务器

**`handleDtkHostCall(func, a1, a2, a3, str, outResultStr)`** — 处理 16 个 DTK 导入命令：

| 命令 | C++ 动作 | 返回值 |
|------|---------|--------|
| `dtk_create_label` | `new DLabel(str, this)`，首个标签设粗体 14pt | 控件 ID |
| `dtk_create_textedit` | `new DTextEdit`，设只读，安装事件过滤器 | 控件 ID |
| `dtk_create_lineedit` | `new DLineEdit`，设占位文本，初始禁用 | 控件 ID |
| `dtk_create_pushbutton` | `new DSuggestButton(str)`，初始禁用 | 控件 ID |
| `dtk_create_hbox` | `a1==0` 返回 0（用预建主布局），否则 `new QHBoxLayout` | 布局 ID |
| `dtk_add_to_layout` | 查找布局和控件注册表，`addWidget`/`addLayout` | 0 |
| `dtk_set_window_title` | `setWindowTitle(str)` | 0 |
| `dtk_set_window_size` | `resize(a1, a2)` | 0 |
| `dtk_get_theme` | 返回当前主题（0=浅色, 1=深色） | 主题值 |
| `dtk_set_theme` | 设置 `DGuiApplicationHelper` 调色板类型，调用 `applyThemeColors` | 0 |
| `dtk_append_message` | `appendMessage(str)` 添加带时间戳的消息 | 0 |
| `dtk_get_input_text` | 读取 `m_inputField` 文本，写入 `outResultStr` | 文本长度 |
| `dtk_clear_input` | `m_inputField->clear()` | 0 |
| `dtk_set_label_text` | 查找 `m_widgets[a1]`，`setText(str)` | 0 |
| `dtk_set_widget_enabled` | 查找 `m_widgets[a1]`，`setEnabled(a2)` | 0 |
| `dtk_send_to_server` | TCP 发送 `{"text":str}` 到服务器 | 0 |

控件/布局通过 `QMap<int32_t, QWidget*> m_widgets` 和
`QMap<int32_t, QHBoxLayout*> m_layouts` 注册管理，
WASM 返回的 ID 是后续 `add_to_layout` 的引用句柄。

**右键上下文菜单**（`showContextMenu`）：
- **Copy** — 从消息区或输入框复制选中文本
- **Paste** — 粘贴剪贴板到输入框
- **Save As...** — `DFileDialog` 保存聊天记录到文本文件
- **Theme Toggle** — 调用 WASM `toggle_theme` 导出函数切换主题

**消息发送**（`onSendClicked`）— 纯转发，无逻辑：
1. `callExport("on_send_clicked")` → WASM 处理全部逻辑（读输入 → 格式化 → 显示 → 发送 → 清空）

**消息接收**（`onReadyRead`）— 纯转发，无逻辑：
1. 读取 TCP 数据，解析 JSON
2. `alloc(len)` + `writeString(ptr, reply)` 写入 WASM 内存
3. `callExport_ii("on_server_message", ptr, len)` → WASM 格式化并显示

**连接状态**（`onConnected`/`onDisconnected`）— 纯转发：
- `onConnected()` → `callExport("on_connected")` → WASM 更新状态、启用控件、显示消息
- `onDisconnected()` → `callExport("on_disconnected")` → WASM 更新状态、禁用控件、显示消息 + 3s 重连

**TCP 通信**：
- 自动连接 `127.0.0.1:12345`
- 断线后 3 秒自动重连

**后备 UI**（`buildFallbackUi`）：
WASM 加载失败时用 C++ 直接创建相同的 UI 控件，保证应用可用。

#### `WasmProcessBridge.h` — QProcess WASM 桥接（仅头文件，282 行）

管理 `wasm_dtk_host.js` 子进程的生命周期和 IPC 通信。

**API**：

| 方法 | 说明 |
|------|------|
| `load(wasmPath)` | 启动 Node 子进程，等待 WASM 加载就绪 |
| `callExport(func, *out)` | 调用 0 参数 WASM 导出函数 |
| `callExport_i32(func, arg, *out)` | 调用 1 参数 WASM 导出函数 |
| `callExport_ii(func, arg1, arg2, *out)` | 调用 2 参数 WASM 导出函数 |
| `readMemory(offset, len)` | 读取 WASM 线性内存 |
| `writeMemory(offset, data)` | 写入 WASM 线性内存 |
| `readString(offset, len)` | 从 WASM 内存读取字符串 |
| `writeString(offset, str)` | 写入字符串到 WASM 内存 |
| `alloc(size)` | 调用 WASM `alloc` 导出函数 |
| `isAlive()` | 检查子进程是否存活 |

**`sendAndProcess(request)`** — 核心通信循环：
1. 写 JSON 请求到子进程 stdin
2. 循环读取 stdout 行：
   - `import` → 调用 `s_hostCallback`，返回 `import_result`
   - `call_result` → 返回给调用者
   - `error` → 返回错误

`readLineSync(timeoutMs)` 使用 `waitForReadyRead` + `readAll` 同步读取一行 JSON。

回调类型：`std::function<int32_t(const QString &func, int32_t arg1, int32_t arg2, int32_t arg3, const QString &str, QString &outResultStr)>`，
`str` 由 JS 端预读 WASM 内存后通过 IPC 传递，`outResultStr` 用于返回结果字符串（如 `dtk_get_input_text`）。

### 服务器源码（`src/`）

#### `server_main.cpp` — 服务器入口

从 `argv` 解析端口号（默认 12345），实例化 `ChatServer`，进入事件循环。

#### `ChatServer.h` — Qt6 TCP 服务器（仅头文件，153 行）

监听端口，接受连接，读取换行分隔的 JSON 消息。

**消息处理流程**：
1. 解析 `{"text":"用户消息"}` JSON
2. 生成种子：`seed = timestamp_ms ⊕ (counter × 2654435761) ⊕ message_length`
3. 调用 `WasmPluginLoader::invoke("generate_reply", {seed})` 执行后端 WASM 插件
4. 返回 `{"text":"回复","echo":"原文","seed":种子}`

#### `WasmPluginLoader.h` — 服务器 WASM 桥接（仅头文件，148 行）

通过 `QProcess` 一次性调用 `wasm_runner.js`。
`invoke(action, params)` 方法：
1. 构建 JSON 请求 `{wasm_path, action, ...params}`
2. `QProcess` 启动 `node wasm_runner.js`
3. stdin 写入 JSON，`closeWriteChannel()`
4. `QEventLoop` + `QTimer` 同步等待完成（超时 10s）
5. 解析 stdout JSON，返回 `result` 字段

### 测试（`tests/`）

#### `test_e2e.cpp` — 端到端测试

连接服务器，依次发送 5 条测试消息：
```
"Hello WASM Server!"
"Tell me about WebAssembly"
"How does Qt work with WASM?"
"What is linear memory?"
"Goodbye!"
```
验证每条消息都收到包含 `echo`（回显）、`text`（随机回复）、`seed`（种子）的 JSON 响应。
全部收到后输出 `E2E Test PASSED` 并退出。

### 构建配置

#### `CMakeLists.txt`

3 个构建目标：

| 目标 | 源文件 | 链接库 | 说明 |
|------|--------|--------|------|
| `dtkchatclient` | `dtk_client_main.cpp`, `DtkChatClient.h`, `WasmProcessBridge.h` | Qt6::Core/Network/Widgets, Dtk6::Widget/Gui/Core | DTK 客户端 |
| `wasmchatserver` | `server_main.cpp`, `ChatServer.h/.cpp`, `WasmPluginLoader.h/.cpp` | Qt6::Core/Network | TCP 服务器 |
| `test_e2e` | `test_e2e.cpp` | Qt6::Core/Network | 端到端测试 |

使用 `CMAKE_AUTOMOC ON`，`C++17` 标准。

#### `package.json`

```json
{
  "name": "wasm-chat",
  "version": "1.0.0",
  "scripts": {
    "build:plugins": "bash scripts/compile_plugins.sh"
  }
}
```

## 编译与运行

### 环境要求

| 依赖 | 验证版本 | 用途 |
|------|---------|------|
| Qt6 | 6.8.0 | C++ GUI 框架（Core / Network / Widgets） |
| DTK6 | 6.7.49 | Deepin 工具套件（Widget / Gui / Core） |
| CMake | 3.31.4 | C++ 构建系统 |
| g++ | 12.3.0 | C++ 编译器 |
| Node.js | v24.7.0 | WASM 进程插件运行时 |
| Emscripten | 6.0.12 | C++ → WASM 编译器（emcc） |

### 第一步：安装并激活 Emscripten

```bash
cd /home/ut000520@uos/Desktop
git clone --depth 1 https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
```

每次新终端会话使用前，需激活 Emscripten 环境：

```bash
source /home/ut000520@uos/Desktop/emsdk/emsdk_env.sh
```

验证：

```bash
emcc --version    # 应输出 emcc 6.0.12
```

### 第二步：编译 C++ 插件源码为 WASM

```bash
cd /home/ut000520@uos/Desktop/wasm
source /home/ut000520@uos/Desktop/emsdk/emsdk_env.sh
bash scripts/compile_plugins.sh
```

输出：

```
=== Compiling chat_ui_plugin.cpp ===
  → /home/ut000520@uos/Desktop/wasm/plugins/chat_ui_plugin.wasm
=== Compiling random_reply_plugin.cpp ===
  → /home/ut000520@uos/Desktop/wasm/plugins/random_reply_plugin.wasm
=== Done! ===
```

验证 WASM 模块的 import/export：

```bash
node -e '
const fs = require("fs");
async function check(file) {
    const mod = await WebAssembly.compile(new Uint8Array(fs.readFileSync(file)));
    const imports = WebAssembly.Module.imports(mod);
    const exports = WebAssembly.Module.exports(mod);
    console.log(file);
    console.log("  imports:", imports.length ? imports.map(i => i.module+"."+i.name).join(", ") : "none");
    console.log("  exports:", exports.map(e => e.name).join(", "));
}
check("plugins/chat_ui_plugin.wasm").then(() => check("plugins/random_reply_plugin.wasm"));
'
```

预期输出：

```
plugins/chat_ui_plugin.wasm
  imports: env.dtk_set_window_title, env.dtk_set_window_size, env.dtk_create_hbox, env.dtk_create_label, env.dtk_add_to_layout, env.dtk_create_textedit, env.dtk_create_lineedit, env.dtk_create_pushbutton, env.dtk_append_message, env.dtk_get_theme, env.dtk_set_theme, env.dtk_get_input_text, env.dtk_clear_input, env.dtk_set_label_text, env.dtk_set_widget_enabled, env.dtk_send_to_server
  exports: memory, __indirect_function_table, alloc, build_ui, on_send_clicked, on_server_message, on_connected, on_disconnected, toggle_theme
plugins/random_reply_plugin.wasm
  imports: none
  exports: memory, __indirect_function_table, generate_reply, get_reply_len
```

### 第三步：编译 C++ 目标

```bash
cd /home/ut000520@uos/Desktop/wasm
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

输出：

```
[ 76%] Built target wasmchatserver
[ 88%] Built target test_e2e
[100%] Built target dtkchatclient
```

生成 3 个可执行文件：

| 文件 | 说明 |
|------|------|
| `build/dtkchatclient` | DTK 客户端（WASM 前端 UI 插件） |
| `build/wasmchatserver` | TCP 服务器（WASM 后端回复插件） |
| `build/test_e2e` | 端到端测试 |

### 第四步：运行

> **重要**：所有程序必须从**项目根目录**（`/home/ut000520@uos/Desktop/wasm`）运行，
> 因为程序内部使用相对路径查找 `plugins/*.wasm` 和 `scripts/*.js`。

```bash
cd /home/ut000520@uos/Desktop/wasm

# 1. 启动服务器（后台运行，监听 12345 端口）
./build/wasmchatserver 12345 &

# 2. 启动 DTK 客户端（前台运行，自动连接 127.0.0.1:12345）
DISPLAY=:0 QT_QPA_PLATFORM=xcb ./build/dtkchatclient
```

服务器启动输出：

```
[ChatServer] Backend WASM plugin loaded: plugins/random_reply_plugin.wasm
[ChatServer] Listening on port 12345
WASM Chat Server running. Press Ctrl+C to quit.
```

客户端启动输出：

```
[WasmProcessBridge] Node process started, WASM plugin loaded: "plugins/chat_ui_plugin.wasm"
WASM UI plugin loaded — calling build_ui()...
[DTK Host] Set window title: "WASM-Powered Chat (DTK)"
[DTK Host] Set window size: 600 x 480
[DTK Host] Main vertical layout (id: 0)
[DTK Host] Created label: "WASM-Powered Chat Client (DTK)" id: 1
[DTK Host] add_to_layout layout: 0 widget: 1
[DTK Host] Created label: "Initializing..." id: 2
[DTK Host] add_to_layout layout: 0 widget: 2
[DTK Host] Created text_edit (readonly= 1 ) id: 3
[DTK Host] add_to_layout layout: 0 widget: 3
[DTK Host] Created hbox id: 4 flag: 1
[DTK Host] add_to_layout layout: 0 widget: 4
[DTK Host] Created line_edit: "Type your message here..." id: 5
[DTK Host] add_to_layout layout: 4 widget: 5
[DTK Host] Created push_button id: 6
[DTK Host] add_to_layout layout: 4 widget: 6
[DTK Host] set_widget_enabled widget: 5 enabled: 0
[DTK Host] set_widget_enabled widget: 6 enabled: 0
UI built by WASM plugin, layout ID: 0
```

### 第五步：端到端测试

```bash
cd /home/ut000520@uos/Desktop/wasm

# 确保服务器正在运行
./build/wasmchatserver 12345 &
sleep 2

# 运行测试（发送 5 条消息，验证回复）
./build/test_e2e 127.0.0.1 12345
```

预期输出：

```
=== WASM Chat E2E Test ===
Connecting to 127.0.0.1:12345...
[OK] Connected to server!

[SENT] Hello WASM Server!
[RECV] Echo: Hello WASM Server!
[RECV] Reply: That's interesting!
[RECV] Seed: 590935161

[SENT] Tell me about WebAssembly
[RECV] Echo: Tell me about WebAssembly
[RECV] Reply: WASM plugins are awesome!
[RECV] Seed: 1024429862
...
[SENT] Goodbye!
[RECV] Echo: Goodbye!
[RECV] Reply: Processing your request...
[RECV] Seed: 1541741088

=== All messages sent! ===
=== E2E Test PASSED ===
[INFO] Disconnected
```

> 回复内容由 `seed % 10` 选择，每次运行结果不同。

### 快速参考（完整流程）

```bash
cd /home/ut000520@uos/Desktop/wasm

# 1. 激活 Emscripten
source /home/ut000520@uos/Desktop/emsdk/emsdk_env.sh

# 2. 编译 WASM 插件
bash scripts/compile_plugins.sh

# 3. 编译 C++ 目标
mkdir -p build && cd build && cmake .. && make -j$(nproc) && cd ..

# 4. 启动服务器
./build/wasmchatserver 12345 &
sleep 2

# 5. 启动客户端（另一个终端）
DISPLAY=:0 QT_QPA_PLATFORM=xcb ./build/dtkchatclient

# 6. 运行测试（另一个终端）
./build/test_e2e 127.0.0.1 12345

# 7. 停止服务器
kill %1 2>/dev/null  # 或 pkill -f wasmchatserver
```

> **注意**：在 DDE/UOS 上需使用 `QT_QPA_PLATFORM=xcb` 以正确渲染窗口。

## IPC 协议

### 前端：C++ ↔ Node.js（行分隔 JSON，双向）

**C++ → Node.js（stdin）**：

| 消息类型 | 示例 | 说明 |
|---------|------|------|
| `call` | `{"type":"call","func":"build_ui","args":[]}` | 调用 WASM 导出函数 |
| `import_result` | `{"type":"import_result","value":5,"str":"Hello WASM Server!"}` | 返回 DTK 宿主回调结果（`str` 为结果字符串，如 `dtk_get_input_text` 返回的输入文本） |
| `read_memory` | `{"type":"read_memory","offset":512,"len":30}` | 读取 WASM 线性内存 |
| `write_memory` | `{"type":"write_memory","offset":ptr,"hex":"..."}` | 写入 WASM 线性内存 |
| `quit` | `{"type":"quit"}` | 通知子进程退出 |

**Node.js → C++（stdout）**：

| 消息类型 | 示例 | 说明 |
|---------|------|------|
| `import` | `{"type":"import","func":"dtk_create_label","args":[536,30],"str":"WASM-Powered Chat Client (DTK)"}` | DTK 宿主回调请求（`str` 为预读的字符串参数） |
| `call_result` | `{"type":"call_result","value":0}` | WASM 导出函数调用结果 |
| `call_result` (hex) | `{"type":"call_result","hex":"48656c6c6f"}` | 内存读取结果 |
| `error` | `{"type":"error","error":"..."}` | 错误消息 |

### 后端：C++ → Node.js（一次性 JSON）

**请求**（stdin，完整 JSON）：
```json
{
  "wasm_path": "plugins/random_reply_plugin.wasm",
  "action": "generate_reply",
  "seed": 42
}
```

**响应**（stdout，完整 JSON）：
```json
{"success": true, "result": "WASM plugins are awesome!"}
```

## DTK 宿主函数

WASM UI 插件从 `env` 模块导入以下函数（C++ `extern "C"` 声明，Emscripten 编译为 import）：

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_create_label` | `(ptr, len) → id` | 创建 DLabel（字符串指针, 字符串长度）→ 控件 ID |
| `dtk_create_textedit` | `(readonly, _) → id` | 创建 DTextEdit（是否只读, 未使用）→ 控件 ID |
| `dtk_create_lineedit` | `(ptr, len) → id` | 创建 DLineEdit（占位文本指针, 长度）→ 控件 ID |
| `dtk_create_pushbutton` | `(ptr, len) → id` | 创建 DSuggestButton（按钮文本指针, 长度）→ 控件 ID |
| `dtk_create_hbox` | `(flag, _) → id` | 创建布局（0=主垂直布局, 1=水平布局）→ 布局 ID |
| `dtk_add_to_layout` | `(layoutId, widgetId) → 0` | 将控件/子布局添加到布局 |
| `dtk_set_window_title` | `(ptr, len) → 0` | 设置窗口标题 |
| `dtk_set_window_size` | `(width, height) → 0` | 设置窗口尺寸 |
| `dtk_get_theme` | `() → theme` | 获取当前主题（0=浅色, 1=深色） |
| `dtk_set_theme` | `(theme, _) → 0` | 设置主题 |
| `dtk_append_message` | `(ptr, len) → 0` | 追加消息到显示区 |
| `dtk_get_input_text` | `(ptr, maxlen) → len` | 读取输入框文本，宿主通过 result-str 返回 |
| `dtk_clear_input` | `() → 0` | 清空输入框 |
| `dtk_set_label_text` | `(labelId, ptr, len) → 0` | 设置标签文本（3 参数） |
| `dtk_set_widget_enabled` | `(widgetId, enabled) → 0` | 启用/禁用控件 |
| `dtk_send_to_server` | `(ptr, len) → 0` | 发送数据到服务器（TCP） |

> `ptr`/`len` 参数指向 WASM 线性内存中的 UTF-8 字符串。
> JS 端在发送 import 请求前预读这些字符串，通过 IPC 的 `str` 字段传递给 C++，
> C++ 直接使用 `str` 而不需要回读 WASM 内存。

## 运行流程

### 启动流程

```
1. 服务器启动
   server_main.cpp → ChatServer::start(12345)
     → WasmPluginLoader 加载 random_reply_plugin.wasm（检查文件存在）
     → QTcpServer::listen(12345)

2. 客户端启动
   dtk_client_main.cpp → DtkChatClient 构造函数
     → WasmProcessBridge::load("plugins/chat_ui_plugin.wasm")
         → QProcess 启动 node wasm_dtk_host.js
         → Node 主线程创建 SharedArrayBuffer + Worker 线程
         → Worker: WebAssembly.instantiate(wasm, {env: dtkFuncs})
         → Worker: postMessage({type:'loaded'})
         → 主线程: stderr 输出就绪信号
         → C++ 等待 stderr 就绪信号
     → callExport("build_ui")
         → 16 次 import 往返（创建控件 + 初始禁用输入）
         → 每次往返: Worker → SAB → 主线程 → stdout → C++ → stdin → 主线程 → SAB → Worker
         → C++ 在自己进程内 new DLabel/DTextEdit/... 并加入布局（纯展示）
         → WASM 保存全部控件 ID，调用 dtk_set_widget_enabled 禁用输入
         → 返回 call_result
     → applyThemeColors()
     → QTimer 500ms 后 TCP 连接服务器

3. TCP 连接成功
   C++ onConnected() → callExport("on_connected")
     → WASM on_connected():
         ├─ dtk_set_label_text(statusLabel, "Connected to server...")  → C++ 更新标签
         ├─ dtk_set_widget_enabled(inputField, 1)                      → C++ 启用输入框
         ├─ dtk_set_widget_enabled(sendButton, 1)                      → C++ 启用按钮
         └─ dtk_append_message("[System] Connected to chat server!")   → C++ 显示消息
```

### 消息发送流程（全部逻辑在 WASM 内闭环）

```
用户输入文本 → 点击 Send / 按回车
  │
  └─ C++ onSendClicked() → callExport("on_send_clicked")  ← 仅转发，无逻辑
       │
       └─ WASM on_send_clicked():
            ├─ dtk_get_input_text(ptr, 4096)
            │    → C++ 读取输入框文本，通过 result_str 返回
            │    → Worker 将文本写入 WASM 内存 ptr
            │    → 返回文本长度
            ├─ 格式化: "[Me] " + 消息  （在 WASM 内存中拼接）
            ├─ dtk_append_message(msgPtr, totalLen)
            │    → C++ 在消息显示区追加 "[Me] Hello..."
            ├─ dtk_send_to_server(origPtr, inputLen)
            │    → C++ 通过 TCP 发送 {"text":"Hello..."} 到服务器
            └─ dtk_clear_input()
                 → C++ 清空输入框

       服务器收到
         ├─ 生成 seed = timestamp ⊕ counter × 2654435761 ⊕ msg_length
         ├─ WasmPluginLoader::invoke("generate_reply", {seed})
         │    → QProcess 启动 node wasm_runner.js
         │    → Node 实例化 random_reply_plugin.wasm
         │    → 调用 generate_reply(seed) → 返回回复指针
         │    → 读取回复字符串 → {"success":true,"result":"..."}
         └─ TCP 返回 {"text":"回复","echo":"原文","seed":种子}
              │
              └─ C++ onReadyRead() 收到
                 ├─ 解析 JSON，提取 "text" 回复
                 ├─ m_wasm->alloc(len) + writeString(ptr, reply)  ← 写入 WASM 内存
                 └─ callExport_ii("on_server_message", ptr, len)  ← 仅转发，无逻辑
                      │
                      └─ WASM on_server_message(ptr, len):
                           ├─ 格式化: "[Server] " + 消息  （在 WASM 内存中拼接）
                           └─ dtk_append_message(msgPtr, totalLen)
                                → C++ 在消息显示区追加 "[Server] ..."
```

### 连接断开流程

```
TCP 连接断开
  │
  └─ C++ onDisconnected() → callExport("on_disconnected")  ← 仅转发，无逻辑
       │
       └─ WASM on_disconnected():
            ├─ dtk_set_label_text(statusLabel, "Disconnected from server")  → C++ 更新标签
            ├─ dtk_set_widget_enabled(sendButton, 0)                        → C++ 禁用按钮
            └─ dtk_append_message("[System] Disconnected. Reconnecting...") → C++ 显示消息

  C++ QTimer 3s 后自动重连 → onConnected → callExport("on_connected") → WASM 处理
```

### 主题切换流程

```
右键菜单 → "Switch to Dark/Light Theme"
  │
  └─ toggleTheme() → callExport("toggle_theme")  ← 仅转发，无逻辑
       │
       └─ WASM toggle_theme():
            ├─ dtk_get_theme()          → C++ 返回当前主题
            ├─ dtk_set_theme(新值)      → C++ 切换主题
            │    └─ DGuiApplicationHelper::setPaletteType()
            │       └─ applyThemeColors()  ← 设置 DTextEdit 深色/浅色调色板+样式表
            └─ 返回旧主题值
```

## 功能特性

- **WASM 拥有全部交互与数据逻辑**：客户端仅嵌入界面展示，不承担任何逻辑。发送、接收、连接/断开均由 WASM 导出函数闭环处理
- **C++ 源码编译为 WASM**：插件源码是标准 C++，用 Emscripten 编译
- **WASM 定义 UI**：整个 UI 布局由 WASM 插件通过调用 DTK 宿主函数创建控件
- **WASM 消息处理**：消息格式化、显示、发送均在 WASM 线性内存中完成
- **WASM 后端**：服务器使用 WASM 插件生成随机回复
- **进程隔离**：WASM 在独立进程中运行，插件崩溃不影响主程序
- **DTK 依赖强制**：WASM 模块导入 16 个 DTK 宿主函数，无 DTK 则无法实例化
- **无 libc 依赖**：手写字符串/内存函数，`-nostdlib` 编译
- **右键上下文菜单**：复制、粘贴、另存为、主题切换
- **深色/浅色主题**：通过右键菜单切换，由 WASM 插件驱动
- **TCP 通信**：客户端连接服务器，发送消息，接收随机回复
- **自动重连**：断线后客户端 3 秒自动重连
- **后备 UI**：WASM 加载失败时用 C++ 直接创建相同 UI
