# AGENTS.md

本项目是 WASM 驱动的 Qt/DTK6 文本编辑器。插件源码为 C++，用 Emscripten 编译为 WASM，
以进程插件形式运行在独立 Node.js 子进程中。采用**事件驱动架构**——宿主源码不包含任何插件业务知识。

阅读 [README.md](README.md) 了解项目全貌；架构决策记录在 [`.agents/notes/`](.agents/notes/README.md)。

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
.agents/notes/          架构决策记录（Agent Notes）
CMakeLists.txt          CMake 构建配置（2 个目标：dtktexteditor + test_editor）
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

## 关键约束

- **事件驱动架构**——宿主源码不包含任何插件业务知识。WASM 插件通过 `dtk_subscribe_event` 订阅控件事件，
  宿主通过 `callExport_ii("on_event", eventId, widgetId)` 通用入口转发事件。
  宿主不知道 `eventId` 对应什么功能。三方换插件（计算器、图片浏览器等）时，宿主源码零修改。
  参见 [事件驱动架构决策](.agents/notes/implemented/architecture/2026-10-10-event-driven-plugin-architecture.md)。
- **插件源码是 C++ 代码**，用 Emscripten 编译为 WASM。不使用手写 WAT/汇编。
- **WASM 插件以进程插件形式运行**——独立 Node.js 子进程，崩溃不影响主程序。
  参见 [进程隔离决策](.agents/notes/implemented/architecture/2026-10-10-process-plugin-crash-isolation.md)。
- **WASM 插件完整实现文本编辑器全部功能**——UI 构建、新建、打开、保存、编辑、主题切换、崩溃测试。
  C++ 客户端是纯展示宿主，仅转发事件，不承担任何逻辑。
  参见 [WASM 拥有全部逻辑决策](.agents/notes/implemented/architecture/2026-10-10-wasm-owns-all-logic.md)。
- **前端 WASM 插件依赖 DTK**——20 个 `extern "C"` 函数编译为 WASM import，无 DTK 宿主则无法实例化。
  参见 [DTK 依赖强制决策](.agents/notes/implemented/architecture/2026-10-10-dtk-dependency-enforcement.md)。
- **无 libc 依赖**——`-nostdlib` 编译，手写 `my_strlen`/`my_memcpy` 用 `__attribute__((optnone))` 防止编译器优化为 libc 调用。
  参见 [无 libc 依赖决策](.agents/notes/implemented/simplification/2026-10-10-no-libc-dependency.md)。
- **同步 import 桥接**——Worker 线程 + SharedArrayBuffer + Atomics.wait/notify。
  参见 [同步桥接决策](.agents/notes/implemented/architecture/2026-10-10-shared-array-buffer-sync-bridge.md)。
- **UI 控件均为 DTK 控件**——DLabel、DTextEdit、DSuggestButton、DMenu、DFileDialog。
- **右键上下文菜单**：内置复制/粘贴 + WASM 插件通过 `dtk_add_context_menu_item` 动态注册的菜单项。
- **深色/浅色主题**——通过 WASM 插件在 `on_event` 中调用 `dtk_get_theme`/`dtk_set_theme` 驱动。
- **崩溃恢复**——`on_event(crashEventId, 0)` 触发 WASM `unreachable` 崩溃，C++ 检测后自动重新加载插件。

## 修改代码时的规则

- **新增 DTK 宿主函数**需要同步更新三处：
  1. `plugins/text_editor_plugin.cpp` — `extern "C"` 声明
  2. `scripts/wasm_dtk_host.js` — `dtkImportNames` 数组 + STR_FUNCS（如有字符串参数）
  3. `src/DtkTextEditor.h` — `handleDtkHostCall()` 实现
- **修改插件导出函数**需要同步更新 `scripts/compile_plugins.sh` 的 `-s EXPORTED_FUNCTIONS`。
  正常情况下导出函数固定为 3 个（`build_ui`/`on_event`/`alloc`），不应修改。
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
