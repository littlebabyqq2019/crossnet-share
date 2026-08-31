#include "file_indexer.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QSqlQuery>
#include <QSqlError>
#include <QTextStream>
#include <QTextCodec>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent>

namespace CrossNetShare {

FileIndexer::FileIndexer(QObject* parent)
    : QObject(parent)
    , fileWatcher_(nullptr)
    , scanTimer_(nullptr)
    , queueTimer_(nullptr)
    , isRunning_(false)
    , isIndexing_(false)
{
    fileWatcher_ = new QFileSystemWatcher(this);
    scanTimer_ = new QTimer(this);
    queueTimer_ = new QTimer(this);
    
    connect(fileWatcher_, &QFileSystemWatcher::fileChanged,
            this, &FileIndexer::onFileChanged);
    connect(fileWatcher_, &QFileSystemWatcher::directoryChanged,
            this, &FileIndexer::onDirectoryChanged);
    
    connect(scanTimer_, &QTimer::timeout,
            this, &FileIndexer::onScanTimerTimeout);
    
    connect(queueTimer_, &QTimer::timeout,
            this, &FileIndexer::processIndexQueue);
    
    // 默认每5秒处理一次索引队列
    queueTimer_->setInterval(5000);
}

FileIndexer::~FileIndexer() {
    stop();
    if (db_.isOpen()) {
        db_.close();
    }
}

bool FileIndexer::initialize(const QString& sharedPath, const QString& dbPath) {
    sharedPath_ = sharedPath;
    dbPath_ = dbPath;
    
    qDebug() << "[FileIndexer] Initializing with shared path:" << sharedPath;
    qDebug() << "[FileIndexer] Database path:" << dbPath;
    
    if (!initializeDatabase()) {
        qWarning() << "[FileIndexer] Failed to initialize index database";
        return false;
    }
    
    qDebug() << "[FileIndexer] Successfully initialized for path:" << sharedPath_;
    return true;
}

bool FileIndexer::initializeDatabase() {
    qDebug() << "[FileIndexer] Checking for existing database connection...";
    
    // 如果已经有连接，先移除
    if (QSqlDatabase::contains("index_db")) {
        qDebug() << "[FileIndexer] Removing existing database connection";
        QSqlDatabase::removeDatabase("index_db");
    }
    
    qDebug() << "[FileIndexer] Creating new database connection";
    qDebug() << "[FileIndexer] Available SQL drivers:" << QSqlDatabase::drivers();
    
    // 创建数据库连接
    db_ = QSqlDatabase::addDatabase("QSQLITE", "index_db");
    db_.setDatabaseName(dbPath_);
    
    qDebug() << "[FileIndexer] Opening database:" << dbPath_;
    
    if (!db_.open()) {
        qWarning() << "[FileIndexer] Failed to open index database:" << db_.lastError().text();
        qWarning() << "[FileIndexer] Database path:" << dbPath_;
        qWarning() << "[FileIndexer] Database driver valid:" << db_.driver();
        return false;
    }
    
    qDebug() << "[FileIndexer] Database opened successfully, creating tables...";
    
    bool tablesCreated = createTables();
    if (!tablesCreated) {
        qWarning() << "[FileIndexer] Failed to create tables";
    } else {
        qDebug() << "[FileIndexer] Tables created successfully";
    }
    
    return tablesCreated;
}

bool FileIndexer::createTables() {
    QSqlQuery query(db_);
    
    qDebug() << "[FileIndexer] Creating files table...";
    
    // 创建文件元数据表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS files ("
        "  file_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  file_path TEXT UNIQUE NOT NULL,"
        "  file_name TEXT NOT NULL,"
        "  file_size INTEGER,"
        "  file_type TEXT,"
        "  modified_time INTEGER,"
        "  indexed_time INTEGER,"
        "  content_hash TEXT"
        ")"
    )) {
        qWarning() << "[FileIndexer] Failed to create files table:" << query.lastError().text();
        return false;
    }
    
    qDebug() << "[FileIndexer] Files table created, creating FTS5 table...";
    
    // 创建 FTS5 全文搜索表
    if (!query.exec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5("
        "  file_id UNINDEXED,"
        "  file_name,"
        "  content,"
        "  tokenize='unicode61 remove_diacritics 2'"
        ")"
    )) {
        qWarning() << "[FileIndexer] Failed to create FTS5 table:" << query.lastError().text();
        qWarning() << "[FileIndexer] This may indicate FTS5 is not available in your SQLite build";
        qWarning() << "[FileIndexer] Try checking SQLite version and FTS5 support";
        return false;
    }
    
    qDebug() << "[FileIndexer] FTS5 table created, creating config table...";
    
    // 创建配置表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS index_config ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ")"
    )) {
        qWarning() << "Failed to create config table:" << query.lastError().text();
        return false;
    }
    
    // 创建索引
    query.exec("CREATE INDEX IF NOT EXISTS idx_file_path ON files(file_path)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_file_type ON files(file_type)");
    
    qDebug() << "Index database tables created successfully";
    return true;
}

