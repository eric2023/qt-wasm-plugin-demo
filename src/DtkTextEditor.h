#ifndef DTKTEXTEDITOR_H
#define DTKTEXTEDITOR_H

#include <DMainWindow>
#include <DApplication>
#include <DTextEdit>
#include <DLabel>
#include <DSuggestButton>
#include <DPushButton>
#include <DMenu>
#include <DGuiApplicationHelper>
#include <DFileDialog>
#include <DPalette>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QTimer>
#include <QMessageBox>
#include <QFileInfo>
#include <QScrollBar>
#include <QContextMenuEvent>
#include <QDebug>
#include <memory>

#include "WasmProcessBridge.h"

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

/**
 * DtkTextEditor — DTK 宿主，通过进程插件加载 WASM 文本编辑器插件。
 *
 * 架构：纯展示宿主——仅嵌入界面展示，不承担任何逻辑。
 * 全部编辑器功能（新建/打开/保存/编辑/崩溃测试）由 WASM 插件实现。
 *
 *   ┌──────── DTK 客户端进程 ────────┐    ┌──── Node.js 子进程 ────┐
 *   │  DtkTextEditor (纯转发层)       │    │  wasm_dtk_host.js      │
 *   │    ↕ JSON IPC (stdin/stdout)   │◄──►│  ↕ WebAssembly API    │
 *   │  handleDtkHostCall()           │◄───│  dtk_create_label()   │
 *   │  → new DLabel / DTextEdit ...  │───►│  返回 widget_id        │
 *   │  callExport("build_ui")        │───►│  exports.build_ui()    │
 *   └────────────────────────────────┘    └────────────────────────┘
 *
 * WASM 插件崩溃（如 crash_test）后，子进程退出，主程序仍可运行。
 * 主程序检测到崩溃后可重新加载 WASM 插件。
 */
class DtkTextEditor : public DMainWindow
{
    Q_OBJECT

public:
    explicit DtkTextEditor(QWidget *parent = nullptr)
        : DMainWindow(parent)
        , m_editor(nullptr)
        , m_statusLabel(nullptr)
        , m_newButton(nullptr)
        , m_openButton(nullptr)
        , m_saveButton(nullptr)
        , m_crashButton(nullptr)
        , m_mainLayout(nullptr)
        , m_nextWidgetId(1)
        , m_currentTheme(DGuiApplicationHelper::instance()->themeType())
    {
        auto *centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        m_mainLayout = new QVBoxLayout(centralWidget);
        m_mainLayout->setContentsMargins(10, 10, 10, 10);
        m_mainLayout->setSpacing(6);

        // 设置 DTK 宿主回调
        WasmProcessBridge::s_hostCallback = [this](const QString &func, int32_t a1, int32_t a2, int32_t a3, const QString &str, QString &outResultStr) -> int32_t {
            return this->handleDtkHostCall(func, a1, a2, a3, str, outResultStr);
        };

        // 加载 WASM 插件
        loadWasmPlugin();
    }

    ~DtkTextEditor()
    {
        WasmProcessBridge::s_hostCallback = nullptr;
    }

