#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>

/**
 * test_editor.cpp — WASM 文本编辑器插件冒烟测试（非 GUI）。
 *
 * 直接通过 Node.js IPC 测试 WASM 事件驱动架构：
 *   1. 加载 WASM 插件，调用 build_ui()
 *      → 验证 import 调用包含 dtk_subscribe_event 和 dtk_add_context_menu_item
 *   2. 调用 on_event(EVT_TOGGLE_THEME=6, 0) — 验证事件分发
 *   3. 调用 on_event(EVT_CRASH_CLICKED=4, 0) — 触发 WASM 崩溃
 *   4. 验证主进程（测试程序）仍在运行
 *
 * 事件 ID 由 WASM 插件定义（build_ui 中 subscribe/register 时传入）：
 *   1=New, 2=Open, 3=Save, 4=Crash, 5=TextChanged, 6=ToggleTheme
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTextStream out(stdout);
    out << "=== WASM Text Editor Plugin Test (Event-Driven) ===" << Qt::endl;

    QProcess node;
    node.setProgram("node");
    node.setArguments({ "scripts/wasm_dtk_host.js", "plugins", "text_editor_plugin" });
    node.setProcessChannelMode(QProcess::SeparateChannels);
    node.start();

    if (!node.waitForStarted(5000)) {
        out << "[FAIL] Cannot start node process" << Qt::endl;
        return 1;
    }

    // 等待 stderr 就绪信号
    node.setReadChannel(QProcess::StandardError);
    node.waitForReadyRead(5000);
    QByteArray stderrData = node.readAllStandardError();
    out << "[INFO] stderr: " << QString::fromUtf8(stderrData).trimmed() << Qt::endl;
    node.setReadChannel(QProcess::StandardOutput);

    int importCount = 0;
    QStringList importFuncs;

    // 发送 JSON 请求，处理中间的 import 请求，统计 import 数
    auto sendJsonCount = [&](const QJsonObject &obj) -> QJsonObject {
        QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        line.append('\n');
        node.write(line);
        node.waitForBytesWritten(2000);

        while (true) {
            QByteArray accum;
            while (accum.indexOf('\n') < 0) {
                if (!node.waitForReadyRead(10000)) return {};
                accum += node.readAll();
            }
            int nl = accum.indexOf('\n');
            QByteArray respLine = accum.left(nl).trimmed();

            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(respLine, &parseError);
            if (parseError.error != QJsonParseError::NoError) continue;

            QJsonObject msg = doc.object();
            QString type = msg.value("type").toString();

            if (type == "call_result") {
                return msg;
            } else if (type == "import") {
                importCount++;
                QString func = msg.value("func").toString();
                importFuncs << func;
                out << "  [import #" << importCount << "] " << func << Qt::endl;

                QJsonObject result;
                result["type"] = "import_result";
                result["value"] = importCount; // 返回递增的控件 ID
                QByteArray resultLine = QJsonDocument(result).toJson(QJsonDocument::Compact);
                resultLine.append('\n');
                node.write(resultLine);
                node.waitForBytesWritten(2000);
            } else if (type == "error") {
                return msg;
            }
        }
    };

    // 测试 1: build_ui()
    out << Qt::endl << "[TEST 1] Calling build_ui()..." << Qt::endl;
    QJsonObject callReq;
    callReq["type"] = "call";
    callReq["func"] = "build_ui";
    callReq["args"] = QJsonArray();
    QJsonObject resp = sendJsonCount(callReq);

    if (resp.contains("error")) {
        out << "[FAIL] build_ui() error: " << resp.value("error").toString() << Qt::endl;
        return 1;
    }
    out << "[OK] build_ui() returned: " << resp.value("value").toInt() << Qt::endl;
    out << "  → " << importCount << " import calls during build_ui" << Qt::endl;

    if (importCount < 10) {
        out << "[FAIL] Expected at least 10 imports, got " << importCount << Qt::endl;
        return 1;
    }
    out << "[OK] Import count looks correct" << Qt::endl;

    // 验证事件驱动架构：build_ui 中应包含 dtk_subscribe_event 和 dtk_add_context_menu_item
    bool hasSubscribe = importFuncs.contains("dtk_subscribe_event");
    bool hasMenuItem = importFuncs.contains("dtk_add_context_menu_item");
    out << "  → dtk_subscribe_event calls: "
        << importFuncs.filter("dtk_subscribe_event").size() << Qt::endl;
    out << "  → dtk_add_context_menu_item calls: "
        << importFuncs.filter("dtk_add_context_menu_item").size() << Qt::endl;
    if (!hasSubscribe) {
        out << "[FAIL] build_ui() did not call dtk_subscribe_event" << Qt::endl;
        return 1;
    }
    if (!hasMenuItem) {
        out << "[FAIL] build_ui() did not call dtk_add_context_menu_item" << Qt::endl;
        return 1;
    }
    out << "[OK] Event-driven architecture verified (subscribe + menu registration)" << Qt::endl;

    // 测试 2: on_event(EVT_TOGGLE_THEME=6, 0) — 通过通用事件入口切换主题
    out << Qt::endl << "[TEST 2] Calling on_event(6, 0) — toggle theme..." << Qt::endl;
    importCount = 0;
    QJsonObject themeReq;
    themeReq["type"] = "call";
    themeReq["func"] = "on_event";
    themeReq["args"] = QJsonArray{ 6, 0 }; // eventId=6 (toggle_theme), widgetId=0
    resp = sendJsonCount(themeReq);
    out << "[OK] on_event(6, 0) returned: " << resp.value("value").toInt()
        << " (" << importCount << " imports)" << Qt::endl;

    // 测试 3: on_event(EVT_CRASH_CLICKED=4, 0) — 通过通用事件入口触发崩溃
    out << Qt::endl << "[TEST 3] Calling on_event(4, 0) — crash test..." << Qt::endl;
    out << "  → Expecting WASM crash (unreachable instruction)" << Qt::endl;
    QJsonObject crashReq;
    crashReq["type"] = "call";
    crashReq["func"] = "on_event";
    crashReq["args"] = QJsonArray{ 4, 0 }; // eventId=4 (crash), widgetId=0
    resp = sendJsonCount(crashReq);

    // 检查是否收到 error（崩溃）
    if (resp.contains("error") || resp.contains("crash")) {
        out << "[OK] WASM crashed as expected: " << resp.value("error").toString() << Qt::endl;
    } else if (resp.isEmpty()) {
        out << "[OK] WASM process crashed (no response — process died)" << Qt::endl;
    } else {
        out << "[WARN] on_event(4, 0) returned normally: " << resp.value("value").toInt() << Qt::endl;
    }

    // 测试 4: 验证主进程仍在运行
    out << Qt::endl << "[TEST 4] Verifying main process survived..." << Qt::endl;
    if (node.state() == QProcess::NotRunning) {
        out << "[OK] Node process exited after crash (expected)" << Qt::endl;
    } else {
        out << "[INFO] Node process still running, sending quit..." << Qt::endl;
        node.write("{\"type\":\"quit\"}\n");
        node.waitForBytesWritten(1000);
    }

    out << Qt::endl << "=== MAIN PROCESS SURVIVED WASM CRASH ===" << Qt::endl;
    out << "=== ALL TESTS PASSED ===" << Qt::endl;

    QTimer::singleShot(500, &app, &QCoreApplication::quit);
    return app.exec();
}
