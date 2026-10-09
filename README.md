# WASM 驱动的聊天应用（DTK）

基于 Qt/DTK6 的聊天应用，前端 UI 和后端回复均由 WASM 插件驱动。
WASM 插件以**进程插件**形式运行——WASM 在独立的 Node.js 子进程中执行，
通过 JSON IPC 与 DTK 宿主通信。

## 架构总览

```
┌─────────────────────────────────────────────────────────┐
│                     DTK 客户端进程                        │
│                                                          │
│  ┌─────────────────────────────────────────────────┐    │
│  │     WASM UI 插件 (chat_ui_dtk.wasm)              │    │
│  │     导入 11 个 DTK 宿主函数                        │    │
│  │     没有 DTK 宿主，插件无法实例化！                 │    │
│  └──────────────────────┬──────────────────────────┘    │
│                         │ JSON IPC (stdin/stdout)        │
│  ┌──────────────────────┴──────────────────────────┐    │
│  │  WasmProcessBridge (QProcess)                    │    │
│  │  → 启动 node wasm_dtk_host.js 子进程              │    │
│  │  ← 接收 import 请求，调用 DTK 宿主回调             │    │
│  │  → 返回 import_result                            │    │
│  │  DTK 宿主创建: DLabel, DTextEdit,                │    │
│  │  DLineEdit, DSuggestButton, DMenu                │    │
│  └─────────────────────────────────────────────────┘    │
│                          ↓ TCP                           │
└──────────────────────────┬──────────────────────────────┘
                           │
                           │ TCP (127.0.0.1:12345)
                           ↓
┌─────────────────────────────────────────────────────────┐
│                      服务器进程                          │
│  ┌─────────────────────────────────────────────────┐    │
│  │  后端 WASM 插件 (random_reply.wasm)              │    │
│  │  10 条预设回复，基于种子选择                       │    │
│  │  通过 Node.js 桥接执行 (wasm_runner.js)          │    │
│  └─────────────────────────────────────────────────┘    │
│  Qt6 TCP 服务器 (ChatServer)                             │
└─────────────────────────────────────────────────────────┘
```

### 进程插件架构（前端）

前端 WASM 插件通过 **Worker 线程 + SharedArrayBuffer** 实现同步导入调用：

```
DTK 客户端进程                     Node.js 子进程 (wasm_dtk_host.js)
┌──────────────────┐             ┌─────────────────────────────────┐
│ DtkChatClient    │  stdin →    │ 主线程                          │
│  WasmProcessBridge│ ←stdout    │  readline 处理 stdin/stdout     │
│                  │             │  轮询 SharedArrayBuffer          │
│  handleDtkHostCall│ ←import    │  ↕ postMessage                  │
│  → new DLabel(str)│ result→    │ Worker 线程                     │
│                  │             │  WebAssembly.instantiate        │
│                  │             │  chat_ui_dtk.wasm               │
│                  │             │  import 函数 → Atomics.wait      │
└──────────────────┘             └─────────────────────────────────┘
```

WASM 导入函数在 Worker 线程中执行，需要 C++ 宿主的返回值。
Worker 通过 `SharedArrayBuffer + Atomics.wait` 阻塞等待主线程，
主线程轮询检测 import 请求，通过 stdout JSON 转发给 C++，
收到 `import_result` 后写入 SharedArrayBuffer 并 `Atomics.notify` 唤醒 Worker。

带字符串参数的导入函数（`create_label`、`create_lineedit`、`create_pushbutton`、
`set_window_title`、`append_message`）在 Worker 端预读 WASM 线性内存中的字符串，
附带在 import 请求中发给 C++，避免 C++ 回读 WASM 内存导致死锁。

## 关键设计决策

### WASM 插件依赖 DTK
前端 WASM 插件（`chat_ui_dtk.wasm`）声明了 11 条 `(import "dtk" ...)` 语句。
没有 DTK 宿主提供这些导入函数，WASM 模块**无法实例化**。
这是强制 DTK 依赖的机制——插件在结构上没有 DTK 就无法运行。