void FileIndexer::start() {
    if (isRunning_) {
        return;
    }
    
    isRunning_ = true;
    
    // 启动文件系统监控
    if (config_.realtimeMonitoring) {
        fileWatcher_->addPath(sharedPath_);
        qDebug() << "File system watcher started for:" << sharedPath_;
    }
    
    // 启动定时扫描
    if (config_.scanIntervalMinutes > 0) {
        scanTimer_->start(config_.scanIntervalMinutes * 60 * 1000);
        qDebug() << "Scheduled scan started, interval:" << config_.scanIntervalMinutes << "minutes";
    }
    
    // 启动队列处理
    queueTimer_->start();
    
    qDebug() << "FileIndexer started";
}

void FileIndexer::stop() {
    if (!isRunning_) {
        return;
    }
    
    isRunning_ = false;
    
    fileWatcher_->removePaths(fileWatcher_->files());
    fileWatcher_->removePaths(fileWatcher_->directories());
    
    scanTimer_->stop();
    queueTimer_->stop();
    
    qDebug() << "FileIndexer stopped";
}

void FileIndexer::setConfig(const IndexConfig& config) {
    config_ = config;
    
    // 更新定时扫描间隔
    if (isRunning_ && config_.scanIntervalMinutes > 0) {
        scanTimer_->setInterval(config_.scanIntervalMinutes * 60 * 1000);
    }
}

