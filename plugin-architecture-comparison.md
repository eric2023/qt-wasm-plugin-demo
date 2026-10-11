# WASM 进程插件 vs Qt 进程插件 vs 动态库插件：方案对比

Status: proposed

## 背景

当前项目使用 WASM 进程插件架构实现文本编辑器。在实际使用中发现两个问题：

1. **已有 Qt/DTK 模块无法复用** — WASM 运行在独立 Node.js 子进程中，没有 Qt 运行时，
   已有的 `MyWidget : public DWidget` 代码无法直接编译运行。所有 UI 必须通过 IPC 遥控宿主创建。
2. **复杂 UI 实现困难** — 每个控件创建都是一次跨进程 IPC 往返（50 个控件 = 100+ 次往返），
   且 WASM 中用 `dtk_create_label(ptr, len)` 描述 UI 极其笨拙。
3. **CPU 占用过高** — `wasm_dtk_host.js` 中 `pollImport()` 使用 `setImmediate(pollImport)` 忙轮询，
   即使无事件也持续调度，导致一个 CPU 核心 100% 占用。

本文档对比三种插件架构方案，供未来架构决策参考。

## 三种方案

### 方案一：WASM 进程插件（当前实现）

```
┌── 主进程 (C++/DTK) ──────────────┐     ┌── Node.js 子进程 ──────────────┐
│  DtkTextEditor                    │     │  wasm_dtk_host.js               │
│    callExport("build_ui")         │────►│  Worker: WASM 实例              │
│    handleDtkHostCall()            │◄────│    dtk_create_label(ptr,len)    │
│      → new DLabel(str, this)      │     │    dtk_subscribe_event(...)     │
│      → 返回 widget_id             │     │    on_event(eventId, widgetId)  │
│    callExport_ii("on_event",...)  │────►│                                 │
└───────────────────────────────────┘     └─────────────────────────────────┘
         JSON IPC (stdin/stdout)
```

WASM 插件通过 IPC 告诉宿主"创建什么控件"，宿主在主进程中创建真实的 Qt 对象。
事件驱动架构：WASM 通过 `dtk_subscribe_event` 订阅事件，宿主通过 `on_event` 通用入口转发。

### 方案二：Qt 进程插件 + X11 窗口嵌入

```
┌── 宿主进程 (C++/DTK) ────────────┐     ┌── 插件进程 (独立 Qt 应用) ──────┐
│  DtkMainWindow                    │     │  MyEditorWidget : public DWidget │
│    QSplitter / QTabWidget         │     │    new DLabel("Title", this)     │
│    createWindowContainer(window)  │◄────│    new DTextEdit(this)           │
│      ↑ QWindow::fromWinId(wid)    │     │    winId() → stdin 发给宿主      │
│    QProcess 管理插件进程          │     │  已有 Qt/DTK 代码直接编译运行    │
│    进程退出 → 重启 → 重新嵌入     │     │  崩溃 → 进程退出                 │
└───────────────────────────────────┘     └─────────────────────────────────┘
         X11 窗口嵌入 (reparent) + stdin/stdout IPC (仅传 winId 和生命周期事件)
```

插件进程是标准 Qt 应用，直接创建控件。宿主通过 X11 `winId` 嵌入插件窗口。

### 方案三：动态库插件（dlopen / QPluginLoader）

```
┌── 主进程 (C++/DTK) ──────────────────────────────────────┐
│  QPluginLoader::instance() → MyPluginInterface*           │
│    plugin->createWidget(parent)  ← 直接在主进程内创建控件  │
│    new DLabel("Title", parent)   ← 控件在主进程堆上        │
│    信号槽直接连接                  ← 同进程，无 IPC         │
│                                                            │
│  ⚠️ 插件 .so 崩溃 = 主进程崩溃                             │
└────────────────────────────────────────────────────────────┘
```

插件编译为 `.so` 共享库，主进程通过 `QPluginLoader` 或 `dlopen` 加载。
插件在主进程内运行，直接创建控件对象。

## 详细对比

### 核心能力

