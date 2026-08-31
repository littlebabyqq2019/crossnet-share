#include "ui/main_window.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QSqlDatabase>
#include <QStandardPaths>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 创建调试日志文件
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/startup.log";
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QFile logFile(logPath);
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream log(&logFile);
        log << "\n=== Client Started: " << QDateTime::currentDateTime().toString(Qt::ISODate) << " ===\n";
        
        // 添加插件搜索路径
        QString appDir = QCoreApplication::applicationDirPath();
        QCoreApplication::addLibraryPath(appDir);
        QCoreApplication::addLibraryPath(appDir + "/plugins");
        
        log << "App directory: " << appDir << "\n";
        log << "Plugin paths:\n";
        for (const QString& path : QCoreApplication::libraryPaths()) {
            log << "  - " << path << "\n";
        }
        
        log << "Available SQL drivers: " << QSqlDatabase::drivers().join(", ") << "\n";
        
        // 检查文件是否存在
        QStringList driverPaths;
        driverPaths << appDir + "/sqldrivers/qsqlite.dll";
        driverPaths << appDir + "/plugins/sqldrivers/qsqlite.dll";
        
        log << "Checking for qsqlite.dll:\n";
        for (const QString& path : driverPaths) {
            log << "  " << path << ": " << (QFile::exists(path) ? "EXISTS" : "NOT FOUND") << "\n";
        }
        
        logFile.close();
    }

    CrossNetShare::MainWindow window;
    window.show();

    return app.exec();
}