void FileIndexer::rebuildIndex() {
    if (isIndexing_) {
        qWarning() << "Already indexing, please wait";
        return;
    }
    
    qDebug() << "Starting full index rebuild...";
    
    isIndexing_ = true;
    emit indexingStarted();
    
    // 异步执行索引重建
    QtConcurrent::run([this]() {
        // 清空现有索引
        QSqlQuery query(db_);
        db_.transaction();
        query.exec("DELETE FROM files");
        query.exec("DELETE FROM files_fts");
        db_.commit();
        
        // 扫描所有文件
        QStringList filters;
        for (const QString& ext : config_.includedExtensions) {
            filters << "*." + ext;
        }
        
        QDirIterator it(sharedPath_, filters, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        
        QStringList filesToIndex;
        while (it.hasNext()) {
            QString filePath = it.next();
            if (shouldIndexFile(filePath)) {
                filesToIndex << filePath;
            }
        }
        
        int total = filesToIndex.size();
        int current = 0;
        
        qDebug() << "Found" << total << "files to index";
        
        // 批量索引
        db_.transaction();
        for (const QString& filePath : filesToIndex) {
            indexFile(filePath);
            current++;
            
            if (current % 10 == 0) {
                emit indexingProgress(current, total);
            }
            
            if (current % 100 == 0) {
                db_.commit();
                db_.transaction();
            }
        }
        db_.commit();
        
        isIndexing_ = false;
        
        // 更新统计信息
        stats_.totalFiles = total;
        stats_.lastUpdateTime = QDateTime::currentDateTime();
        
        qDebug() << "Index rebuild completed," << total << "files indexed";
        emit indexingFinished();
    });
}

void FileIndexer::clearIndex() {
    QSqlQuery query(db_);
    db_.transaction();
    query.exec("DELETE FROM files");
    query.exec("DELETE FROM files_fts");
    db_.commit();
    
    stats_.totalFiles = 0;
    stats_.indexSizeMB = 0;
    
    qDebug() << "Index cleared";
}

void FileIndexer::indexFile(const QString& filePath) {
    if (!shouldIndexFile(filePath)) {
        return;
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        removeFileFromIndex(filePath);
        return;
    }
    
    // 检查文件大小
    qint64 fileSizeMB = fileInfo.size() / (1024 * 1024);
    if (fileSizeMB > config_.maxFileSizeMB) {
        qDebug() << "File too large, skipping:" << filePath << "(" << fileSizeMB << "MB)";
        return;
    }
    
    // 计算文件哈希
    QString hash = calculateFileHash(filePath);
    
    // 检查是否已索引且未修改
    if (isFileIndexed(filePath, hash)) {
        return;
    }
    
    // 提取文本内容
    QString content = extractText(filePath);
    if (content.isEmpty()) {
        qDebug() << "No content extracted from:" << filePath;
        return;
    }
    
    // 限制内容大小
    if (content.length() > config_.maxContentChars) {
        content = content.left(config_.maxContentChars);
    }
    
    // 插入或更新文件元数据
    QSqlQuery query(db_);
    query.prepare(
        "INSERT OR REPLACE INTO files "
        "(file_path, file_name, file_size, file_type, modified_time, indexed_time, content_hash) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"
    );
    query.addBindValue(filePath);
    query.addBindValue(fileInfo.fileName());
    query.addBindValue(fileInfo.size());
    query.addBindValue(fileInfo.suffix().toLower());
    query.addBindValue(fileInfo.lastModified().toSecsSinceEpoch());
    query.addBindValue(QDateTime::currentDateTime().toSecsSinceEpoch());
    query.addBindValue(hash);
    
    if (!query.exec()) {
        qWarning() << "Failed to insert file metadata:" << query.lastError().text();
        return;
    }
    
    // 获取文件ID
    qint64 fileId = query.lastInsertId().toLongLong();
    
    // 插入或更新FTS索引
    query.prepare(
        "INSERT OR REPLACE INTO files_fts (file_id, file_name, content) "
        "VALUES (?, ?, ?)"
    );
    query.addBindValue(fileId);
    query.addBindValue(fileInfo.fileName());
    query.addBindValue(content);
    
    if (!query.exec()) {
        qWarning() << "Failed to insert FTS index:" << query.lastError().text();
        return;
    }
    
    qDebug() << "Indexed file:" << filePath;
    emit fileIndexed(filePath);
}

void FileIndexer::removeFileFromIndex(const QString& filePath) {
    QSqlQuery query(db_);
    
    // 获取文件ID
    query.prepare("SELECT file_id FROM files WHERE file_path = ?");
    query.addBindValue(filePath);
    
    if (query.exec() && query.next()) {
        qint64 fileId = query.value(0).toLongLong();
        
        // 删除FTS索引
        query.prepare("DELETE FROM files_fts WHERE file_id = ?");
        query.addBindValue(fileId);
        query.exec();
        
        // 删除文件元数据
        query.prepare("DELETE FROM files WHERE file_id = ?");
        query.addBindValue(fileId);
        query.exec();
        
        qDebug() << "Removed from index:" << filePath;
    }
}

bool FileIndexer::isFileIndexed(const QString& filePath, const QString& hash) const {
    QSqlQuery query(db_);
    query.prepare("SELECT content_hash FROM files WHERE file_path = ?");
    query.addBindValue(filePath);
    
    if (query.exec() && query.next()) {
        QString storedHash = query.value(0).toString();
        return storedHash == hash;
    }
    
    return false;
}

QString FileIndexer::calculateFileHash(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    
    QFileInfo fileInfo(filePath);
    
    // 简化的哈希：文件路径 + 大小 + 修改时间
    QString hashInput = filePath +
                       QString::number(fileInfo.size()) +
                       fileInfo.lastModified().toString(Qt::ISODate);
    
    return QString::fromUtf8(
        QCryptographicHash::hash(hashInput.toUtf8(), QCryptographicHash::Md5).toHex()
    );
}

QString FileIndexer::extractText(const QString& filePath) {
    QString ext = getFileExtension(filePath).toLower();
    
    if (ext == "txt") {
        return extractTextFromTxt(filePath);
    } else if (ext == "pdf") {
        return extractTextFromPdf(filePath);
    } else if (ext == "doc" || ext == "docx") {
        return extractTextFromWord(filePath);
    }
    
    return QString();
}

QString FileIndexer::extractTextFromTxt(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file:" << filePath;
        return QString();
    }
    
    QByteArray content = file.readAll();
    
    // 尝试 UTF-8 解码
    QString text = QString::fromUtf8(content);
    
    // 如果包含大量替换字符，尝试 GBK
    if (text.count(QChar::ReplacementCharacter) > content.size() / 10) {
        QTextCodec* gbkCodec = QTextCodec::codecForName("GBK");
        if (gbkCodec) {
            text = gbkCodec->toUnicode(content);
        }
    }
    
    return text;
}