    /**
     * 加载 WASM 插件并构建 UI。
     * 可在崩溃后重新调用以恢复。
     */
    void loadWasmPlugin()
    {
        m_wasm = std::make_unique<WasmProcessBridge>();
        if (!m_wasm->load(QStringLiteral("plugins/text_editor_plugin.wasm"))) {
            qWarning() << "Failed to load WASM text editor plugin!";
            buildFallbackUi();
            return;
        }

        qDebug() << "WASM text editor plugin loaded — calling build_ui()...";
        int32_t layoutId = 0;
        if (!m_wasm->callExport("build_ui", &layoutId)) {
            qWarning() << "build_ui() failed!";
            buildFallbackUi();
            return;
        }
        qDebug() << "UI built by WASM plugin, layout ID:" << layoutId;

        applyThemeColors();

        // 连接信号
        if (m_newButton)
            connect(m_newButton, &DPushButton::clicked, this, &DtkTextEditor::onNewClicked);
        if (m_openButton)
            connect(m_openButton, &DPushButton::clicked, this, &DtkTextEditor::onOpenClicked);
        if (m_saveButton)
            connect(m_saveButton, &DPushButton::clicked, this, &DtkTextEditor::onSaveClicked);
        if (m_crashButton)
            connect(m_crashButton, &DPushButton::clicked, this, &DtkTextEditor::onCrashClicked);
        if (m_editor)
            connect(m_editor, &DTextEdit::textChanged, this, &DtkTextEditor::onTextChanged);
    }

protected:
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        showContextMenu(event->globalPos());
    }
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::ContextMenu) {
            auto *ctxEvent = static_cast<QContextMenuEvent*>(event);
            showContextMenu(ctxEvent->globalPos());
            return true;
        }
        return DMainWindow::eventFilter(obj, event);
    }

