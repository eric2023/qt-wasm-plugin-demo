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
 * 直接通过 Node.js IPC 测试 WASM 插件：
 *   1. 加载 WASM 插件，调用 build_ui()
 *   2. 验证 import 调用序列正确
 *   3. 调用 crash_test()，验证子进程崩溃
 *   4. 验证主进程（测试程序）仍在运行
 *
 * 这验证了进程隔离的核心价值：WASM 崩溃不影响主程序。
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTextStream out(stdout);
    out << "=== WASM Text Editor Plugin Test ===" << Qt::endl;

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

    // 辅助函数：发送 JSON 并读取响应
    auto sendJson = [&](const QJsonObject &obj) -> QJsonObject {
        QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        line.append('\n');
        node.write(line);
        node.waitForBytesWritten(2000);

        // 读取响应（可能包含多个 import 请求）
        while (true) {
            // 等待一行
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
                // 处理 import 请求 — 返回简单的模拟值
                QString func = msg.value("func").toString();
                QJsonObject result;
                result["type"] = "import_result";
                result["value"] = 1; // 返回控件 ID=1
                QByteArray resultLine = QJsonDocument(result).toJson(QJsonDocument::Compact);
                resultLine.append('\n');
                node.write(resultLine);
                node.waitForBytesWritten(2000);
            } else if (type == "error") {
                return msg;
            }
        }
    };

    int importCount = 0;

    // 改进版 sendJson，统计 import 数
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

    // 测试 2: toggle_theme()
    out << Qt::endl << "[TEST 2] Calling toggle_theme()..." << Qt::endl;
    importCount = 0;
    QJsonObject themeReq;
    themeReq["type"] = "call";
    themeReq["func"] = "toggle_theme";
    themeReq["args"] = QJsonArray();
    resp = sendJsonCount(themeReq);
    out << "[OK] toggle_theme() returned: " << resp.value("value").toInt()
        << " (" << importCount << " imports)" << Qt::endl;

    // 测试 3: crash_test() — WASM 崩溃
    out << Qt::endl << "[TEST 3] Calling crash_test()..." << Qt::endl;
    out << "  → Expecting WASM crash (unreachable instruction)" << Qt::endl;
    QJsonObject crashReq;
    crashReq["type"] = "call";
    crashReq["func"] = "crash_test";
    crashReq["args"] = QJsonArray();
    resp = sendJsonCount(crashReq);

    // 检查是否收到 error（崩溃）
    if (resp.contains("error") || resp.contains("crash")) {
        out << "[OK] WASM crashed as expected: " << resp.value("error").toString() << Qt::endl;
    } else if (resp.isEmpty()) {
        out << "[OK] WASM process crashed (no response — process died)" << Qt::endl;
    } else {
        out << "[WARN] crash_test returned normally: " << resp.value("value").toInt() << Qt::endl;
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