QString FileIndexer::extractTextFromPdf(const QString& filePath) {
    // 查找Python解释器
    QString python = QStandardPaths::findExecutable("python");
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable("python3");
    }
    
    if (python.isEmpty()) {
        qWarning() << "Python not found, cannot extract PDF text";
        return QString();
    }
    
    // 查找提取脚本
    QString scriptPath = QCoreApplication::applicationDirPath() + "/extract_pdf_text.py";
    if (!QFileInfo::exists(scriptPath)) {
        qWarning() << "PDF extraction script not found:" << scriptPath;
        return QString();
    }
    
    // 调用Python脚本提取文本
    QProcess process;
    process.start(python, QStringList() << scriptPath << filePath);
    
    if (!process.waitForStarted(10000)) {
        qWarning() << "Failed to start PDF extraction:" << process.errorString();
        return QString();
    }
    
    if (!process.waitForFinished(60000)) {
        process.kill();
        qWarning() << "PDF extraction timed out:" << filePath;
        return QString();
    }
    
    if (process.exitCode() != 0) {
        QString error = QString::fromLocal8Bit(process.readAllStandardError());
        qWarning() << "PDF extraction failed:" << error;
        return QString();
    }
    
    QString text = QString::fromUtf8(process.readAllStandardOutput());
    
    // 输出调试信息
    QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError());
    if (!stderr_output.isEmpty()) {
        qDebug() << "PDF extraction output:" << stderr_output;
    }
    
    return text;
}

QString FileIndexer::extractTextFromWord(const QString& filePath) {
    // 查找Python解释器
    QString python = QStandardPaths::findExecutable("python");
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable("python3");
    }
    
    if (python.isEmpty()) {
        qWarning() << "Python not found, cannot extract Word text";
        return QString();
    }
    
    // 查找提取脚本
    QString scriptPath = QCoreApplication::applicationDirPath() + "/extract_word_text.py";
    if (!QFileInfo::exists(scriptPath)) {
        qWarning() << "Word extraction script not found:" << scriptPath;
        return QString();
    }
    
    // 调用Python脚本提取文本
    QProcess process;
    process.start(python, QStringList() << scriptPath << filePath);
    
    if (!process.waitForStarted(10000)) {
        qWarning() << "Failed to start Word extraction:" << process.errorString();
        return QString();
    }
    
    if (!process.waitForFinished(60000)) {
        process.kill();
        qWarning() << "Word extraction timed out:" << filePath;
        return QString();
    }
    
    if (process.exitCode() != 0) {
        QString error = QString::fromLocal8Bit(process.readAllStandardError());
        qWarning() << "Word extraction failed:" << error;
        return QString();
    }
    
    QString text = QString::fromUtf8(process.readAllStandardOutput());
    
    // 输出调试信息
    QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError());
    if (!stderr_output.isEmpty()) {
        qDebug() << "Word extraction output:" << stderr_output;
    }
    
    return text;
}

QStringList FileIndexer::search(const QString& query, const QStringList& fileTypes) {
    if (query.trimmed().isEmpty()) {
        return QStringList();
    }
    
    QString ftsQuery = buildFTS5Query(query);
    
    QSqlQuery sqlQuery(db_);
    QString sql = 
        "SELECT DISTINCT f.file_path "
        "FROM files f "
        "JOIN files_fts fts ON f.file_id = fts.file_id "
        "WHERE files_fts MATCH ?";
    
    // 添加文件类型过滤
    if (!fileTypes.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < fileTypes.size(); ++i) {
            placeholders << "?";
        }
        sql += " AND f.file_type IN (" + placeholders.join(", ") + ")";
    }
    
    sql += " ORDER BY rank LIMIT 1000";
    
    sqlQuery.prepare(sql);
    sqlQuery.addBindValue(ftsQuery);
    
    for (const QString& type : fileTypes) {
        sqlQuery.addBindValue(type);
    }
    
    QStringList results;
    if (sqlQuery.exec()) {
        while (sqlQuery.next()) {
            results << sqlQuery.value(0).toString();
        }
    } else {
        qWarning() << "Search query failed:" << sqlQuery.lastError().text();
    }
    
    qDebug() << "Search for" << query << "found" << results.size() << "results";
    return results;
}

