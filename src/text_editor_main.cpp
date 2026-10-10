#include <DApplication>
#include <DGuiApplicationHelper>
#include "DtkTextEditor.h"

DGUI_USE_NAMESPACE
DWIDGET_USE_NAMESPACE

/**
 * DTK Text Editor entry point.
 *
 * This application demonstrates that Qt/DTK interfaces can serve as hosts
 * for WASM process plugins. The entire text editor functionality is
 * implemented in a WASM plugin (text_editor_plugin.wasm), which imports
 * DTK host functions to create widgets and perform file operations.
 *
 * The WASM plugin runs in a separate Node.js child process (process plugin).
 * If the plugin crashes, the main application continues to run and can
 * reload the plugin.
 */
int main(int argc, char *argv[])
{
    DGuiApplicationHelper::setAttribute(DGuiApplicationHelper::UseInactiveColorGroup, true);

    DApplication app(argc, argv);
    app.setApplicationName("DtkWasmTextEditor");
    app.setApplicationVersion("1.0");
    app.setProductName("WASM Text Editor (DTK)");
    app.setWindowIcon(QIcon::fromTheme("dialog-information"));
    app.setApplicationDisplayName("WASM Text Editor (DTK)");

    DtkTextEditor editor;
    editor.show();

    return app.exec();
}
