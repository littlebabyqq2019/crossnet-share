#include "ui/main_window.h"
#include "document_converter.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    CrossNetShare::DocumentConverter::initialize();

    CrossNetShare::MainWindow window;
    window.show();

    int result = app.exec();

    CrossNetShare::DocumentConverter::cleanup();

    return result;
}
