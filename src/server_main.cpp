#include <QCoreApplication>
#include <QTimer>
#include "ChatServer.h"

/**
 * Server entry point.
 *
 * Usage:  ./wasmchatserver [port]
 *
 * Starts a TCP server that loads the backend WASM plugin (random_reply.wasm)
 * and responds to client messages with random replies.
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("WasmChatServer");

    quint16 port = 12345;
    if (argc > 1) {
        bool ok = false;
        quint16 p = QString(argv[1]).toUShort(&ok);
        if (ok) port = p;
    }

    ChatServer server;
    if (!server.start(port)) {
        fprintf(stderr, "Failed to start server on port %u\n", port);
        return 1;
    }

    fprintf(stdout, "WASM Chat Server running. Press Ctrl+C to quit.\n");
    fflush(stdout);

    return app.exec();
}
