#ifndef DTKCHATCLIENT_H
#define DTKCHATCLIENT_H

#include <DMainWindow>
#include <DApplication>
#include <DTextEdit>
#include <DLineEdit>
#include <DLabel>
#include <DPushButton>
#include <DSuggestButton>
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
#include <QTcpSocket>
#include <QMessageBox>
#include <QFileInfo>
#include <QScrollBar>
#include <QContextMenuEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>
#include <memory>

#include "WasmProcessBridge.h"

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

/**
 * DtkChatClient — DTK 宿主，通过进程插件加载 WASM UI 插件。
 *
 * 架构：
 *
 *   ┌──────── DTK 客户端进程 ────────┐    ┌──── Node.js 子进程 ────┐
 *   │                                │    │                        │
 *   │  WasmProcessBridge             │    │  wasm_dtk_host.js      │
 *   │    ↕ JSON IPC (stdin/stdout)   │◄──►│  ↕ WebAssembly API    │
 *   │                                │    │                        │
 *   │  handleDtkHostCall(func,a1,a2) │◄───│  dtk_create_label()   │
 *   │  → new DLabel / DTextEdit ...  │───►│  返回 widget_id        │
 *   │                                │    │                        │
 *   │  callExport("build_ui")        │───►│  exports.build_ui()    │
 *   │  ← call_result                 │◄───│                        │
 *   └────────────────────────────────┘    └────────────────────────┘
 *
 * WASM 插件运行在独立进程中，通过 IPC 调用 DTK 宿主函数。
 * 插件依赖 DTK 宿主提供导入函数实现，否则无法实例化。
 */
class DtkChatClient : public DMainWindow
{
    Q_OBJECT

public:
    explicit DtkChatClient(QWidget *parent = nullptr)
        : DMainWindow(parent)
        , m_socket(nullptr)
        , m_connected(false)
        , m_messagesDisplay(nullptr)
        , m_inputField(nullptr)
        , m_sendButton(nullptr)
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

        // 设置 DTK 宿主回调 — WASM 插件通过 IPC 调用此函数创建 DTK 控件
        WasmProcessBridge::s_hostCallback = [this](const QString &func, int32_t a1, int32_t a2, const QString &str) -> int32_t {
            return this->handleDtkHostCall(func, a1, a2, str);
        };

        // 通过进程插件加载 WASM UI 插件
        m_wasm = std::make_unique<WasmProcessBridge>();
        if (!m_wasm->load(QStringLiteral("plugins/chat_ui_plugin.wasm"))) {
            qWarning() << "Failed to load WASM UI plugin! Building fallback UI.";
            buildFallbackUi();
        } else {
            qDebug() << "WASM UI plugin loaded — calling build_ui()...";
            int32_t layoutId = 0;
            if (!m_wasm->callExport("build_ui", &layoutId)) {
                qWarning() << "build_ui() failed! Building fallback UI.";
                buildFallbackUi();
            } else {
                qDebug() << "UI built by WASM plugin, layout ID:" << layoutId;
            }
        }
        applyThemeColors();

        // 连接信号
        if (m_sendButton) {
            connect(m_sendButton, &DPushButton::clicked, this, &DtkChatClient::onSendClicked);
        }
        if (m_inputField) {
            connect(m_inputField, &DLineEdit::returnPressed, this, &DtkChatClient::onSendClicked);
        }