| 维度 | WASM 进程插件 | Qt 进程插件 | 动态库插件 |
|------|:---:|:---:|:---:|
| 崩溃隔离 | ✅ 独立进程 | ✅ 独立进程 | ❌ 插件崩溃=主进程崩溃 |
| 已有 Qt 代码复用 | ❌ 不可能 | ✅ 直接编译运行 | ✅ 直接编译运行 |
| 复杂 UI 支持 | ❌ 每控件一次 IPC 往返 | ✅ 原生 Qt 控件 | ✅ 原生 Qt 控件 |
| 宿主零修改（换插件） | ✅ 事件驱动 | ✅ 仅加载+嵌入 | ✅ 接口约定 |
| 安全沙箱 | ✅ WASM 沙箱 | ⚠️ 完整系统权限 | ❌ 完整主进程权限 |

### 开发体验

| 维度 | WASM 进程插件 | Qt 进程插件 | 动态库插件 |
|------|:---:|:---:|:---:|
| 插件开发门槛 | ❌ 高（-nostdlib，手写 strlen） | ✅ 低（标准 Qt 开发） | ✅ 最低（标准 Qt 插件） |
| 可用库 | ❌ 无 libc，无 STL，无 Qt | ✅ Qt/DTK + 全部 C++ 生态 | ✅ Qt/DTK + 全部 C++ 生态 |
| 调试体验 | ❌ 困难（WASM 无调试器） | ✅ 正常（GDB 调试子进程） | ✅ 正常（GDB 调试主进程） |
| 编译工具链 | ❌ Emscripten + cmake | ✅ 标准 cmake/qmake | ✅ 标准 cmake/qmake |

### 性能

| 维度 | WASM 进程插件 | Qt 进程插件 | 动态库插件 |
|------|:---:|:---:|:---:|
| 控件创建 | ❌ 每控件一次跨进程 IPC | ✅ 子进程本地创建 | ✅ 主进程本地创建 |
| 事件传递 | ❌ 跨进程 IPC（SAB+Atomics） | ✅ 子进程本地处理 | ✅ 主进程本地处理 |
| 空闲 CPU 占用 | ❌ 高（setImmediate 忙轮询） | ✅ 低（事件驱动） | ✅ 低（事件驱动） |
| 内存开销 | ❌ 高（Node.js + WASM + SAB 131KB） | ⚠️ 中（每个插件进程独立 Qt 实例） | ✅ 低（共享主进程 Qt 实例） |
| 启动速度 | ❌ 慢（Node.js 启动 + WASM 实例化） | ⚠️ 中（Qt 应用启动） | ✅ 快（dlopen 加载） |

### 部署与兼容性

| 维度 | WASM 进程插件 | Qt 进程插件 | 动态库插件 |
|------|:---:|:---:|:---:|
| X11 兼容 | ✅ | ✅ `fromWinId` + `createWindowContainer` | ✅ |
| Wayland 兼容 | ✅ | ❌ 不支持跨进程窗口嵌入 | ✅ |
| Qt 版本一致性要求 | ✅ 无（WASM 独立） | ⚠️ 需相同 Qt 版本 | ❌ 必须完全一致 |
| 跨平台确定性 | ✅ WASM 字节码跨平台 | ⚠️ 需为每个平台编译 | ❌ 需为每个平台编译 |
| 二进制大小 | ❌ Node.js 运行时 + Emscripten | ✅ 标准 Qt 应用 | ✅ 最小（仅 .so） |

### 宿主代码复杂度

| 维度 | WASM 进程插件 | Qt 进程插件 | 动态库插件 |
|------|:---:|:---:|:---:|
| 宿主需要控件工厂代码 | ✅ 需要（create_label/textedit/...） | ❌ 不需要 | ❌ 不需要 |
| 宿主需要事件映射表 | ✅ 需要（subscribe_event） | ❌ 不需要 | ❌ 不需要 |
| 宿主需要文件 I/O 代理 | ✅ 需要（read_file/write_file） | ❌ 不需要（插件进程直接访问） | ❌ 不需要 |
| 宿主 IPC 协议复杂度 | ❌ 高（import/result/SAB/Atomics） | ✅ 低（仅 winId + 生命周期） | ✅ 无（函数调用） |
| 宿主崩溃恢复代码 | ✅ 需要（清理控件+重加载） | ✅ 需要（重启进程+重嵌入） | ❌ 不可能（已崩溃） |

