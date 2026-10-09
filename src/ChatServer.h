#ifndef CHATSERVER_H
#define CHATSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDateTime>
#include <QSet>
#include <QTimer>
#include "WasmPluginLoader.h"

/**
 * ChatServer — TCP server that receives messages from clients and replies
 * using the backend WASM plugin (random_reply.wasm).
 *
 * Protocol (line-based, newline-delimited JSON):
 *   Client → Server:  {"text": "user message"}
 *   Server → Client:  {"text": "server reply", "echo": "original message"}
 *
 * The server uses a seed based on the current timestamp + message length
 * to produce a pseudo-random reply index (seed % 10).
 */
class ChatServer : public QObject
{
    Q_OBJECT

public:
    explicit ChatServer(QObject *parent = nullptr)
        : QObject(parent)
        , m_replyCounter(0)
    {
        // Load the backend WASM plugin
        m_plugin = std::make_unique<WasmPluginLoader>(
            QStringLiteral("plugins/random_reply_plugin.wasm"),
            QStringLiteral("scripts/wasm_runner.js"));

        connect(&m_server, &QTcpServer::newConnection, this, &ChatServer::onNewConnection);

        if (!m_plugin->isReady()) {
            fprintf(stderr, "[ChatServer] WARNING: backend WASM plugin not found!\n");
        } else {
            fprintf(stdout, "[ChatServer] Backend WASM plugin loaded: plugins/random_reply_plugin.wasm\n");
        }
    }

    ~ChatServer() = default;

    /**
     * Start listening on the given port.
     */
    bool start(quint16 port = 12345)
    {
        if (!m_server.listen(QHostAddress::Any, port)) {
            fprintf(stderr, "[ChatServer] Failed to listen on port %u: %s\n",
                    port, m_server.errorString().toUtf8().constData());
            return false;
        }
        fprintf(stdout, "[ChatServer] Listening on port %u\n", port);
        return true;
    }

private slots:
    void onNewConnection()
    {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *client = m_server.nextPendingConnection();
            m_clients.insert(client);
            fprintf(stdout, "[ChatServer] Client connected: %s:%d\n",
                    client->peerAddress().toString().toUtf8().constData(),
                    client->peerPort());

            connect(client, &QTcpSocket::readyRead, this, [this, client]() {
                onReadyRead(client);
            });
            connect(client, &QTcpSocket::disconnected, this, [this, client]() {
                onClientDisconnected(client);
            });
        }
    }

    void onReadyRead(QTcpSocket *client)
    {
        QByteArray data = client->readAll();
        QString dataStr = QString::fromUtf8(data).trimmed();

        if (dataStr.isEmpty())
            return;

        fprintf(stdout, "[ChatServer] Received: %s\n", dataStr.toUtf8().constData());

        // Parse the incoming message JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(dataStr.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            fprintf(stderr, "[ChatServer] JSON parse error: %s\n",
                    parseError.errorString().toUtf8().constData());
            return;
        }

        QJsonObject msgObj = doc.object();
        QString userText = msgObj.value("text").toString();

        // Generate a seed for the WASM plugin
        // Seed = timestamp (ms) XOR counter XOR message length
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
        int seed = static_cast<int>((timestamp ^ (m_replyCounter * 2654435761u) ^
                                     userText.length()) & 0x7FFFFFFF);
        m_replyCounter++;

        // Invoke the backend WASM plugin to generate a random reply
        QString reply;
        if (m_plugin->isReady()) {
            QJsonObject params;
            params["seed"] = seed;
            reply = m_plugin->invoke("generate_reply", params);
        }

        if (reply.isEmpty()) {
            reply = QStringLiteral("(WASM plugin error - no reply generated)");
        }

        fprintf(stdout, "[ChatServer] Reply (seed=%d): %s\n",
                seed, reply.toUtf8().constData());

        // Send reply back to client
        QJsonObject replyObj;
        replyObj["text"] = reply;
        replyObj["echo"] = userText;
        replyObj["seed"] = seed;
        QByteArray replyData = QJsonDocument(replyObj).toJson(QJsonDocument::Compact);
        replyData.append('\n');

        client->write(replyData);
        client->flush();
    }

    void onClientDisconnected(QTcpSocket *client)
    {
        fprintf(stdout, "[ChatServer] Client disconnected: %s:%d\n",
                client->peerAddress().toString().toUtf8().constData(),
                client->peerPort());
        m_clients.remove(client);
        client->deleteLater();
    }

private:
    QTcpServer m_server;
    QSet<QTcpSocket *> m_clients;
    std::unique_ptr<WasmPluginLoader> m_plugin;
    quint32 m_replyCounter;
};

#endif // CHATSERVER_H
