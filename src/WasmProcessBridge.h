#ifndef WASMPROCESSBRIDGE_H
#define WASMPROCESSBRIDGE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>

/**
 * WasmProcessBridge — 进程插件桥接器。
 * WASM 插件声明的 (import "dtk" ...) 导入函数由子进程转发到本桥接器，
 * 本桥接器调用 DTK 宿主回调（创建 DTK 控件等），将返回值发回子进程。
 *
 * 通信协议：逐行 JSON（每行一个 JSON 对象，以 \n 分隔）。
 *
 *   本类 → 子进程（调用 WASM 导出函数）：
 *     {"type":"call","func":"build_ui","args":[]}
 *     {"type":"read_memory","offset":512,"len":30}
 *     {"type":"write_memory","offset":ptr,"hex":"..."}
 *
 *     {"type":"import","func":"dtk_create_label","args":[512,23]}
 *
 *   本类 → 子进程（导入回调返回值）：
 *     {"type":"import_result","value":1}
 *
 *   子进程 → 本类（导出函数调用结果）：
 *     {"type":"call_result","value":0}
 *     {"type":"call_result","hex":"48656c6c6f"}
 */
class WasmProcessBridge
{
public:
    using DtkHostCallback = std::function<int32_t(const QString &func, int32_t arg1, int32_t arg2, int32_t arg3, const QString &str, QString &outResultStr)>;

    static inline DtkHostCallback s_hostCallback = nullptr;

    WasmProcessBridge() : m_process(nullptr) {}

    ~WasmProcessBridge()
    {
        if (m_process) {
            m_process->write("{\"type\":\"quit\"}\n");
            m_process->waitForBytesWritten(500);
            m_process->kill();
            m_process->waitForFinished(1000);
            delete m_process;
        }
    }

    bool load(const QString &wasmPath)
    {
        // 从完整路径提取目录和基础名（不含扩展名）
        QFileInfo fi(wasmPath);
        QString dir = fi.absolutePath();
        QString baseName = fi.completeBaseName();

        m_process = new QProcess();
        m_process->setProgram("node");
        m_process->setArguments({ "scripts/wasm_dtk_host.js", dir, baseName });
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        m_process->start();

        if (!m_process->waitForStarted(5000)) {
            qWarning() << "[WasmProcessBridge] Failed to start node process:"
                       << m_process->errorString();
            return false;
        }

        // 等待子进程的就绪信号（stderr 上的一行）
        m_process->setReadChannel(QProcess::StandardError);
        waitForReadyLine(5000);
        m_process->setReadChannel(QProcess::StandardOutput);

        qDebug() << "[WasmProcessBridge] Node process started, WASM plugin loaded:" << wasmPath;
        return true;
    }

    // ── 调用 WASM 导出函数（0 参数，返回 i32） ──
    bool callExport(const QString &func, int32_t *outResult = nullptr)
    {
        return callExportWithArgs(func, {}, outResult);
    }

    // ── 调用 WASM 导出函数（1 个 i32 参数，返回 i32） ──
    bool callExport_i32(const QString &func, int32_t arg, int32_t *outResult = nullptr)
    {
        return callExportWithArgs(func, { arg }, outResult);
    }

    // ── 调用 WASM 导出函数（2 个 i32 参数，返回 i32） ──
    bool callExport_ii(const QString &func, int32_t arg1, int32_t arg2, int32_t *outResult = nullptr)
    {
        return callExportWithArgs(func, { arg1, arg2 }, outResult);
    }

    // ── 从 WASM 线性内存读取数据 ──
    QByteArray readMemory(uint32_t offset, uint32_t len)
    {
        QJsonObject req;
        req["type"] = "read_memory";
        req["offset"] = (int)offset;
        req["len"] = (int)len;

        QJsonObject resp = sendAndProcess(req);
        if (resp.isEmpty() || resp.contains("error")) {
            qWarning() << "[WasmProcessBridge] readMemory failed:" << resp.value("error").toString();
            return {};
        }
        QString hex = resp.value("hex").toString();
        return QByteArray::fromHex(hex.toUtf8());
    }

    // ── 向 WASM 线性内存写入数据 ──
    bool writeMemory(uint32_t offset, const QByteArray &data)
    {
        QJsonObject req;
        req["type"] = "write_memory";
        req["offset"] = (int)offset;
        req["hex"] = QString::fromUtf8(data.toHex());

        QJsonObject resp = sendAndProcess(req);
        return !resp.isEmpty() && !resp.contains("error");
    }

    // ── 便捷方法：写入字符串到 WASM 内存 ──
    bool writeString(uint32_t offset, const QString &str)
    {
        return writeMemory(offset, str.toUtf8());
    }

    // ── 便捷方法：从 WASM 内存读取字符串 ──
    QString readString(uint32_t offset, uint32_t len)
    {
        return QString::fromUtf8(readMemory(offset, len));
    }

