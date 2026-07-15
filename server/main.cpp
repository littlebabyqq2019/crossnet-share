#include "ui/main_window.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    CrossNetShare::MainWindow window;
    window.show();

    return app.exec();
}
