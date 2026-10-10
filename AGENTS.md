# AGENTS.md

本项目是 WASM 驱动的 Qt/DTK6 聊天应用。插件源码为 C++，用 Emscripten 编译为 WASM，
以进程插件形式运行在独立 Node.js 子进程中。

阅读 [README.md](README.md) 了解项目全貌；架构决策记录在 [`.agents/notes/`](.agents/notes/README.md)。

## 仓库结构

```
plugins/                WASM 插件 C++ 源码 → Emscripten 编译 → .wasm
  chat_ui_plugin.cpp      前端 UI 插件（16 个 DTK import，7 个导出 — 全部交互/数据逻辑在 WASM 内）
  random_reply_plugin.cpp 后端回复插件（零 import，2 个导出）
scripts/                WASM 运行时与编译脚本
  compile_plugins.sh      Emscripten 编译脚本
  wasm_dtk_host.js        前端 WASM 进程插件运行时（Worker + SharedArrayBuffer）
  wasm_runner.js          后端 WASM 一次性执行桥接
src/                    C++ 源代码
  dtk_client_main.cpp     DTK 客户端入口
  DtkChatClient.h         DTK 客户端核心（纯展示宿主 — 仅嵌入 UI，不承担逻辑）
  WasmProcessBridge.h     QProcess WASM 桥接（IPC 通信）
  server_main.cpp         服务器入口
  ChatServer.h            Qt6 TCP 服务器
  WasmPluginLoader.h      服务器 WASM 桥接
tests/
  test_e2e.cpp            端到端测试
.agents/notes/          架构决策记录（Agent Notes）
CMakeLists.txt          CMake 构建配置（3 个目标）
```

## 构建与运行

```bash
# 1. 激活 Emscripten
source /home/ut000520@uos/Desktop/emsdk/emsdk_env.sh

# 2. 编译 WASM 插件
bash scripts/compile_plugins.sh

# 3. 编译 C++ 目标
mkdir -p build && cd build && cmake .. && make -j$(nproc) && cd ..

# 4. 启动服务器（从项目根目录运行）
./build/wasmchatserver 12345 &

# 5. 启动客户端
DISPLAY=:0 QT_QPA_PLATFORM=xcb ./build/dtkchatclient

# 6. 端到端测试
./build/test_e2e 127.0.0.1 12345
```

> **重要**：所有程序必须从**项目根目录**运行——程序内部使用相对路径查找 `plugins/*.wasm` 和 `scripts/*.js`。

## 关键约束

- **WASM 插件拥有全部交互与数据逻辑**——客户端仅嵌入界面展示，不承担任何逻辑。发送消息、接收消息、连接/断开状态更新均由 WASM 导出函数驱动。参见 [WASM 全逻辑决策](.agents/notes/implemented/architecture/2026-10-10-wasm-owns-all-logic.md)。
- **插件源码是 C++ 代码**，用 Emscripten 编译为 WASM。不使用手写 WAT/汇编。
- **WASM 插件以进程插件形式运行**——独立 Node.js 子进程，崩溃不影响主程序。参见 [进程隔离决策](.agents/notes/implemented/architecture/2026-10-10-process-plugin-crash-isolation.md)。
- **前端 WASM 插件依赖 DTK**——16 个 `extern "C"` 函数编译为 WASM import，无 DTK 宿主则无法实例化。参见 [DTK 依赖强制决策](.agents/notes/implemented/architecture/2026-10-10-dtk-dependency-enforcement.md)。
- **无 libc 依赖**——`-nostdlib` 编译，手写 `my_strlen`/`my_memcpy` 用 `__attribute__((optnone))` 防止编译器优化为 libc 调用。参见 [无 libc 依赖决策](.agents/notes/implemented/simplification/2026-10-10-no-libc-dependency.md)。
- **同步 import 桥接**——Worker 线程 + SharedArrayBuffer + Atomics.wait/notify。参见 [同步桥接决策](.agents/notes/implemented/architecture/2026-10-10-shared-array-buffer-sync-bridge.md)。
- **UI 控件均为 DTK 控件**——DMainWindow、DLabel、DTextEdit、DLineEdit、DSuggestButton、DMenu。
- **右键上下文菜单**：复制、粘贴、另存为、主题切换。
- **深色/浅色主题**——通过 WASM `toggle_theme` 导出函数驱动。

## 修改代码时的规则

- **新增 DTK 宿主函数**需要同步更新三处：
  1. `plugins/chat_ui_plugin.cpp` — `extern "C"` 声明
  2. `scripts/wasm_dtk_host.js` — `dtkImportNames` 数组 + STR_FUNCS（如有字符串参数）
  3. `src/DtkChatClient.h` — `handleDtkHostCall()` 实现
- **修改插件导出函数**需要同步更新 `scripts/compile_plugins.sh` 的 `-s EXPORTED_FUNCTIONS`。
- **路径变更**（移动文件、重命名）时，在同一变更中更新 [`.agents/notes/`](.agents/notes/README.md) 中引用该路径的已实现 Agent Note。
- **`.wasm` 文件是编译产物**，已被 `.gitignore` 忽略，不提交到仓库。仅提交 `.cpp` 源码。

## Agent Notes

架构决策记录在 `.agents/notes/` 目录中。每个 Agent Note 记录一个决策的 Problem、Decision、
Alternatives considered 和 Consequences。

- 查看已有决策：浏览 `.agents/notes/implemented/` 下的 class 子目录
- 提出新决策：在 `.agents/notes/proposed/{class}/` 下创建文件
- 决策格式规范：参见 [`.agents/notes/README.md`](.agents/notes/README.md)
- **每个新 Agent Note 触发取代检查**：搜索活跃目录树中覆盖相同决策的旧 note

新增 Agent Note 仅当代码、测试和现有文档无法解释的持久决策理由。机械或本地编辑豁免。