### 进程插件（非内嵌运行时）
WASM 运行时**不在 DTK 进程内**——而是作为独立的 Node.js 子进程运行。
这样 WASM 插件与宿主进程隔离，通过 JSON IPC 通信。
服务器也不内嵌 WASM 运行时，同样使用 Node.js 桥接。

### DTK 控件
所有 UI 控件均为 DTK 控件：
- `DMainWindow` — 主窗口
- `DLabel` — 标题和状态标签
- `DTextEdit` — 消息显示区（只读，深色主题）
- `DLineEdit` — 消息输入框
- `DSuggestButton` — 发送按钮
- `DMenu` — 右键上下文菜单

### 深色/浅色主题
消息显示区使用显式的深色调色板（`QColor(30, 30, 30)` 背景，`QColor(220, 220, 220)` 文字）。
主题切换通过 WASM 插件的 `toggle_theme` 导出函数实现，
该函数调用 `dtk_get_theme`/`dtk_set_theme` 宿主导入完成切换。

## 功能特性

- **WASM 定义 UI**：整个 UI 布局由 WASM 插件通过调用 DTK 宿主函数创建控件
- **WASM 消息格式化**：消息在显示前先分配到 WASM 线性内存中
- **WASM 后端**：服务器使用 WASM 插件生成随机回复
- **右键上下文菜单**：复制、粘贴、另存为、主题切换
- **深色/浅色主题**：通过右键菜单切换，由 WASM 插件驱动
- **TCP 通信**：客户端连接服务器，发送消息，接收随机回复
- **自动重连**：断线后客户端自动重连

## 文件结构

```
wasm/
├── CMakeLists.txt              # CMake 构建配置
├── README.md                   # 本文件
├── package.json                # Node.js 依赖声明（wabt 用于 WAT→WASM 编译）
├── package-lock.json           # npm 依赖锁定文件
│
├── plugins/                    # WASM 插件源码与编译产物
│   ├── chat_ui_dtk.wat         # 前端 WASM 插件（WAT 源码）
│   ├── chat_ui_dtk.wasm        # 前端 WASM 插件（编译后二进制）
│   ├── random_reply.wat        # 后端 WASM 插件（WAT 源码）
│   └── random_reply.wasm       # 后端 WASM 插件（编译后二进制）
│
├── scripts/                    # 构建与运行脚本
│   ├── build_plugins.js        # 使用 wabt 编译所有 .wat → .wasm
│   ├── wasm_dtk_host.js        # 前端 WASM 进程插件运行时（Worker + SAB）
│   └── wasm_runner.js          # 服务器端 WASM 执行的 Node.js 桥接
│
├── src/                        # C++ 源代码
│   ├── dtk_client_main.cpp     # DTK 客户端入口（DApplication）
│   ├── DtkChatClient.h         # DTK 客户端核心：WasmProcessBridge + DTK 宿主导入 + UI 逻辑
│   ├── WasmProcessBridge.h     # QProcess 包装：管理 wasm_dtk_host.js，JSON IPC，DTK 宿主回调分发
│   ├── server_main.cpp         # 服务器入口
│   ├── ChatServer.h            # Qt6 TCP 服务器 + WASM 后端插件（仅头文件）
│   ├── ChatServer.cpp          # ChatServer 桩文件（类定义在头文件中）
│   ├── WasmPluginLoader.h      # 基于 QProcess 的 WASM 桥接（服务器用，仅头文件）
│   └── WasmPluginLoader.cpp    # WasmPluginLoader 桩文件（类定义在头文件中）
│
└── tests/
    └── test_e2e.cpp            # 端到端测试：发送 5 条消息 → 验证回复
```

### 各文件说明

#### 构建与配置

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | CMake 构建配置。定义 3 个构建目标：`dtkchatclient`（DTK6 + WasmProcessBridge）、`wasmchatserver`（Qt6 Network）、`test_e2e`。 |
| `package.json` | 声明 `wabt` 依赖用于 WAT→WASM 编译。提供 `npm run build:plugins` 脚本。 |