        // TCP 套接字
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected, this, &DtkChatClient::onConnected);
        connect(m_socket, &QTcpSocket::disconnected, this, &DtkChatClient::onDisconnected);
        connect(m_socket, &QTcpSocket::readyRead, this, &DtkChatClient::onReadyRead);
        connect(m_socket, &QTcpSocket::errorOccurred, this, &DtkChatClient::onSocketError);

        QTimer::singleShot(500, this, [this]() {
            appendMessage("[System] Connecting to server...");
            m_socket->connectToHost("127.0.0.1", 12345);
        });
    }

    ~DtkChatClient()
    {
        WasmProcessBridge::s_hostCallback = nullptr;
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
     * func 是导入函数名（如 "dtk_create_label"），a1/a2 是参数。
     */
    int32_t handleDtkHostCall(const QString &func, int32_t a1, int32_t a2, const QString &str)
    {
        if (func == "dtk_create_label") {
            auto *label = new DLabel(str, this);
            int32_t id = m_nextWidgetId++;
            m_widgets[id] = label;
            if (!m_statusLabel) {
                QFont f = label->font();
                f.setBold(true);
                f.setPointSize(14);
                label->setFont(f);
            } else if (!m_titleLabel) {
                m_titleLabel = m_statusLabel;
                m_statusLabel = label;
            } else {
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
            if (!m_messagesDisplay) m_messagesDisplay = textEdit;
            qDebug() << "[DTK Host] Created text_edit (readonly=" << a1 << ") id:" << id;
            return id;
        }
        if (func == "dtk_create_lineedit") {
            auto *lineEdit = new DLineEdit(this);
            lineEdit->setPlaceholderText(str);
            lineEdit->setEnabled(false);
            int32_t id = m_nextWidgetId++;
            m_widgets[id] = lineEdit;
            if (!m_inputField) m_inputField = lineEdit;
            qDebug() << "[DTK Host] Created line_edit:" << str << "id:" << id;
            return id;
        }
        if (func == "dtk_create_pushbutton") {
            auto *btn = new DSuggestButton(str, this);
            btn->setEnabled(false);
            int32_t id = m_nextWidgetId++;
            m_widgets[id] = btn;
            if (!m_sendButton) m_sendButton = btn;
            qDebug() << "[DTK Host] Created push_button id:" << id;
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
        if (func == "dtk_append_message") {
            appendMessage(str);
            qDebug() << "[DTK Host] Append message:" << str;
            return 0;
        }

        qWarning() << "[DTK Host] Unknown import:" << func;
        return 0;
    }

    void buildFallbackUi()
    {
        setWindowTitle("WASM Chat (DTK) - Fallback");
        resize(600, 480);

        m_titleLabel = new DLabel("WASM-Powered Chat Client (DTK)", this);
        QFont f = m_titleLabel->font(); f.setBold(true); f.setPointSize(14);
        m_titleLabel->setFont(f);
        m_mainLayout->addWidget(m_titleLabel);

        m_statusLabel = new DLabel("Initializing...", this);
        m_mainLayout->addWidget(m_statusLabel);

        m_messagesDisplay = new DTextEdit(this);
        m_messagesDisplay->setReadOnly(true);
        m_messagesDisplay->installEventFilter(this);
        m_messagesDisplay->viewport()->installEventFilter(this);
        m_mainLayout->addWidget(m_messagesDisplay, 1);

        auto *hbox = new QHBoxLayout();
        m_inputField = new DLineEdit(this);
        m_inputField->setPlaceholderText("Type your message here...");
        m_sendButton = new DSuggestButton("Send", this);
        hbox->addWidget(m_inputField);
        hbox->addWidget(m_sendButton);
        m_mainLayout->addLayout(hbox);
    }

    void applyThemeColors()
    {
        if (!m_messagesDisplay) return;

        DPalette pal = m_messagesDisplay->palette();
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
        m_messagesDisplay->setPalette(pal);
        m_messagesDisplay->setAutoFillBackground(true);
        m_messagesDisplay->viewport()->setPalette(pal);
        m_messagesDisplay->viewport()->setAutoFillBackground(true);

        QString bgStyle = (m_currentTheme == DGuiApplicationHelper::DarkType)
            ? "DTextEdit, QTextEdit, QWidget { background-color: rgb(30,30,30); color: rgb(220,220,220); }"
            : "DTextEdit, QTextEdit, QWidget { background-color: rgb(250,250,250); color: rgb(30,30,30); }";
        m_messagesDisplay->setStyleSheet(bgStyle);

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

        m_messagesDisplay->repaint();
        repaint();
    }

    void showContextMenu(const QPoint &globalPos)
    {
        DMenu menu(this);

        QAction *copyAction = menu.addAction("Copy");
        copyAction->setEnabled(hasSelectableText());

        QAction *pasteAction = menu.addAction("Paste");
        pasteAction->setEnabled(m_inputField && m_inputField->isEnabled());

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
            doSaveAs();
        } else if (text.contains("Theme")) {
            toggleTheme();
        }
    }

    bool hasSelectableText() const
    {
        if (m_messagesDisplay && m_messagesDisplay->textCursor().hasSelection())
            return true;
        if (m_inputField && !m_inputField->text().isEmpty())
            return true;
        return false;
    }

    void doCopy()
    {
        if (m_messagesDisplay && m_messagesDisplay->textCursor().hasSelection()) {
            m_messagesDisplay->copy();
        } else if (m_inputField && !m_inputField->text().isEmpty()) {
            m_inputField->lineEdit()->copy();
        }
    }

    void doPaste()
    {
        if (m_inputField && m_inputField->isEnabled()) {
            m_inputField->lineEdit()->paste();
        }
    }

    void doSaveAs()
    {
        QString fileName = DFileDialog::getSaveFileName(
            this, "Save Chat History", "chat_history.txt",
            "Text Files (*.txt);;All Files (*)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Cannot open file: " + fileName);
            return;
        }
        QTextStream out(&file);
        out << m_messagesDisplay->toPlainText();
        file.close();
        appendMessage("[System] Chat history saved to: " + fileName);
    }

    void toggleTheme()
    {
        // 调用 WASM 插件的 toggle_theme 导出函数（通过进程 IPC）
        int32_t oldTheme = 0;
        if (m_wasm && m_wasm->isAlive()) {
            m_wasm->callExport("toggle_theme", &oldTheme);
        } else {
            if (m_currentTheme == DGuiApplicationHelper::LightType) {
                m_currentTheme = DGuiApplicationHelper::DarkType;
            } else {
                m_currentTheme = DGuiApplicationHelper::LightType;
            }
            DGuiApplicationHelper::instance()->setPaletteType(m_currentTheme);
            applyThemeColors();
        }
        appendMessage("[System] Theme switched to " +
                      QString(m_currentTheme == DGuiApplicationHelper::LightType ? "Light" : "Dark"));
    }

private slots:
    void onConnected()
    {
        m_connected = true;
        if (m_statusLabel) {
            m_statusLabel->setText("Connected to server (127.0.0.1:12345)");
            DPalette pal = m_statusLabel->palette();
            pal.setColor(DPalette::TextTips, QColor(0, 128, 0));
            m_statusLabel->setPalette(pal);
        }
        if (m_sendButton) m_sendButton->setEnabled(true);
        if (m_inputField) {
            m_inputField->setEnabled(true);
            m_inputField->setFocus();
        }
        appendMessage("[System] Connected to chat server!");
    }

    void onDisconnected()
    {
        m_connected = false;
        if (m_statusLabel) {
            m_statusLabel->setText("Disconnected from server");
            DPalette pal = m_statusLabel->palette();
            pal.setColor(DPalette::TextTips, QColor(255, 0, 0));
            m_statusLabel->setPalette(pal);
        }
        if (m_sendButton) m_sendButton->setEnabled(false);
        appendMessage("[System] Disconnected. Reconnecting in 3s...");
        QTimer::singleShot(3000, this, [this]() {
            if (!m_connected) {
                appendMessage("[System] Reconnecting...");
                m_socket->connectToHost("127.0.0.1", 12345);
            }
        });
    }

    void onSocketError(QAbstractSocket::SocketError)
    {
        if (m_statusLabel) {
            m_statusLabel->setText("Error: " + m_socket->errorString());
            DPalette pal = m_statusLabel->palette();
            pal.setColor(DPalette::TextTips, QColor(255, 0, 0));
            m_statusLabel->setPalette(pal);
        }
    }

    void onReadyRead()
    {
        QByteArray data = m_socket->readAll();
        QString dataStr = QString::fromUtf8(data).trimmed();
        if (dataStr.isEmpty()) return;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(dataStr.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            appendMessage("[Server] " + dataStr);
            return;
        }
        QJsonObject obj = doc.object();
        appendMessage("[Server] " + obj.value("text").toString());
    }

    void onSendClicked()
    {
        if (!m_inputField || !m_connected) return;
        QString text = m_inputField->text().trimmed();
        if (text.isEmpty()) return;

        // 通过进程插件分配 WASM 内存，写入消息，读回验证
        if (m_wasm && m_wasm->isAlive()) {
            uint32_t ptr = m_wasm->alloc(text.toUtf8().size());
            m_wasm->writeString(ptr, text);
            QString wasmText = m_wasm->readString(ptr, text.toUtf8().size());
            appendMessage("[Me] " + wasmText);
        } else {
            appendMessage("[Me] " + text);
        }

        // 发送到服务器
        QJsonObject msgObj;
        msgObj["text"] = text;
        QByteArray msgData = QJsonDocument(msgObj).toJson(QJsonDocument::Compact);
        msgData.append('\n');
        m_socket->write(msgData);
        m_socket->flush();

        m_inputField->clear();
        m_inputField->setFocus();
    }

private:
    void appendMessage(const QString &msg)
    {
        if (!m_messagesDisplay) return;
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_messagesDisplay->append(QString("[%1] %2").arg(timestamp, msg));
        auto *bar = m_messagesDisplay->verticalScrollBar();
        bar->setValue(bar->maximum());
    }

private:
    QTcpSocket *m_socket;
    bool m_connected;
    std::unique_ptr<WasmProcessBridge> m_wasm;

    DLabel *m_titleLabel;
    DLabel *m_statusLabel;
    DTextEdit *m_messagesDisplay;
    DLineEdit *m_inputField;
    DPushButton *m_sendButton;
    QVBoxLayout *m_mainLayout;

    QMap<int32_t, QWidget *> m_widgets;
    QMap<int32_t, QHBoxLayout *> m_layouts;
    int32_t m_nextWidgetId;

    DGuiApplicationHelper::ColorType m_currentTheme;
};

#endif // DTKCHATCLIENT_H