    // ── 便捷方法：调用 WASM alloc 导出 ──
    uint32_t alloc(uint32_t size)
    {
        int32_t ptr = 0;
        if (!callExport_i32("alloc", (int32_t)size, &ptr)) return 0;
        return (uint32_t)ptr;
    }

    // ── 检查子进程是否存活 ──
    bool isAlive() const
    {
        return m_process && m_process->state() == QProcess::Running;
    }

private:
    QProcess *m_process;

    // ── 等待 stderr 上的一行（就绪信号） ──
    void waitForReadyLine(int timeoutMs)
    {
        if (m_process->canReadLine()) return;
        m_process->waitForReadyRead(timeoutMs);
    }

    // ── 发送请求并处理响应（可能包含多个 import 交互） ──
    QJsonObject sendAndProcess(const QJsonObject &request)
    {
        if (!isAlive()) {
            qWarning() << "[WasmProcessBridge] Process not alive";
            return {};
        }

        QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact);
        line.append('\n');
        m_process->write(line);
        if (!m_process->waitForBytesWritten(2000)) {
            qWarning() << "[WasmProcessBridge] Timeout writing to process";
            return {};
        }

        // 读取响应 — 可能先收到若干 import 请求，最后收到 call_result
        while (true) {
            QByteArray respLine = readLineSync(10000);
            if (respLine.isEmpty()) {
                qWarning() << "[WasmProcessBridge] Timeout or EOF reading response";
                // 打印 stderr 帮助调试
                m_process->setReadChannel(QProcess::StandardError);
                QByteArray stderrData = m_process->readAll();
                if (!stderrData.isEmpty())
                    qWarning() << "[WasmProcessBridge] stderr:" << stderrData;
                m_process->setReadChannel(QProcess::StandardOutput);
                return {};
            }

            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(respLine, &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                qWarning() << "[WasmProcessBridge] JSON parse error:" << parseError.errorString()
                           << "line:" << respLine;
                continue;
            }

            QJsonObject msg = doc.object();
            QString type = msg.value("type").toString();

            if (type == "call_result") {
                return msg;
            } else if (type == "import") {
                QString func = msg.value("func").toString();
                QJsonArray args = msg.value("args").toArray();
                int32_t a1 = args.size() > 0 ? args[0].toInt() : 0;
                int32_t a2 = args.size() > 1 ? args[1].toInt() : 0;
                int32_t a3 = args.size() > 2 ? args[2].toInt() : 0;
                QString str = msg.value("str").toString();

                QString outResultStr;
                int32_t result = 0;
                if (s_hostCallback) {
                    result = s_hostCallback(func, a1, a2, a3, str, outResultStr);
                }

                QJsonObject importResult;
                importResult["type"] = "import_result";
                importResult["value"] = (int)result;
                if (!outResultStr.isEmpty()) {
                    importResult["str"] = outResultStr;
                }
                QByteArray resultLine = QJsonDocument(importResult).toJson(QJsonDocument::Compact);
                resultLine.append('\n');
                m_process->write(resultLine);
                m_process->waitForBytesWritten(2000);
            } else if (type == "error") {
                qWarning() << "[WasmProcessBridge] Plugin error:" << msg.value("error").toString();
                return msg;
            } else {
                qWarning() << "[WasmProcessBridge] Unknown message type:" << type;
            }
        }
    }

    // ── 调用 WASM 导出函数（通用） ──
    bool callExportWithArgs(const QString &func, const QJsonArray &args, int32_t *outResult)
    {
        QJsonObject req;
        req["type"] = "call";
        req["func"] = func;
        req["args"] = args;

        QJsonObject resp = sendAndProcess(req);
        if (resp.isEmpty() || resp.contains("error")) {
            qWarning() << "[WasmProcessBridge] call" << func << "failed:" << resp.value("error").toString();
            return false;
        }
        if (outResult) {
            *outResult = (int32_t)resp.value("value").toInt();
        }
        return true;
    }

    // ── 同步读取子进程 stdout 的一行 ──
    QByteArray readLineSync(int timeoutMs)
    {
        // 先检查缓冲区
        QByteArray result = m_process->readLine();
        if (!result.isEmpty() && result.endsWith('\n')) {
            return result.trimmed();
        }

        // 等待更多数据
        qint64 endTime = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
        QByteArray accum = result;
        while (accum.indexOf('\n') < 0) {
            qint64 remaining = endTime - QDateTime::currentMSecsSinceEpoch();
            if (remaining <= 0) return {};
            if (!m_process->waitForReadyRead(remaining)) return {};
            accum += m_process->readAll();
        }
        int nl = accum.indexOf('\n');
        return accum.left(nl).trimmed();
    }
};

#endif // WASMPROCESSBRIDGE_H