QString FileIndexer::buildFTS5Query(const QString& userQuery) const {
    QString query = userQuery.trimmed();
    
    // 简单的查询转换
    // AND -> 空格（FTS5默认是AND）
    query.replace(" AND ", " ", Qt::CaseInsensitive);
    query.replace(" and ", " ");
    
    // OR 保持原样（FTS5支持）
    query.replace(" or ", " OR ");
    
    // NOT -> NOT（FTS5支持）
    query.replace(" not ", " NOT ");
    
    return query;
}

IndexStats FileIndexer::getStats() {
    IndexStats stats = stats_;
    
    // 更新统计信息
    QSqlQuery query(db_);
    
    // 文件总数
    if (query.exec("SELECT COUNT(*) FROM files")) {
        if (query.next()) {
            stats.totalFiles = query.value(0).toInt();
        }
    }
    
    // 数据库大小
    QFileInfo dbInfo(dbPath_);
    if (dbInfo.exists()) {
        stats.indexSizeMB = dbInfo.size() / (1024 * 1024);
    }
    
    // 待索引文件数
    QMutexLocker locker(&queueMutex_);
    stats.pendingFiles = indexQueue_.size();
    stats.isIndexing = isIndexing_;
    
    return stats;
}

bool FileIndexer::shouldIndexFile(const QString& filePath) const {
    QFileInfo fileInfo(filePath);
    
    // 检查文件是否存在
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }
    
    // 检查文件扩展名
    QString ext = getFileExtension(filePath).toLower();
    if (!config_.includedExtensions.contains(ext)) {
        return false;
    }
    
    // 检查排除模式
    if (matchesPattern(filePath, config_.excludedPatterns)) {
        return false;
    }
    
    return true;
}

bool FileIndexer::matchesPattern(const QString& filePath, const QStringList& patterns) const {
    for (const QString& pattern : patterns) {
        QRegExp regex(pattern, Qt::CaseInsensitive, QRegExp::Wildcard);
        if (regex.exactMatch(filePath) || regex.exactMatch(QFileInfo(filePath).fileName())) {
            return true;
        }
    }
    return false;
}

QString FileIndexer::getFileExtension(const QString& filePath) const {
    return QFileInfo(filePath).suffix();
}

void FileIndexer::updateFileIndex(const QString& filePath) {
    addToIndexQueue(filePath);
}

void FileIndexer::addToIndexQueue(const QString& filePath) {
    QMutexLocker locker(&queueMutex_);
    indexQueue_.insert(filePath);
}

void FileIndexer::processIndexQueue() {
    if (isIndexing_) {
        return;  // 正在批量索引，跳过
    }
    
    QMutexLocker locker(&queueMutex_);
    
    if (indexQueue_.isEmpty()) {
        return;
    }
    
    // 每次处理最多10个文件
    int count = 0;
    auto it = indexQueue_.begin();
    while (it != indexQueue_.end() && count < 10) {
        QString filePath = *it;
        it = indexQueue_.erase(it);
        
        locker.unlock();
        indexFile(filePath);
        locker.relock();
        
        count++;
    }
}

void FileIndexer::onFileChanged(const QString& path) {
    qDebug() << "File changed:" << path;
    updateFileIndex(path);
}

void FileIndexer::onDirectoryChanged(const QString& path) {
    qDebug() << "Directory changed:" << path;
    
    // 扫描目录，检测新增/删除的文件
    QtConcurrent::run([this, path]() {
        scanDirectory(path);
    });
}

void FileIndexer::scanDirectory(const QString& dirPath) {
    // 获取当前目录中所有应该索引的文件
    QStringList filters;
    for (const QString& ext : config_.includedExtensions) {
        filters << "*." + ext;
    }
    
    QDirIterator it(dirPath, filters, QDir::Files,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    
    QSet<QString> currentFiles;
    while (it.hasNext()) {
        QString filePath = it.next();
        if (shouldIndexFile(filePath)) {
            currentFiles.insert(filePath);
            addToIndexQueue(filePath);
        }
    }
    
    // 检查已索引的文件是否仍存在
    QSqlQuery query(db_);
    query.prepare("SELECT file_path FROM files WHERE file_path LIKE ?");
    query.addBindValue(dirPath + "%");
    
    if (query.exec()) {
        while (query.next()) {
            QString indexedPath = query.value(0).toString();
            if (!currentFiles.contains(indexedPath)) {
                removeFileFromIndex(indexedPath);
            }
        }
    }
}

void FileIndexer::onScanTimerTimeout() {
    qDebug() << "Scheduled scan triggered";
    
    QtConcurrent::run([this]() {
        scanDirectory(sharedPath_);
    });
}

} // namespace CrossNetShare
