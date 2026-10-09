#include <DApplication>
#include <DGuiApplicationHelper>
#include <QContextMenuEvent>
#include "DtkChatClient.h"

DGUI_USE_NAMESPACE
DWIDGET_USE_NAMESPACE

/**
 * DTK Client entry point.
 *
 * This application demonstrates that Qt/DTK interfaces can serve as hosts
 * for WASM plugins. The entire chat UI layout is defined by a WASM plugin
 * (chat_ui_dtk.wasm), which imports DTK host functions to create widgets.
 *
 * The application provides:
 *   - Right-click context menu: Copy, Paste, Save As, Theme Toggle
 *   - Backend WASM plugin (random_reply.wasm) on the server for random replies
 */
int main(int argc, char *argv[])
{
    DGuiApplicationHelper::setAttribute(DGuiApplicationHelper::UseInactiveColorGroup, true);

    DApplication app(argc, argv);
    app.setApplicationName("DtkWasmChatClient");
    app.setApplicationVersion("1.0");
    app.setProductName("WASM Chat (DTK)");
    app.setWindowIcon(QIcon::fromTheme("dialog-information"));

    // Enable DDE platform integration
    app.setApplicationDisplayName("WASM Chat (DTK)");

    DtkChatClient client;
    client.show();

    return app.exec();
}