#### WASM 插件（`plugins/`）

| 文件 | 说明 |
|------|------|
| `chat_ui_dtk.wat` | **前端 WASM 插件（WAT 源码）。** 声明 11 条 `(import "dtk" ...)` 语句，从结构上依赖 DTK。导出 4 个函数：`build_ui()`、`alloc(size)`、`format_and_display(ptr, len)`、`toggle_theme()`。 |
| `random_reply.wat` | **后端 WASM 插件（WAT 源码）。** 自包含（无导入）。导出 `generate_reply(seed)` 函数，基于 `seed % 10` 从 10 条预设回复中选择一条返回。 |

#### 脚本（`scripts/`）

| 文件 | 说明 |
|------|------|
| `build_plugins.js` | 使用 `wabt` npm 包将所有 `plugins/*.wat` 编译为 `*.wasm`。 |
| `wasm_dtk_host.js` | **前端 WASM 进程插件运行时。** 主线程 + Worker 线程架构。Worker 线程加载 `chat_ui_dtk.wasm` 并同步执行导出函数；WASM 导入函数通过 `SharedArrayBuffer + Atomics.wait/notify` 与主线程同步通信；主线程通过 `readline` 处理 stdin/stdout JSON 行协议，转发 import 请求到 C++ 父进程。 |
| `wasm_runner.js` | 服务器端 WASM 执行的 Node.js 桥接脚本。由 `WasmPluginLoader` 通过 `QProcess` 调用。 |

#### 客户端源码（`src/`）

| 文件 | 说明 |
|------|------|
| `dtk_client_main.cpp` | DTK 客户端入口。创建 `DApplication`，配置 DDE 平台集成，实例化 `DtkChatClient`，显示窗口，进入事件循环。 |
| `DtkChatClient.h` | **DTK 客户端核心类（仅头文件）。** 通过 `WasmProcessBridge` 加载 `chat_ui_dtk.wasm`。`handleDtkHostCall(func, a1, a2, str)` 处理 11 个 DTK 导入命令：创建控件/布局、设置窗口属性、获取/设置主题、追加消息。`str` 参数由 JS 端预读 WASM 内存后通过 IPC 传递，避免 C++ 回读内存导致死锁。包含右键上下文菜单（复制/粘贴/另存为/主题切换）、深色/浅色主题、TCP 通信与自动重连。 |
| `WasmProcessBridge.h` | **QProcess 包装类（仅头文件）。** 管理 `wasm_dtk_host.js` 子进程。发送 JSON 行到 stdin，读取 JSON 行从 stdout。收到 `import` 消息时调用 `s_hostCallback`（DTK 宿主回调），将返回值作为 `import_result` 发回。API：`load()`、`callExport()`、`callExport_i32()`、`callExport_ii()`、`readMemory()`、`writeMemory()`、`readString()`、`writeString()`、`alloc()`。 |

#### 服务器源码（`src/`）

| 文件 | 说明 |
|------|------|
| `server_main.cpp` | 服务器入口。从 argv 解析端口号（默认 12345），实例化 `ChatServer`，进入事件循环。 |
| `ChatServer.h` | **Qt6 TCP 服务器（仅头文件）。** 监听端口，接受连接，读取换行分隔的 JSON 消息。收到 `{"text":"..."}` 后，通过 `WasmPluginLoader` 加载 `random_reply.wasm`，以种子生成随机回复。 |
| `WasmPluginLoader.h` | **基于 QProcess 的 WASM 桥接（仅头文件）。** 启动 `node wasm_runner.js` 子进程，通过 stdin/stdout JSON 通信执行 `random_reply.wasm`。 |

#### 测试（`tests/`）

| 文件 | 说明 |
|------|------|
| `test_e2e.cpp` | 端到端测试。连接服务器，发送 5 条消息，验证每条消息都收到包含回显 + 随机回复 + 种子的响应。 |

