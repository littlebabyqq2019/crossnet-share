// 最小索引器测试程序
#include "client/file_indexer.h"
#include <QCoreApplication>
#include <QDebug>
#include <QSqlDatabase>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "=== Indexer Test Start ===";
    qDebug() << "Available SQL drivers:" << QSqlDatabase::drivers();
    
    CrossNetShare::FileIndexer indexer;
    
    QString testPath = "C:/Users/asusu/Desktop/1212";
    QString dbPath = "C:/Users/asusu/AppData/Roaming/CrossNetShareClient/test_index.db";
    
    qDebug() << "Testing with:";
    qDebug() << "  Share path:" << testPath;
    qDebug() << "  DB path:" << dbPath;
    
    if (indexer.initialize(testPath, dbPath)) {
        qDebug() << "SUCCESS: Indexer initialized";
    } else {
        qDebug() << "FAILED: Indexer initialization failed";
    }
    
    return 0;
}