## 当前 WASM 方案的具体问题

### 问题一：已有 Qt/DTK 代码无法复用

WASM 运行在独立 Node.js 子进程中，没有 Qt 运行时（MOC、信号槽、QObject 对象树）。
已有的 `MyWidget : public DWidget` 代码中的 `new DLabel("Title", this)` 在 WASM 中无法编译运行。
所有 UI 必须重写为 IPC 调用序列：`dtk_create_label(ptr, len)` → `dtk_add_to_layout(layout, widget)` → ...

### 问题二：复杂 UI 实现困难

当前 WASM 插件只能使用 hbox/vbox 两种布局。50 个控件的界面需要 100+ 次跨进程 IPC 往返
（每次往返 = Worker→主线程 SAB notify → C++ 处理 → C++→主线程 stdin → 主线程→Worker SAB notify）。
无法使用 QGridLayout、QStackedWidget、QScrollArea 等复杂 Qt 布局。

### 问题三：CPU 占用过高

`wasm_dtk_host.js` 第 274-289 行 `pollImport()` 使用 `setImmediate(pollImport)` 忙轮询：

```javascript
function pollImport() {
    if (Atomics.load(i32, TAG_OFFSET / 4) === 1) {
        // 处理 import 请求
        return;
    }
    setImmediate(pollImport);  // ← 无事件时也立即重新调度，100% CPU
}
```

`setImmediate` 在事件循环空闲时立即回调，形成忙轮询。即使无任何事件，
Node.js 主线程也持续运行 `pollImport`，导致一个 CPU 核心 100% 占用。

**修复方案**（不改变架构）：用 `Atomics.wait` 替代 `setImmediate` 轮询。
让主线程在 SAB 的 TAG_OFFSET 上阻塞等待 Worker 的 `Atomics.notify`：

```javascript
function pollImport() {
    // 阻塞等待 TAG 变为 1（Worker 的 Atomics.notify 唤醒）
    Atomics.wait(i32, TAG_OFFSET / 4, 0, 1000);  // 超时 1s 防永久阻塞
    if (Atomics.load(i32, TAG_OFFSET / 4) === 1) {
        // 处理 import 请求
        return;
    }
    setImmediate(pollImport);  // 超时后重新调度
}
```

但这有死锁风险：主线程在 `Atomics.wait` 中阻塞时，无法处理 C++ 父进程通过 stdin 发来的消息。
需要将 stdin 处理和 import 等待分离到不同机制——这增加了复杂度。

## 各方案适用场景

### WASM 进程插件适用场景

- **需要安全沙箱**：插件不可信，需限制其文件系统/网络访问
- **纯计算逻辑**：图像处理、加密解密、文本分析——不需要 UI
- **跨平台确定性**：同一 WASM 字节码在不同 OS 上运行
- **不适用**：需要复用已有 Qt 代码、需要复杂 UI、需要 Qt 信号槽

### Qt 进程插件适用场景

- **需要复用已有 Qt/DTK 模块**：插件进程就是标准 Qt 应用
- **复杂 UI**：原生 Qt 控件，任意布局
- **需要崩溃隔离**：独立进程
- **X11 环境**：UOS Desktop 25 等使用 X11 的系统
- **不适用**：Wayland 环境（不支持跨进程窗口嵌入）、需要安全沙箱

### 动态库插件适用场景

- **可信插件**：内部开发，不需要崩溃隔离
- **最高性能**：同进程直接调用
- **Wayland 兼容**：无窗口嵌入问题
- **不适用**：不可信插件、需要崩溃隔离

## 建议

### 短期（保持当前 WASM 架构）

1. **修复 CPU 占用问题**：将 `setImmediate(pollImport)` 改为 `Atomics.wait` 阻塞等待，
   或使用 `setTimeout(pollImport, 0)` 降低轮询频率（但引入延迟）。