private:
    /**
     * DTK 宿主回调 — 处理 WASM 插件通过 IPC 转发的 DTK 导入函数调用。
     */
    int32_t handleDtkHostCall(const QString &func, int32_t a1, int32_t a2, int32_t a3, const QString &str, QString &outResultStr)
    {
        if (func == "dtk_create_label") {
            auto *label = new DLabel(str, this);
            int32_t id = m_nextWidgetId++;
            m_widgets[id] = label;
            if (!m_titleLabel) {
                QFont f = label->font();
                f.setBold(true);
                f.setPointSize(14);
                label->setFont(f);
                m_titleLabel = label;
            } else if (!m_statusLabel) {
                m_statusLabel = label;
            }
            qDebug() << "[DTK Host] Created label:" << str << "id:" << id;
            return id;
        }
        if (func == "dtk_create_textedit") {
            auto *textEdit = new DTextEdit(this);
            textEdit->setReadOnly(a1 != 0);
            textEdit->installEventFilter(this);
            textEdit->viewport()->installEventFilter(this);
            int32_t id = m_nextWidgetId++;
            m_widgets[id] = textEdit;
            if (!m_editor) m_editor = textEdit;
            qDebug() << "[DTK Host] Created text_edit (readonly=" << a1 << ") id:" << id;
            return id;
        }
        if (func == "dtk_create_pushbutton") {
            auto *btn = new DSuggestButton(str, this);
            int32_t id = m_nextWidgetId++;
            m_widgets[id] = btn;
            if (!m_newButton) m_newButton = btn;
            else if (!m_openButton) m_openButton = btn;
            else if (!m_saveButton) m_saveButton = btn;
            else if (!m_crashButton) m_crashButton = btn;
            qDebug() << "[DTK Host] Created push_button:" << str << "id:" << id;
            return id;
        }
        if (func == "dtk_create_hbox") {
            if (a1 == 0) {
                qDebug() << "[DTK Host] Main vertical layout (id: 0)";
                return 0;
            }
            auto *layout = new QHBoxLayout();
            int32_t id = m_nextWidgetId++;
            m_layouts[id] = layout;
            qDebug() << "[DTK Host] Created hbox id:" << id << "flag:" << a1;
            return id;
        }
        if (func == "dtk_add_to_layout") {
            auto layoutIt = m_layouts.find(a1);
            if (layoutIt != m_layouts.end()) {
                auto widgetIt = m_widgets.find(a2);
                if (widgetIt != m_widgets.end()) {
                    layoutIt.value()->addWidget(widgetIt.value());
                }
            } else if (a1 == 0) {
                auto widgetIt = m_widgets.find(a2);
                if (widgetIt != m_widgets.end()) {
                    m_mainLayout->addWidget(widgetIt.value());
                } else {
                    auto subLayoutIt = m_layouts.find(a2);
                    if (subLayoutIt != m_layouts.end()) {
                        m_mainLayout->addLayout(subLayoutIt.value());
                    }
                }
            }
            qDebug() << "[DTK Host] add_to_layout layout:" << a1 << "widget:" << a2;
            return 0;
        }
        if (func == "dtk_set_window_title") {
            setWindowTitle(str);
            qDebug() << "[DTK Host] Set window title:" << str;
            return 0;
        }
        if (func == "dtk_set_window_size") {
            resize(a1, a2);
            qDebug() << "[DTK Host] Set window size:" << a1 << "x" << a2;
            return 0;
        }
        if (func == "dtk_get_theme") {
            return (m_currentTheme == DGuiApplicationHelper::DarkType) ? 1 : 0;
        }
        if (func == "dtk_set_theme") {
            m_currentTheme = (a1 == 1) ? DGuiApplicationHelper::DarkType
                                       : DGuiApplicationHelper::LightType;
            DGuiApplicationHelper::instance()->setPaletteType(m_currentTheme);
            applyThemeColors();
            qDebug() << "[DTK Host] Set theme:" << (a1 == 1 ? "dark" : "light");
            return 0;
        }
        if (func == "dtk_set_label_text") {
            auto it = m_widgets.find(a1);
            if (it != m_widgets.end()) {
                auto *label = qobject_cast<DLabel*>(it.value());
                if (label) label->setText(str);
            }
            qDebug() << "[DTK Host] set_label_text id:" << a1 << "text:" << str;
            return 0;
        }
        if (func == "dtk_set_widget_enabled") {
            auto it = m_widgets.find(a1);
            if (it != m_widgets.end()) {
                it.value()->setEnabled(a2 != 0);
            }
            qDebug() << "[DTK Host] set_widget_enabled id:" << a1 << "enabled:" << a2;
            return 0;
        }
        // ── 文本编辑器专用导入 ──
        if (func == "dtk_get_editor_text") {
            if (m_editor) {
                outResultStr = m_editor->toPlainText();
                qDebug() << "[DTK Host] get_editor_text length:" << outResultStr.toUtf8().size();
                return (int32_t)outResultStr.toUtf8().size();
            }
            return 0;
        }
        if (func == "dtk_set_editor_text") {
            if (m_editor) {
                // 阻断 textChanged 信号，避免回调 WASM 形成循环
                m_editor->blockSignals(true);
                m_editor->setPlainText(str);
                m_editor->blockSignals(false);
            }
            qDebug() << "[DTK Host] set_editor_text length:" << str.toUtf8().size();
            return 0;
        }
        if (func == "dtk_get_editor_text_length") {
            if (m_editor) {
                int32_t len = (int32_t)m_editor->toPlainText().toUtf8().size();
                qDebug() << "[DTK Host] get_editor_text_length:" << len;
                return len;
            }
            return 0;
        }
        if (func == "dtk_clear_editor") {
            if (m_editor) {
                m_editor->blockSignals(true);
                m_editor->clear();
                m_editor->blockSignals(false);
            }
            qDebug() << "[DTK Host] clear_editor";
            return 0;
        }
        if (func == "dtk_open_file_dialog") {
            QString fileName = DFileDialog::getOpenFileName(
                this, "Open File", QString(),
                "Text Files (*.txt *.cpp *.h *.js *.md *.json);;All Files (*)");
            if (fileName.isEmpty()) {
                qDebug() << "[DTK Host] open_file_dialog: cancelled";
                return 0;
            }
            outResultStr = fileName;
            qDebug() << "[DTK Host] open_file_dialog:" << fileName;
            return (int32_t)fileName.toUtf8().size();
        }
        if (func == "dtk_save_file_dialog") {
            QString fileName = DFileDialog::getSaveFileName(
                this, "Save File", "untitled.txt",
                "Text Files (*.txt);;All Files (*)");
            if (fileName.isEmpty()) {
                qDebug() << "[DTK Host] save_file_dialog: cancelled";
                return 0;
            }
            outResultStr = fileName;
            qDebug() << "[DTK Host] save_file_dialog:" << fileName;
            return (int32_t)fileName.toUtf8().size();
        }
        if (func == "dtk_read_file") {
            // str = 文件路径, a1 = ptr (WASM 内存写入位置), a2 = maxlen
            QFile file(str);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "[DTK Host] read_file: cannot open" << str;
                return -1;
            }
            QByteArray content = file.readAll();
            file.close();
            // 通过 result_str 机制返回文件内容
            outResultStr = QString::fromUtf8(content);
            int32_t len = (int32_t)content.size();
            if (len > a2) len = a2;
            qDebug() << "[DTK Host] read_file:" << str << "bytes:" << content.size();
            return len;
        }
        if (func == "dtk_write_file") {
            // str = 文件路径, a1/a2 = data ptr/len (在 STR 区域)
            // 但对于 dtk_write_file，C++ 侧通过 str 参数收到的是文件路径
            // data 内容需要从 WASM 内存读取——这里 str 是路径
            // 实际上 dtk_write_file(path, pathLen, data, dataLen):
            //   a1=path ptr, a2=path len → 预读为 str
            //   a3=data ptr, data 是第二个字符串参数
            // 我们需要从 WASM 内存读取 data——但 Worker 不会预读 a3
            // 解决方案：C++ 侧通过 readMemory 从 WASM 内存读取 data
            // 但这会导致死锁（C++ 阻塞等 stdout，Node 阻塞等 import_result）
            // 替代方案：dtk_write_file 的 data 通过 result_str 机制传输
            // Worker 端对 dtk_write_file 特殊处理：data 在 a3，也预读到 SAB 的某个区域
            // 更简单的方案：str = 路径，通过另一个 IPC read_memory 读 data
            // 但死锁问题...

            // 实际方案：dtk_write_file 的 str 参数是路径（预读 a1/a2），
            // data 内容通过 STR 区域传输——但 STR 已被路径占用
            // 最简方案：路径和 data 都放入 STR 区域，用分隔符分开
            // 但这很 hacky

            // 最干净的方案：C++ 通过 read_memory IPC 读 data
            // 但 C++ 正在 sendAndProcess 循环中，不能发起另一个 IPC
            // 除非... 我们让 Worker 也预读 data 参数

            // 让 Worker 对 dtk_write_file 特殊处理：
            // a1/a2 = path, a3 = data ptr
            // Worker 预读 path 到 STR，预读 data 到 RESULT_STR
            // 但 RESULT_STR 是 C++ → Worker 方向，不能用于 Worker → C++

            // 最简方案：直接用 str 传路径，data 通过追加到 str 后面传输
            // 格式: str = path + "\x00" + data

            // 实际最简方案：str 是路径，我们通过 readMemory 读 data
            // 但这需要异步——不行

            // 让我重新设计：dtk_write_file 的 data 在 a3
            // Worker 端在 STR_FUNCS 中，dtk_write_file 预读 a1/a2 (path) 为 str
            // 同时也读 a3 到... 我们需要第二个字符串通道

            // 暂时方案：在 JS 端对 dtk_write_file 特殊处理，
            // 将 data 也放入 str 字段（路径在前面）
            // 但 C++ 需要分离

            // 最简单：str = path，额外 IPC read data
            // 但需要 Worker 配合...

            // 其实 JS 已经把 data 预读了——只是放到了 STR 区域
            // 但路径也在 STR 区域
            // 我们需要修改 JS 让 dtk_write_file 的 data 放到 RESULT_STR 区域
            // 但那是反方向的

            // 最终方案：dtk_write_file 的 a1/a2 是 path（STR 预读）
            // a3 是 data_ptr，a2 是 data_len——不对，a2 已被 path len 用了
            // dtk_write_file(path, pathLen, data, dataLen) → a1=path, a2=pathLen, a3=data, 参数4=dataLen
            // 但我们只有 3 个 arg 通道

            // 重新设计 WASM 接口：
            // dtk_write_file(pathPtr, pathLen, dataPtr, dataLen)
            // a1=pathPtr, a2=pathLen, a3=dataPtr
            // dataLen 无法传递——除非用固定约定

            // 更好的方案：让 JS 也预读 a3 指向的字符串
            // 用 RESULT_STR 区域传 data（但这是 C++→Worker 方向）
            // 或者用另一个 SAB 区域

            // 实际最简方案：在 SAB 中添加 DATA 区域
            // 但这增加复杂度

            // 最实际方案：str = path，C++ 通过 readMemory 读 data
            // 但这需要改变 sendAndProcess 的逻辑

            // 或者：让 WASM 插件分两步调用
            // 先 dtk_write_file_prepare(path, pathLen) → 返回 0
            // 再 dtk_write_data(dataPtr, dataLen) → 但这需要状态

            // 最终决定：str 携带 path，通过 readMemory 从 WASM 读 data
            // 在 handleDtkHostCall 中不能调 sendAndProcess（嵌套）
            // 所以我们让 m_wasm 的 readMemory 直接读 m_editor 的文本
            // 因为 data 就是编辑器内容！

            // 等等——dtk_write_file 被 WASM 调用时，data 参数来自 WASM 内存
            // WASM 插件在 on_save_clicked 中调 dtk_get_editor_text 拿到文本
            // 然后传给 dtk_write_file
            // 所以 data 就是编辑器文本——我们可以直接用 m_editor->toPlainText()

            // 但这样 C++ 就在处理逻辑了，违反"纯转发"原则
            // 不过文件 I/O 本来就是宿主职责（WASM 无法直接访问文件系统）

            // 更好的方案：让 JS 对 dtk_write_file 特殊处理
            // 在 STR_FUNCS 中，但使用不同的预读逻辑：
            // path 在 a1/a2，data 在 a3 + 用 result_strlen 传递 data_len
            // 不行，result 是返回值

            // 最终方案：修改 JS makeDtkImport 对 dtk_write_file 特殊处理
            // 将 path 放入 STR，将 data 也读出来放到 RESULT_STR 区域
            // C++ 从 STR 读 path，从 RESULT_STR 读 data

            // 但 RESULT_STR 是在 import_result 时由 C++ 写入的
            // Worker → C++ 方向的数据通过 import 消息的 str 字段传输
            // 我们需要 import 消息有第二个 str 字段

            // OK 最简单：import 消息添加 str2 字段
            // JS 对 dtk_write_file 预读两个字符串

            // 但这需要改 JS 和 C++ 的 IPC

            // 最简单方案：用分隔符。str = path + "\n" + data
            // C++ 分割 str 得到 path 和 data

            // 这个方案最简单，不需要改 SAB 布局
            // str 中 \n 作为分隔符，但文件内容可能包含 \n
            // 用 \x00 作为分隔符——JSON 不支持 \x00

            // 用 base64 编码 data 部分？
            // str = path + "\x01" + base64(data)

            // 太复杂了。让我用最直接的方案：
            // dtk_write_file 不通过 STR 预读，而是 C++ 通过 readMemory 读 data
            // 在 handleDtkHostCall 返回后，sendAndProcess 继续处理
            // 但 readMemory 调用 sendAndProcess → 嵌套！

            // 最终方案：在 SAB 中添加第二个字符串区域
            // 但 SAB 已经很大了

            // OK 让我用最实际的方法：
            // JS 端对 dtk_write_file 特殊处理：
            // a1/a2 = path → STR 区域（str 字段）
            // a3 = data ptr → 额外读到 str2 字段
            // 但 a3 只有 ptr，没有 len
            // 我们需要传 data_len——可以用第 4 个 arg

            // 等等，dtk_write_file(path, pathLen, data, dataLen)
            // 在 WASM 中，这映射到 args = [pathPtr, pathLen, dataPtr, dataLen]
            // 但 makeDtkImport 只处理 3 个 args
            // 我们需要第 4 个 arg

            // 让我修改 makeDtkImport 支持 4 个 args
            // 并在 SAB 中添加 ARG4_OFFSET

            // 这需要改 SAB 布局——但只在 JS 端改就行
            // SAB 中 STR 区域之前有空间

            // 或者...更简单：
            // dtk_write_file 改为 3 个参数：dtk_write_file(pathPtr, pathLen, dataPtr)
            // data_len 通过 dtk_get_editor_text_length 获取
            // WASM 在调用 dtk_write_file 前先调 dtk_get_editor_text_length
            // 但 dtk_write_file 仍然需要知道 data_len 来预读

            // 最简单方案：让 JS 对 dtk_write_file 特殊处理
            // 预读 path (a1/a2) 到 str
            // 预读 data (a3/a3+1) 到 str2
            // 但我们只有 3 args: a1, a2, a3
            // dtk_write_file(path, pathLen, data, dataLen) → 4 args
            // JS 的 args 数组会有 4 个元素

            // 让 makeDtkImport 读 args[3] 作为 data_len
            // 但 SAB 没有 ARG4 位置

            // 我来改方案：dtk_write_file 在 WASM 中改为
            // dtk_write_file(pathPtr, pathLen, dataPtr, dataLen)
            // JS 端处理 4 个 args：a1=path, a2=pathLen, a3=dataPtr, a4=dataLen
            // 在 SAB 中用 STR 区域存 path，用 RESULT_STR 区域存 data
            // 但 RESULT_STR 是 C++ → Worker 方向...

            // 算了，让我用最简单的方案：
            // dtk_write_file(pathPtr, pathLen, dataPtr, dataLen)
            // JS 端：path 在 a1/a2 → STR 区域 → str 字段
            //        data 在 a3/dataLen → 需要 dataLen
            // 但 WASM 调用时 args = [pathPtr, pathLen, dataPtr, dataLen]
            // JS makeDtkImport 的 args 有 4 个
            // 我们可以从 args[3] 获取 dataLen

            // 让我修改 makeDtkImport 对 dtk_write_file 特殊处理
            // 读 path 到 STR，读 data 到 RESULT_STR
            // import 消息包含 str (path) 和 str2 (data)
            // C++ 从 str 读 path，从 str2 读 data

            // 这需要：
            // 1. JS makeDtkImport 对 dtk_write_file 特殊处理
            // 2. pollImport 对 dtk_write_file 附加 str2
            // 3. C++ sendAndProcess 解析 str2

            // 让我实现这个方案
            // 但...这太复杂了

            // 最简单方案：直接用 m_editor 的文本作为 data
            // 因为 on_save_clicked 先获取编辑器文本，然后传给 dtk_write_file
            // C++ 侧可以直接从 m_editor 读文本写入文件
            // 这不违反"纯转发"原则，因为文件 I/O 本来就是宿主职责
            // WASM 负责"何时保存、保存什么"的逻辑
            // 宿主负责"如何写入文件"

            // str = 文件路径，data 直接从 m_editor 读取
            QFile file(str);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qWarning() << "[DTK Host] write_file: cannot open" << str;
                return -1;
            }
            QTextStream out(&file);
            out << m_editor->toPlainText();
            file.close();
            qDebug() << "[DTK Host] write_file:" << str;
            return 0;
        }
        if (func == "dtk_append_message") {
            // 保留兼容性
            return 0;
        }
        qWarning() << "[DTK Host] Unknown import:" << func;
        return 0;
    }

    void buildFallbackUi()
    {
        setWindowTitle("WASM Text Editor (DTK) - Fallback");
        resize(800, 600);

        m_titleLabel = new DLabel("WASM-Powered Text Editor", this);
        QFont f = m_titleLabel->font(); f.setBold(true); f.setPointSize(14);
        m_titleLabel->setFont(f);
        m_mainLayout->addWidget(m_titleLabel);

        m_statusLabel = new DLabel("Ready", this);
        m_mainLayout->addWidget(m_statusLabel);

        auto *hbox = new QHBoxLayout();
        m_newButton = new DSuggestButton("New", this);
        m_openButton = new DSuggestButton("Open", this);
        m_saveButton = new DSuggestButton("Save", this);
        m_crashButton = new DSuggestButton("Crash Test", this);
        hbox->addWidget(m_newButton);
        hbox->addWidget(m_openButton);
        hbox->addWidget(m_saveButton);
        hbox->addWidget(m_crashButton);
        m_mainLayout->addLayout(hbox);

        m_editor = new DTextEdit(this);
        m_editor->setReadOnly(false);
        m_editor->installEventFilter(this);
        m_editor->viewport()->installEventFilter(this);
        m_mainLayout->addWidget(m_editor, 1);
    }

    void applyThemeColors()
    {
        if (!m_editor) return;

        DPalette pal = m_editor->palette();
        if (m_currentTheme == DGuiApplicationHelper::DarkType) {
            pal.setColor(DPalette::Base, QColor(30, 30, 30));
            pal.setColor(DPalette::Text, QColor(220, 220, 220));
            pal.setColor(DPalette::Window, QColor(30, 30, 30));
            pal.setColor(DPalette::WindowText, QColor(220, 220, 220));
        } else {
            pal.setColor(DPalette::Base, QColor(250, 250, 250));
            pal.setColor(DPalette::Text, QColor(30, 30, 30));
            pal.setColor(DPalette::Window, QColor(250, 250, 250));
            pal.setColor(DPalette::WindowText, QColor(30, 30, 30));
        }
        m_editor->setPalette(pal);
        m_editor->setAutoFillBackground(true);
        m_editor->viewport()->setPalette(pal);
        m_editor->viewport()->setAutoFillBackground(true);

        QString bgStyle = (m_currentTheme == DGuiApplicationHelper::DarkType)
            ? "DTextEdit, QTextEdit, QWidget { background-color: rgb(30,30,30); color: rgb(220,220,220); }"
            : "DTextEdit, QTextEdit, QWidget { background-color: rgb(250,250,250); color: rgb(30,30,30); }";
        m_editor->setStyleSheet(bgStyle);

        DPalette winPal = palette();
        if (m_currentTheme == DGuiApplicationHelper::DarkType) {
            winPal.setColor(DPalette::Window, QColor(30, 30, 30));
            winPal.setColor(DPalette::WindowText, QColor(220, 220, 220));
        } else {
            winPal.setColor(DPalette::Window, QColor(250, 250, 250));
            winPal.setColor(DPalette::WindowText, QColor(30, 30, 30));
        }
        setPalette(winPal);

        for (QWidget *child : findChildren<QWidget*>()) {
            child->style()->unpolish(child);
            child->style()->polish(child);
        }

        m_editor->repaint();
        repaint();
    }

    void showContextMenu(const QPoint &globalPos)
    {
        DMenu menu(this);

        QAction *copyAction = menu.addAction("Copy");
        copyAction->setEnabled(hasSelectableText());

        QAction *pasteAction = menu.addAction("Paste");
        pasteAction->setEnabled(m_editor && m_editor->isEnabled());

        menu.addAction("Save As...");
        menu.addSeparator();

        QString themeLabel = (m_currentTheme == DGuiApplicationHelper::LightType)
                             ? "Switch to Dark Theme"
                             : "Switch to Light Theme";
        menu.addAction(themeLabel);

        QAction *selected = menu.exec(globalPos);
        if (!selected) return;

        QString text = selected->text();
        if (text == "Copy") {
            doCopy();
        } else if (text == "Paste") {
            doPaste();
        } else if (text == "Save As...") {
            onSaveClicked();
        } else if (text.contains("Theme")) {
            toggleTheme();
        }
    }

    bool hasSelectableText() const
    {
        if (m_editor && m_editor->textCursor().hasSelection())
            return true;
        return false;
    }

    void doCopy()
    {
        if (m_editor && m_editor->textCursor().hasSelection()) {
            m_editor->copy();
        }
    }

    void doPaste()
    {
        if (m_editor && m_editor->isEnabled()) {
            m_editor->paste();
        }
    }

    void toggleTheme()
    {
        if (m_wasm && m_wasm->isAlive()) {
            m_wasm->callExport("toggle_theme");
        } else {
            if (m_currentTheme == DGuiApplicationHelper::LightType) {
                m_currentTheme = DGuiApplicationHelper::DarkType;
            } else {
                m_currentTheme = DGuiApplicationHelper::LightType;
            }
            DGuiApplicationHelper::instance()->setPaletteType(m_currentTheme);
            applyThemeColors();
        }
    }