## 构建

### 前置依赖
- Qt 6.8.0
- DTK6 6.7.49（`libdtk6core-dev`、`libdtk6widget-dev`、`libdtk6gui-dev`）
- CMake 3.16+
- Node.js（用于 WAT→WASM 编译和 WASM 进程插件运行时）

### 构建步骤

```bash
# 1. 编译 WASM 插件（WAT → WASM）
npm install
node scripts/build_plugins.js

# 2. 编译所有 C++ 目标
cd build && cmake .. && make -j$(nproc)
```

### 构建目标
- `dtkchatclient` — DTK 客户端，通过 WasmProcessBridge 运行 WASM 进程插件
- `wasmchatserver` — TCP 服务器，使用 WASM 后端插件
- `test_e2e` — 端到端测试

## 运行

```bash
# 启动服务器
./build/wasmchatserver 12345 &

# 运行 DTK 客户端
DISPLAY=:0 QT_QPA_PLATFORM=xcb ./build/dtkchatclient

# 运行端到端测试
./build/test_e2e 127.0.0.1 12345
```

> **注意**：在 DDE 上需使用 `QT_QPA_PLATFORM=xcb` 以正确渲染窗口。

## WASM 插件

| 插件 | 文件 | 用途 |
|------|------|------|
| 聊天 UI（DTK） | `plugins/chat_ui_dtk.wat` | 通过调用 DTK 宿主导入函数定义 UI |
| 随机回复 | `plugins/random_reply.wat` | 服务器端随机回复生成 |

## DTK 宿主函数

WASM UI 插件从 "dtk" 模块导入以下函数：

| 函数 | 签名 | 说明 |
|------|------|------|
| `dtk_create_label` | `(i32, i32) → i32` | 创建 DLabel（字符串指针, 字符串长度）→ 控件 ID |
| `dtk_create_textedit` | `(i32, i32) → i32` | 创建 DTextEdit（是否只读, 未使用）→ 控件 ID |
| `dtk_create_lineedit` | `(i32, i32) → i32` | 创建 DLineEdit（字符串指针, 字符串长度）→ 控件 ID |
| `dtk_create_pushbutton` | `(i32, i32) → i32` | 创建 DSuggestButton（字符串指针, 字符串长度）→ 控件 ID |
| `dtk_create_hbox` | `(i32, i32) → i32` | 创建 QHBoxLayout（标志位, 未使用）→ 布局 ID |
| `dtk_add_to_layout` | `(i32, i32) → i32` | 将控件添加到布局（布局 ID, 控件 ID） |
| `dtk_set_window_title` | `(i32, i32) → i32` | 设置窗口标题（字符串指针, 字符串长度） |
| `dtk_set_window_size` | `(i32, i32) → i32` | 设置窗口尺寸（宽度, 高度） |
| `dtk_get_theme` | `() → i32` | 获取当前主题（0=浅色, 1=深色） |
| `dtk_set_theme` | `(i32, i32) → i32` | 设置主题（主题值, 未使用） |
| `dtk_append_message` | `(i32, i32) → i32` | 追加消息到显示区（字符串指针, 字符串长度） |

## 右键上下文菜单

在窗口任意位置右键可访问：
- **复制** — 从消息区或输入框复制选中的文本
- **粘贴** — 将剪贴板内容粘贴到输入框
- **另存为...** — 将聊天记录保存到文本文件
- **主题切换** — 在浅色和深色主题之间切换

## IPC 协议

### C++ → Node.js（stdin）
```json
{"type":"call","func":"build_ui","args":[]}
{"type":"import_result","value":1}
{"type":"read_memory","offset":512,"len":30}
{"type":"write_memory","offset":ptr,"hex":"..."}
```

### Node.js → C++（stdout）
```json
{"type":"import","func":"dtk_create_label","args":[512,23],"str":"WASM-Powered Chat (DTK)"}
{"type":"call_result","value":0}
{"type":"call_result","hex":"48656c6c6f"}
```
