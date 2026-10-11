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
#include <QVector>
#include <memory>

#include "WasmProcessBridge.h"

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

/**
 * DtkTextEditor — DTK 宿主，通过进程插件加载 WASM 文本编辑器插件。
 *
 * 架构：纯展示宿主——仅嵌入界面展示，不承担任何逻辑。
 * 宿主源码不包含任何插件业务知识（不知道有哪些按钮、有哪些功能）。
 * 全部编辑器功能由 WASM 插件实现，通过事件驱动架构通信。
 *
 * 事件驱动通信：
 *   1. WASM 插件在 build_ui 中创建控件后调用 dtk_subscribe_event(widgetId, eventType, eventId)
 *      订阅事件——宿主记录映射，连接 Qt 信号
 *   2. 事件触发时宿主调用 callExport_ii("on_event", eventId, widgetId)——通用入口
 *   3. WASM 插件在 on_event 中根据 eventId 分发到自己的处理逻辑
 *   4. 右键菜单项由 WASM 插件通过 dtk_add_context_menu_item 注册
 *
 * 三方换插件时，宿主源码零修改——新插件只需导出 build_ui/on_event/alloc。
 *
 *   ┌──────── DTK 客户端进程 ────────┐    ┌──── Node.js 子进程 ────┐
 *   │  DtkTextEditor (纯展示宿主)     │    │  wasm_dtk_host.js      │
 *   │    ↕ JSON IPC (stdin/stdout)   │◄──►│  ↕ WebAssembly API    │
 *   │  handleDtkHostCall()           │◄───│  dtk_create_label()   │
 *   │  → new DLabel / DTextEdit ...  │───►│  返回 widget_id        │
 *   │  dtk_subscribe_event()         │◄───│  订阅控件事件          │
 *   │  → connect Qt signal           │    │                        │
 *   │  callExport("build_ui")        │───►│  exports.build_ui()    │
 *   │  callExport_ii("on_event",id)  │───►│  exports.on_event()    │
 *   └────────────────────────────────┘    └────────────────────────┘
 */
