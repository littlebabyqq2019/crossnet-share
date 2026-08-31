#include "ui/main_window.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
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
        
        // 注意：v2.0.0 使用独立的 SQLite C API，不再依赖 Qt SQL
        log << "Using independent SQLite C API with FTS5 support\n";
        
        // 检查 SQLite DLL 是否存在
        QStringList sqlitePaths;
        sqlitePaths << appDir + "/sqlite3.dll";
        sqlitePaths << appDir + "/simple.dll";
        
        log << "Checking for SQLite libraries:\n";
        for (const QString& path : sqlitePaths) {
            log << "  " << path << ": " << (QFile::exists(path) ? "EXISTS" : "NOT FOUND") << "\n";
        }
        
        logFile.close();
    }

    CrossNetShare::MainWindow window;
    window.show();

    return app.exec();
}