2. **记录已知局限**：已有 Qt 代码无法复用、复杂 UI 困难、Wayland 不兼容。

### 中期（如需复用已有 Qt 模块）

迁移到 **Qt 进程插件 + X11 窗口嵌入**方案：
- 宿主仅做：启动进程 → 拿 winId → `createWindowContainer` → 嵌入布局 → 监听进程退出
- 插件进程：标准 Qt 应用，已有代码直接编译运行
- 崩溃恢复：检测进程退出 → 重启 → 重新嵌入
- 三方换插件：宿主零修改

### 长期（如需兼顾安全沙箱和代码复用）

混合方案：
- WASM 进程插件用于纯计算逻辑（安全沙箱）
- Qt 进程插件用于 UI 和交互（代码复用）
- 两者共存：WASM 作为 Qt 插件进程的子模块

## 技术细节：Qt 进程插件 + X11 窗口嵌入

### 宿主侧实现要点

```cpp
// 1. 启动插件进程
QProcess *pluginProcess = new QProcess(this);
pluginProcess->start("./plugin_executable");

// 2. 读取插件进程输出的 winId
connect(pluginProcess, &QProcess::readyReadStandardOutput, [=]() {
    QString line = pluginProcess->readLine().trimmed();
    WId wid = line.toULongLong();

    // 3. 嵌入插件窗口
    QWindow *pluginWindow = QWindow::fromWinId(wid);
    QWidget *container = QWidget::createWindowContainer(pluginWindow, this);
    layout->addWidget(container);
});

// 4. 崩溃恢复
connect(pluginProcess, QOverload<int>::of(&QProcess::finished), [=](int exitCode) {
    qDebug() << "Plugin crashed, restarting...";
    // 移除旧容器
    delete container;
    // 重启
    pluginProcess->start("./plugin_executable");
});
```

### 插件进程侧实现要点

```cpp
int main(int argc, char *argv[]) {
    DApplication app(argc, argv);

    // 已有 Qt/DTK 代码——零修改
    MyEditorWidget widget;
    widget.show();

    // 将 winId 发给宿主
    QTimer::singleShot(100, [&widget]() {
        printf("%llu\n", (unsigned long long)widget.winId());
        fflush(stdout);
    });

    return app.exec();
}
```

### 通信机制

宿主和插件进程之间可以通过多种方式通信：

| 方式 | 用途 | 复杂度 |
|------|------|--------|
| stdin/stdout JSON | 生命周期管理、winId 传递 | 低 |
| QLocalSocket / QLocalServer | 双向消息通信 | 中 |
| 共享内存 (QSharedMemory) | 大数据传输 | 中 |
| D-Bus | 进程间方法调用 | 高 |

对于简单的插件加载+嵌入场景，stdin/stdout 足够。
如果插件需要与宿主交互（如打开文件、调用宿主功能），可以使用 QLocalSocket。

### Wayland 不兼容问题

X11 支持跨进程窗口嵌入（通过 `XReparentWindow`），但 Wayland 出于安全考虑**不支持**。
如果未来需要迁移到 Wayland，方案有：

1. **使用 XWayland**：在 Wayland 下运行 X11 兼容模式，窗口嵌入仍可用
2. **使用 Wayland 的 subsurface 协议**：需要自定义协议实现，复杂
3. **回退到动态库插件**：放弃崩溃隔离，换取 Wayland 兼容
4. **使用 Qt 的 remote objects**：Qt 官方的进程间通信方案，但不是窗口嵌入

## 参考

- [当前 WASM 架构实现](../../implemented/architecture/2026-10-10-event-driven-plugin-architecture.md)
- [Qt QWindow::fromWinId 文档](https://doc.qt.io/qt-6/qwindow.html#fromWinId)
- [Qt QWidget::createWindowContainer 文档](https://doc.qt.io/qt-6/qwidget.html#createWindowContainer)
- [X11 ReparentWindow 协议](https://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html#Reparent_Window_Events)