class DtkTextEditor : public DMainWindow
{
    Q_OBJECT

public:
    explicit DtkTextEditor(QWidget *parent = nullptr)
        : DMainWindow(parent)
        , m_editor(nullptr)
        , m_statusLabel(nullptr)
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
        // 事件连接由 WASM 插件在 build_ui 中通过 dtk_subscribe_event 动态注册
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
     *
     * 宿主只提供 UI 原语和事件订阅机制，不包含任何业务逻辑。
     */
    int32_t handleDtkHostCall(const QString &func, int32_t a1, int32_t a2, int32_t a3, const QString &str, QString &outResultStr)
    {
        // ── 控件创建 ──
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

        // ── 窗口属性 ──
        if (func == "dtk_set_window_title") {
            setWindowTitle(str);
            return 0;
        }
        if (func == "dtk_set_window_size") {
            resize(a1, a2);
            return 0;
        }

        // ── 主题 ──
        if (func == "dtk_get_theme") {
            return (m_currentTheme == DGuiApplicationHelper::DarkType) ? 1 : 0;
        }
        if (func == "dtk_set_theme") {
            m_currentTheme = (a1 == 1) ? DGuiApplicationHelper::DarkType
                                       : DGuiApplicationHelper::LightType;
            DGuiApplicationHelper::instance()->setPaletteType(m_currentTheme);
            applyThemeColors();
            return 0;
        }

        // ── 控件操作 ──
        if (func == "dtk_set_label_text") {
            auto it = m_widgets.find(a1);
            if (it != m_widgets.end()) {
                auto *label = qobject_cast<DLabel*>(it.value());
                if (label) label->setText(str);
            }
            return 0;
        }
        if (func == "dtk_set_widget_enabled") {
            auto it = m_widgets.find(a1);
            if (it != m_widgets.end()) {
                it.value()->setEnabled(a2 != 0);
            }
            return 0;
        }

        // ── 事件驱动架构核心导入 ──

        /**
         * dtk_subscribe_event(widgetId, eventType, eventId)
         *   eventType: 1=clicked, 2=textChanged
         *   宿主记录映射并连接 Qt 信号，事件触发时调用 on_event(eventId, widgetId)
         */
        if (func == "dtk_subscribe_event") {
            // a1=widgetId, a2=eventType, a3=eventId
            m_eventMappings.append({a1, a2, a3});
            auto it = m_widgets.find(a1);
            if (it != m_widgets.end()) {
                QWidget *widget = it.value();
                int32_t eventId = a3;
                int32_t widgetId = a1;

                if (a2 == 1) { // clicked
                    auto *btn = dynamic_cast<DPushButton*>(widget);
                    if (btn) {
                        connect(btn, &DPushButton::clicked, this, [this, eventId, widgetId]() {
                            if (m_wasm && m_wasm->isAlive()) {
                                m_wasm->callExport_ii("on_event", eventId, widgetId);
                                if (!m_wasm->isAlive()) {
                                    handleWasmCrash();
                                }
                            }
                        });
                        qDebug() << "[DTK Host] Subscribed clicked event for widget" << a1 << "eventId:" << a3;
                    }
                } else if (a2 == 2) { // textChanged
                    auto *te = dynamic_cast<DTextEdit*>(widget);
                    if (te) {
                        connect(te, &DTextEdit::textChanged, this, [this, eventId, widgetId]() {
                            if (m_wasm && m_wasm->isAlive()) {
                                m_wasm->callExport_ii("on_event", eventId, widgetId);
                            }
                        });
                        qDebug() << "[DTK Host] Subscribed textChanged event for widget" << a1 << "eventId:" << a3;
                    }
                }
            }
            return a3; // 返回 eventId
        }

        /**
         * dtk_add_context_menu_item(label, len, eventId)
         *   宿主记录菜单项，右键菜单中显示，用户选择后调用 on_event(eventId, 0)
         */
        if (func == "dtk_add_context_menu_item") {
            // str=label, a3=eventId
            int32_t eventId = a3;
            m_menuItems.append({eventId, str});
            qDebug() << "[DTK Host] Registered context menu item:" << str << "eventId:" << eventId;
            return eventId;
        }

        // ── 文本编辑器专用导入（UI 原语，非业务逻辑） ──
        if (func == "dtk_get_editor_text") {
            if (m_editor) {
                outResultStr = m_editor->toPlainText();
                return (int32_t)outResultStr.toUtf8().size();
            }
            return 0;
        }
        if (func == "dtk_set_editor_text") {
            if (m_editor) {
                m_editor->blockSignals(true);
                m_editor->setPlainText(str);
                m_editor->blockSignals(false);
            }
            return 0;
        }
        if (func == "dtk_get_editor_text_length") {
            if (m_editor) {
                return (int32_t)m_editor->toPlainText().toUtf8().size();
            }
            return 0;
        }
        if (func == "dtk_clear_editor") {
            if (m_editor) {
                m_editor->blockSignals(true);
                m_editor->clear();
                m_editor->blockSignals(false);
            }
            return 0;
        }
        if (func == "dtk_open_file_dialog") {
            QString fileName = DFileDialog::getOpenFileName(
                this, "Open File", QString(),
                "Text Files (*.txt *.cpp *.h *.js *.md *.json);;All Files (*)");
            if (fileName.isEmpty()) {
                return 0;
            }
            outResultStr = fileName;
            return (int32_t)fileName.toUtf8().size();
        }
        if (func == "dtk_save_file_dialog") {
            QString fileName = DFileDialog::getSaveFileName(
                this, "Save File", "untitled.txt",
                "Text Files (*.txt);;All Files (*)");
            if (fileName.isEmpty()) {
                return 0;
            }
            outResultStr = fileName;
            return (int32_t)fileName.toUtf8().size();
        }
        if (func == "dtk_read_file") {
            // str = 文件路径 (预读自 a1/a2)
            // a3 = destPtr (WASM 内存写入位置) — Worker 用 result_str 复制到此处
            // 注意: maxlen 是第 4 个参数, SAB 只有 3 个 arg 槽, 无法传递
            // 但 RESULT_STR_MAX=64KB 已足够, WASM 分配 65536 字节缓冲区
            QFile file(str);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return -1;
            }
            QByteArray content = file.readAll();
            file.close();
            outResultStr = QString::fromUtf8(content);
            int32_t len = (int32_t)content.size();
            qDebug() << "[DTK Host] read_file:" << str << "bytes:" << content.size();
            return len;
        }
        if (func == "dtk_write_file") {
            // str = 文件路径
            // data 直接从 m_editor 读取（WASM 通过 dtk_get_editor_text 获取文本后传入）
            // 文件 I/O 是宿主职责，WASM 负责"何时保存"的逻辑
            QFile file(str);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return -1;
            }
            QTextStream out(&file);
            out << m_editor->toPlainText();
            file.close();
            qDebug() << "[DTK Host] write_file:" << str;
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
        auto *newBtn = new DSuggestButton("New", this);
        auto *openBtn = new DSuggestButton("Open", this);
        auto *saveBtn = new DSuggestButton("Save", this);
        hbox->addWidget(newBtn);
        hbox->addWidget(openBtn);
        hbox->addWidget(saveBtn);
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

