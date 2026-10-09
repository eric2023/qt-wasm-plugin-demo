#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QEventLoop>

/**
 * test_e2e.cpp — End-to-end test client.
 *
 * Connects to the server, sends a message, and prints the reply.
 * This verifies the full chain: client → TCP → server → WASM plugin → reply.
 *
 * Usage: ./test_e2e [host] [port]
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString host = argc > 1 ? argv[1] : "127.0.0.1";
    quint16 port = argc > 2 ? QString(argv[2]).toUShort() : 12345;

    QTextStream out(stdout);
    out << "=== WASM Chat E2E Test ===" << Qt::endl;
    out << "Connecting to " << host << ":" << port << "..." << Qt::endl;

    QTcpSocket socket;
    QEventLoop loop;

    // Track state
    int messagesSent = 0;
    const QStringList testMessages = {
        "Hello WASM Server!",
        "Tell me about WebAssembly",
        "How does Qt work with WASM?",
        "What is linear memory?",
        "Goodbye!"
    };

    QObject::connect(&socket, &QTcpSocket::connected, [&]() {
        out << "[OK] Connected to server!" << Qt::endl;
        // Send first message
        if (messagesSent < testMessages.size()) {
            QJsonObject msg;
            msg["text"] = testMessages[messagesSent];
            QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
            data.append('\n');
            socket.write(data);
            socket.flush();
            out << "\n[SENT] " << testMessages[messagesSent] << Qt::endl;
            messagesSent++;
        }
    });

    QObject::connect(&socket, &QTcpSocket::readyRead, [&]() {
        QByteArray data = socket.readAll();
        QString dataStr = QString::fromUtf8(data).trimmed();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(dataStr.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            out << "[RECV] (raw): " << dataStr << Qt::endl;
        } else {
            QJsonObject obj = doc.object();
            QString reply = obj.value("text").toString();
            QString echo = obj.value("echo").toString();
            int seed = obj.value("seed").toInt();
            out << "[RECV] Echo: " << echo << Qt::endl;
            out << "[RECV] Reply: " << reply << Qt::endl;
            out << "[RECV] Seed: " << seed << Qt::endl;
        }

        // Send next message or quit
        if (messagesSent < testMessages.size()) {
            QTimer::singleShot(500, [&]() {
                QJsonObject msg;
                msg["text"] = testMessages[messagesSent];
                QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
                data.append('\n');
                socket.write(data);
                socket.flush();
                out << "\n[SENT] " << testMessages[messagesSent] << Qt::endl;
                messagesSent++;
            });
        } else {
            QTimer::singleShot(500, [&]() {
                out << "\n=== All messages sent! ===" << Qt::endl;
                out << "=== E2E Test PASSED ===" << Qt::endl;
                socket.disconnectFromHost();
                app.quit();
            });
        }
    });

    QObject::connect(&socket, &QTcpSocket::disconnected, [&]() {
        out << "[INFO] Disconnected" << Qt::endl;
        app.quit();
    });

    QObject::connect(&socket, &QTcpSocket::errorOccurred, [&](QAbstractSocket::SocketError) {
        out << "[ERROR] " << socket.errorString() << Qt::endl;
        app.quit();
    });

    socket.connectToHost(host, port);

    // Timeout after 30 seconds
    QTimer::singleShot(30000, [&]() {
        out << "[TIMEOUT] Test timed out!" << Qt::endl;
        app.quit();
    });

    return app.exec();
}
