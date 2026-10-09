#ifndef WASMPLUGINLOADER_H
#define WASMPLUGINLOADER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QProcess>
#include <QEventLoop>
#include <QTimer>
#include <QFileInfo>
/**
 * WasmPluginLoader — Synchronous bridge to the Node.js WebAssembly runtime.
 *
 * Qt C++ classes call invoke() with a JSON request object. This class spawns
 * `node wasm_runner.js` as a QProcess, feeds the JSON on stdin, and reads the
 * JSON response from stdout. The call is synchronous (blocking) via QEventLoop,
 * which is acceptable for the short-lived one-shot plugin invocations in this
 * application.
 *
 * Architecture:
 *
 *   Qt C++  ──JSON──►  wasm_runner.js  ──load+exec──►  .wasm binary
 *      ◄──JSON──                          ◄──result──
 */
class WasmPluginLoader : public QObject
{
    Q_OBJECT

public:
    /**
     * @param wasmPath  Absolute or relative path to the .wasm plugin file.
     * @param runnerPath Path to wasm_runner.js (defaults to scripts/wasm_runner.js).
     * @param nodePath   Path to node executable (defaults to "node").
     * @param parent     Qt parent.
     */
    explicit WasmPluginLoader(const QString &wasmPath,
                              const QString &runnerPath = QString(),
                              const QString &nodePath = QStringLiteral("node"),
                              QObject *parent = nullptr)
        : QObject(parent)
        , m_wasmPath(wasmPath)
        , m_nodePath(nodePath)
    {
        if (runnerPath.isEmpty()) {
            // Default: look for scripts/wasm_runner.js relative to CWD
            m_runnerPath = QStringLiteral("scripts/wasm_runner.js");
        } else {
            m_runnerPath = runnerPath;
        }
    }

    /**
     * Synchronously invoke a WASM plugin action.
     *
     * @param action   The action name (e.g. "format", "generate_reply").
     * @param params   Additional parameters to include in the JSON request.
     * @param timeoutMs Maximum wait time in milliseconds.
     * @return The "result" field from the JSON response, or empty string on error.
     */
    QString invoke(const QString &action, const QJsonObject &params = {},
                   int timeoutMs = 10000)
    {
        QJsonObject request;
        request["wasm_path"] = m_wasmPath;
        request["action"] = action;
        for (auto it = params.begin(); it != params.end(); ++it) {
            request[it.key()] = it.value();
        }

        QByteArray requestData = QJsonDocument(request).toJson(QJsonDocument::Compact);

        QProcess process;
        process.setProgram(m_nodePath);
        process.setArguments({m_runnerPath});

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        // Wire up completion signals
        connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, [&]() {
            if (process.state() != QProcess::NotRunning) {
                process.kill();
            }
        });

        process.start();
        if (!process.waitForStarted(3000)) {
            return {};
        }

        process.write(requestData);
        process.closeWriteChannel();

        timer.start(timeoutMs);
        loop.exec();
        timer.stop();

        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(2000);
        }

        QByteArray output = process.readAllStandardOutput();
        QByteArray errorOutput = process.readAllStandardError();

        if (!errorOutput.isEmpty()) {
            // Log to stderr but don't fail if we got valid output
            fprintf(stderr, "[WasmPluginLoader] stderr: %s\n", errorOutput.constData());
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(output, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            fprintf(stderr, "[WasmPluginLoader] JSON parse error: %s\n",
                    parseError.errorString().toUtf8().constData());
            return {};
        }

        QJsonObject response = doc.object();
        if (!response.value("success").toBool(false)) {
            fprintf(stderr, "[WasmPluginLoader] Plugin error: %s\n",
                    response.value("error").toString().toUtf8().constData());
            return {};
        }

        return response.value("result").toString();
    }

    /**
     * Convenience: check if the loader is ready (wasm file exists).
     */
    bool isReady() const
    {
        QFileInfo fi(m_wasmPath);
        return fi.exists() && fi.isFile();
    }

private:
    QString m_wasmPath;
    QString m_runnerPath;
    QString m_nodePath;
};

#endif // WASMPLUGINLOADER_H