    /**
     * 右键菜单：内置的复制/粘贴 + WASM 插件注册的菜单项。
     * 宿主不硬编码任何业务菜单项——全部由 WASM 通过 dtk_add_context_menu_item 注册。
     */
    void showContextMenu(const QPoint &globalPos)
    {
        DMenu menu(this);

        // 内置编辑操作（宿主原语，非业务逻辑）
        QAction *copyAction = menu.addAction("Copy");
        copyAction->setEnabled(hasSelectableText());

        QAction *pasteAction = menu.addAction("Paste");
        pasteAction->setEnabled(m_editor && m_editor->isEnabled());

        // WASM 插件注册的菜单项
        if (!m_menuItems.isEmpty()) {
            menu.addSeparator();
            for (const auto &item : m_menuItems) {
                menu.addAction(item.label);
            }
        }

        QAction *selected = menu.exec(globalPos);
        if (!selected) return;

        QString text = selected->text();
        if (text == "Copy") {
            doCopy();
        } else if (text == "Paste") {
            doPaste();
        } else {
            // 查找匹配的 WASM 注册菜单项，调用 on_event
            for (const auto &item : m_menuItems) {
                if (item.label == text) {
                    if (m_wasm && m_wasm->isAlive()) {
                        m_wasm->callExport_ii("on_event", item.eventId, 0);
                        if (!m_wasm->isAlive()) {
                            handleWasmCrash();
                        }
                    }
                    break;
                }
            }
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
            QApplication::clipboard()->setText(m_editor->textCursor().selectedText());
        }
    }

    void doPaste()
    {
        if (m_editor && m_editor->isEnabled()) {
            m_editor->paste();
        }
    }

    /**
     * WASM 崩溃恢复——清理旧控件和事件映射，重新加载插件。
     */
    void handleWasmCrash()
    {
        qDebug() << "=== WASM plugin crashed! Reloading... ===";

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
        m_eventMappings.clear();
        m_menuItems.clear();
        m_titleLabel = nullptr;
        m_statusLabel = nullptr;
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
    QVBoxLayout *m_mainLayout;

    QMap<int32_t, QWidget *> m_widgets;
    QMap<int32_t, QHBoxLayout *> m_layouts;
    int32_t m_nextWidgetId;

    DGuiApplicationHelper::ColorType m_currentTheme;

    // WASM 插件注册的右键菜单项
    struct MenuItem {
        int32_t eventId;
        QString label;
    };
    QVector<MenuItem> m_menuItems;

    // 事件映射表（widgetId + eventType → eventId）——用于调试/排查
    struct EventMapping {
        int32_t widgetId;
        int32_t eventType;
        int32_t eventId;
    };
    QVector<EventMapping> m_eventMappings;
};

#endif // DTKTEXTEDITOR_H