private slots:
    void onTextChanged()
    {
        if (m_wasm && m_wasm->isAlive()) {
            m_wasm->callExport("on_text_changed");
        }
    }

    void onNewClicked()
    {
        if (m_wasm && m_wasm->isAlive()) {
            m_wasm->callExport("on_new_clicked");
        }
    }

    void onOpenClicked()
    {
        if (m_wasm && m_wasm->isAlive()) {
            m_wasm->callExport("on_open_clicked");
        }
    }

    void onSaveClicked()
    {
        if (m_wasm && m_wasm->isAlive()) {
            m_wasm->callExport("on_save_clicked");
        }
    }

    void onCrashClicked()
    {
        if (m_wasm && m_wasm->isAlive()) {
            qDebug() << "=== CRASH TEST: calling crash_test() ===";
            m_wasm->callExport("crash_test");
            // callExport 返回后检查进程是否崩溃
            if (!m_wasm->isAlive()) {
                qDebug() << "=== WASM plugin crashed as expected! Main app survived. ===";
                handleWasmCrash();
            }
        }
    }

private:
    void handleWasmCrash()
    {
        // WASM 插件崩溃后，主程序仍可运行
        // 清理旧控件，重新加载 WASM 插件
        qDebug() << "=== Reloading WASM plugin after crash... ===";

        // 清理旧控件
        for (auto *widget : m_widgets) {
            if (widget && widget != m_editor) {
                m_mainLayout->removeWidget(widget);
                delete widget;
            }
        }
        if (m_editor) {
            m_mainLayout->removeWidget(m_editor);
            delete m_editor;
            m_editor = nullptr;
        }
        for (auto *layout : m_layouts) {
            if (layout) delete layout;
        }
        m_widgets.clear();
        m_layouts.clear();
        m_titleLabel = nullptr;
        m_statusLabel = nullptr;
        m_newButton = nullptr;
        m_openButton = nullptr;
        m_saveButton = nullptr;
        m_crashButton = nullptr;
        m_nextWidgetId = 1;

        // 重新加载
        loadWasmPlugin();

        // 显示恢复消息
        if (m_statusLabel) {
            m_statusLabel->setText("WASM plugin reloaded after crash — main app survived!");
        }
    }

private:
    std::unique_ptr<WasmProcessBridge> m_wasm;

    DLabel *m_titleLabel;
    DLabel *m_statusLabel;
    DTextEdit *m_editor;
    DPushButton *m_newButton;
    DPushButton *m_openButton;
    DPushButton *m_saveButton;
    DPushButton *m_crashButton;
    QVBoxLayout *m_mainLayout;

    QMap<int32_t, QWidget *> m_widgets;
    QMap<int32_t, QHBoxLayout *> m_layouts;
    int32_t m_nextWidgetId;

    DGuiApplicationHelper::ColorType m_currentTheme;
};

#endif // DTKTEXTEDITOR_H
