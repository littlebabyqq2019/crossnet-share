#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>
#include <QMutex>

namespace CrossNetShare {

// 索引配置
struct IndexConfig {
    bool enabled = true;                    // 是否启用索引
    bool realtimeMonitoring = true;         // 实时监控
    int scanIntervalMinutes = 60;           // 定时扫描间隔（分钟）
    
    QStringList includedExtensions = {      // 包含的文件类型
        "txt", "pdf", "doc", "docx"
    };
    
    QStringList excludedPatterns = {        // 排除的模式
        "~$*", "*.tmp", "temp/*"
    };
    
    int maxFileSizeMB = 50;                 // 最大文件大小（MB）
    int maxContentChars = 1000000;          // 最大索引内容字符数
};

// 索引统计信息
struct IndexStats {
    int totalFiles = 0;                     // 已索引文件数
    qint64 indexSizeMB = 0;                // 索引大小（MB）
    QDateTime lastUpdateTime;               // 最后更新时间
    int pendingFiles = 0;                   // 待索引文件数
    bool isIndexing = false;                // 是否正在索引
};

// 文件索引器
class FileIndexer : public QObject {
    Q_OBJECT

public:
    explicit FileIndexer(QObject* parent = nullptr);
    ~FileIndexer();

    // 初始化索引器
    bool initialize(const QString& sharedPath, const QString& dbPath);
    
    // 启动/停止索引服务
    void start();
    void stop();
    
    // 配置管理
    void setConfig(const IndexConfig& config);
    IndexConfig getConfig() const { return config_; }
    
    // 索引操作
    void rebuildIndex();                    // 重建全部索引
    void clearIndex();                      // 清除索引
    void updateFileIndex(const QString& filePath);  // 更新单个文件索引
    
    // 搜索功能
    QStringList search(const QString& query, const QStringList& fileTypes = QStringList());
    
    // 统计信息
    IndexStats getStats() const;
    
    // 是否应该索引该文件
    bool shouldIndexFile(const QString& filePath) const;

signals:
    void indexingStarted();
    void indexingProgress(int current, int total);
    void indexingFinished();
    void indexingError(const QString& error);
    void fileIndexed(const QString& filePath);

private slots:
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);
    void onScanTimerTimeout();
    void processIndexQueue();

private:
    // 数据库操作
    bool initializeDatabase();
    bool createTables();
    
    // 文本提取
    QString extractText(const QString& filePath);
    QString extractTextFromTxt(const QString& filePath);
    QString extractTextFromPdf(const QString& filePath);
    QString extractTextFromWord(const QString& filePath);
    
    // 索引操作
    void indexFile(const QString& filePath);
    void removeFileFromIndex(const QString& filePath);
    bool isFileIndexed(const QString& filePath, const QString& hash) const;
    QString calculateFileHash(const QString& filePath) const;
    
    // 文件扫描
    void scanDirectory(const QString& dirPath);
    void addToIndexQueue(const QString& filePath);
    
    // FTS5 查询构建
    QString buildFTS5Query(const QString& userQuery) const;
    
    // 工具函数
    bool matchesPattern(const QString& filePath, const QStringList& patterns) const;
    QString getFileExtension(const QString& filePath) const;

private:
    QString sharedPath_;                    // 共享目录路径
    QString dbPath_;                        // 数据库路径
    QSqlDatabase db_;                       // 索引数据库
    
    IndexConfig config_;                    // 索引配置
    IndexStats stats_;                      // 统计信息
    
    QFileSystemWatcher* fileWatcher_;       // 文件系统监控器
    QTimer* scanTimer_;                     // 定时扫描计时器
    QTimer* queueTimer_;                    // 队列处理计时器
    
    QSet<QString> indexQueue_;              // 待索引文件队列
    QMutex queueMutex_;                     // 队列互斥锁
    
    bool isRunning_;                        // 是否正在运行
    bool isIndexing_;                       // 是否正在索引
};

} // namespace CrossNetShare
