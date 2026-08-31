#include "file_indexer.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QTextStream>
#include <QTextCodec>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QtConcurrent>

// SQLite C API
#include <sqlite3.h>

namespace CrossNetShare {

FileIndexer::FileIndexer(QObject* parent)
    : QObject(parent)
    , db_(nullptr)
    , fileWatcher_(nullptr)
    , scanTimer_(nullptr)
    , queueTimer_(nullptr)
    , isRunning_(false)
    , isIndexing_(false)
    , simpleLoaded_(false)
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
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool FileIndexer::initialize(const QString& sharedPath, const QString& dbPath) {
    emit logMessage("==========================================================");
    emit logMessage("=== FileIndexer::initialize() CALLED ===");
    emit logMessage("==========================================================");
    
    sharedPath_ = sharedPath;
    dbPath_ = dbPath;
    
    emit logMessage(QString("[FileIndexer] Initializing with shared path: %1").arg(sharedPath));
    emit logMessage(QString("[FileIndexer] Database path: %1").arg(dbPath));
    
    if (!initializeDatabase()) {
        emit logMessage("[FileIndexer] Failed to initialize index database");
        return false;
    }
    
    emit logMessage(QString("[FileIndexer] Successfully initialized for path: %1").arg(sharedPath_));
    return true;
}

// SQLite C API 辅助函数
bool FileIndexer::execSQL(const char* sql, QString* error) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        QString errorStr = errMsg ? QString::fromUtf8(errMsg) : "Unknown error";
        emit logMessage(QString("[FileIndexer] SQL error: %1").arg(errorStr));
        emit logMessage(QString("[FileIndexer] SQL: %1").arg(QString::fromUtf8(sql)));
        if (error) {
            *error = errorStr;
        }
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return false;
    }
    
    return true;
}

bool FileIndexer::execSQL(const QString& sql, QString* error) {
    return execSQL(sql.toUtf8().constData(), error);
}

QString FileIndexer::escapeString(const QString& str) const {
    QString escaped = str;
    escaped.replace("'", "''");
    return escaped;
}

qint64 FileIndexer::getLastInsertId() {
    return sqlite3_last_insert_rowid(db_);
}

bool FileIndexer::prepareStatement(const QString& sql, sqlite3_stmt** stmt) {
    int rc = sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, stmt, nullptr);
    if (rc != SQLITE_OK) {
        emit logMessage(QString("[FileIndexer] Failed to prepare statement: %1")
            .arg(QString::fromUtf8(sqlite3_errmsg(db_))));
        emit logMessage(QString("[FileIndexer] SQL: %1").arg(sql));
        return false;
    }
    return true;
}

void FileIndexer::finalizeStatement(sqlite3_stmt* stmt) {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

bool FileIndexer::initializeDatabase() {
    emit logMessage("[FileIndexer] Opening database with SQLite C API...");
    
    // 打开数据库
    int rc = sqlite3_open(dbPath_.toUtf8().constData(), &db_);
    if (rc != SQLITE_OK) {
        QString error = db_ ? QString::fromUtf8(sqlite3_errmsg(db_)) : "Unknown error";
        emit logMessage(QString("[FileIndexer] Failed to open database: %1").arg(error));
        emit logMessage(QString("[FileIndexer] Database path: %1").arg(dbPath_));
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    
    emit logMessage("[FileIndexer] Database opened successfully");
    
    // 启用扩展加载（关键！这是迁移到独立 SQLite 的主要原因）
    rc = sqlite3_enable_load_extension(db_, 1);
    if (rc != SQLITE_OK) {
        emit logMessage(QString("[FileIndexer] Failed to enable extensions: %1")
            .arg(QString::fromUtf8(sqlite3_errmsg(db_))));
        emit logMessage("[FileIndexer] Will continue without extensions");
    } else {
        emit logMessage("[FileIndexer] Extension loading enabled");
    }
    
    // 尝试加载 Simple 扩展（用于中文分词）
    simpleLoaded_ = loadSimpleExtension();
    
    emit logMessage("[FileIndexer] Creating tables...");
    
    bool tablesCreated = createTables();
    if (!tablesCreated) {
        emit logMessage("[FileIndexer] Failed to create tables");
    } else {
        emit logMessage("[FileIndexer] Tables created successfully");
    }
    
    return tablesCreated;
}

bool FileIndexer::loadSimpleExtension() {
    // 查找 simple 扩展库
    QString appDir = QCoreApplication::applicationDirPath();
    
#ifdef Q_OS_WIN
    QString simpleLib = appDir + "/simple.dll";
#elif defined(Q_OS_MAC)
    QString simpleLib = appDir + "/libsimple.dylib";
#else
    QString simpleLib = appDir + "/libsimple.so";
#endif
    
    emit logMessage(QString("[FileIndexer] Looking for Simple extension: %1").arg(simpleLib));
    
    if (!QFileInfo::exists(simpleLib)) {
        emit logMessage("[FileIndexer] Simple extension not found, will use unicode61 tokenizer");
        emit logMessage("[FileIndexer] Note: Chinese search may not work optimally");
        return false;
    }
    
    emit logMessage("[FileIndexer] Found Simple extension, attempting to load...");
    
    // 使用 SQLite C API 加载扩展
    char* errMsg = nullptr;
    int rc = sqlite3_load_extension(
        db_,
        simpleLib.toUtf8().constData(),
        nullptr,  // entry point (使用默认)
        &errMsg
    );
    
    if (rc != SQLITE_OK) {
        QString error = errMsg ? QString::fromUtf8(errMsg) : "Unknown error";
        emit logMessage(QString("[FileIndexer] Failed to load Simple extension: %1").arg(error));
        emit logMessage("[FileIndexer] Will use unicode61 tokenizer instead");
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return false;
    }
    
    emit logMessage("[FileIndexer] Simple extension loaded successfully!");
    emit logMessage("[FileIndexer] Chinese full-text search enabled with jieba tokenizer");
    return true;
}

bool FileIndexer::createTables() {
    emit logMessage("[FileIndexer] Creating files table...");
    
    // 创建文件元数据表
    const char* createFilesSql = R"(
        CREATE TABLE IF NOT EXISTS files (
            file_id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT UNIQUE NOT NULL,
            file_name TEXT NOT NULL,
            file_size INTEGER,
            file_type TEXT,
            modified_time INTEGER,
            indexed_time INTEGER,
            content_hash TEXT
        )
    )";
    
    if (!execSQL(createFilesSql)) {
        emit logMessage("[FileIndexer] Failed to create files table");
        return false;
    }
    
    emit logMessage("[FileIndexer] Files table created, creating FTS5 table...");
    
    // 检测是否可以使用 Simple tokenizer
    QString tokenizer = detectBestTokenizer();
    emit logMessage(QString("[FileIndexer] Using tokenizer: %1").arg(tokenizer));
    
    // 创建 FTS5 全文搜索表
    QString createFtsTable = QString(
        "CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5("
        "  file_id UNINDEXED,"
        "  file_name,"
        "  content,"
        "  tokenize='%1'"
        ")"
    ).arg(tokenizer);
    
    if (!execSQL(createFtsTable)) {
        emit logMessage("[FileIndexer] Failed to create FTS5 table");
        emit logMessage("[FileIndexer] This may indicate FTS5 is not available in your SQLite build");
        return false;
    }
    
    emit logMessage("[FileIndexer] FTS5 table created, creating config table...");
    
    // 创建配置表
    const char* createConfigSql = R"(
        CREATE TABLE IF NOT EXISTS index_config (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )";
    
    if (!execSQL(createConfigSql)) {
        emit logMessage("[FileIndexer] Failed to create config table");
        return false;
    }
    
    // 创建索引
    execSQL("CREATE INDEX IF NOT EXISTS idx_file_path ON files(file_path)");
    execSQL("CREATE INDEX IF NOT EXISTS idx_file_type ON files(file_type)");
    
    emit logMessage("[FileIndexer] Index database tables created successfully");
    return true;
}

QString FileIndexer::detectBestTokenizer() {
    // 如果 Simple 扩展已加载，使用 simple tokenizer
    if (simpleLoaded_) {
        // 测试是否真的可用
        const char* testSql = "CREATE VIRTUAL TABLE IF NOT EXISTS _test_simple USING fts5(content, tokenize='simple')";
        if (execSQL(testSql)) {
            execSQL("DROP TABLE IF EXISTS _test_simple");
            emit logMessage("[FileIndexer] Simple tokenizer is available");
            return "simple";
        }
    }
    
    // 否则使用 unicode61
    emit logMessage("[FileIndexer] Simple tokenizer not available, using unicode61");
    return "unicode61 remove_diacritics 2";
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
        execSQL("BEGIN TRANSACTION");
        execSQL("DELETE FROM files");
        execSQL("DELETE FROM files_fts");
        execSQL("COMMIT");
        
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
        execSQL("BEGIN TRANSACTION");
        for (const QString& filePath : filesToIndex) {
            indexFile(filePath);
            current++;
            
            if (current % 10 == 0) {
                emit indexingProgress(current, total);
            }
            
            if (current % 100 == 0) {
                execSQL("COMMIT");
                execSQL("BEGIN TRANSACTION");
            }
        }
        execSQL("COMMIT");
        
        isIndexing_ = false;
        
        // 更新统计信息
        stats_.totalFiles = total;
        stats_.lastUpdateTime = QDateTime::currentDateTime();
        
        qDebug() << "Index rebuild completed," << total << "files indexed";
        emit indexingFinished();
    });
}

void FileIndexer::clearIndex() {
    execSQL("BEGIN TRANSACTION");
    execSQL("DELETE FROM files");
    execSQL("DELETE FROM files_fts");
    execSQL("COMMIT");
    
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
        emit logMessage(QString("[FileIndexer] No content extracted from: %1").arg(filePath));
        return;
    }
    
    emit logMessage(QString("[FileIndexer] Extracted %1 characters from: %2").arg(content.length()).arg(QFileInfo(filePath).fileName()));
    
    // 限制内容大小
    if (content.length() > config_.maxContentChars) {
        content = content.left(config_.maxContentChars);
    }
    
    // 插入或更新文件元数据
    sqlite3_stmt* stmt = nullptr;
    const char* sql = 
        "INSERT OR REPLACE INTO files "
        "(file_path, file_name, file_size, file_type, modified_time, indexed_time, content_hash) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";
    
    if (!prepareStatement(QString::fromUtf8(sql), &stmt)) {
        return;
    }
    
    sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fileInfo.fileName().toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, fileInfo.size());
    sqlite3_bind_text(stmt, 4, fileInfo.suffix().toLower().toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, fileInfo.lastModified().toSecsSinceEpoch());
    sqlite3_bind_int64(stmt, 6, QDateTime::currentDateTime().toSecsSinceEpoch());
    sqlite3_bind_text(stmt, 7, hash.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    
    if (rc != SQLITE_DONE) {
        emit logMessage(QString("[FileIndexer] Failed to insert file metadata: %1")
            .arg(QString::fromUtf8(sqlite3_errmsg(db_))));
        return;
    }
    
    // 获取文件ID
    qint64 fileId = getLastInsertId();
    
    emit logMessage(QString("[FileIndexer] Inserted file metadata, ID: %1").arg(fileId));
    
    // 插入或更新FTS索引
    const char* ftsSql = 
        "INSERT OR REPLACE INTO files_fts (file_id, file_name, content) "
        "VALUES (?, ?, ?)";
    
    if (!prepareStatement(QString::fromUtf8(ftsSql), &stmt)) {
        return;
    }
    
    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_text(stmt, 2, fileInfo.fileName().toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    
    if (rc != SQLITE_DONE) {
        emit logMessage(QString("[FileIndexer] Failed to insert FTS index: %1")
            .arg(QString::fromUtf8(sqlite3_errmsg(db_))));
        return;
    }
    
    emit logMessage(QString("[FileIndexer] Successfully indexed: %1").arg(fileInfo.fileName()));
    emit fileIndexed(filePath);
}

void FileIndexer::removeFileFromIndex(const QString& filePath) {
    sqlite3_stmt* stmt = nullptr;
    
    // 获取文件ID
    const char* selectSql = "SELECT file_id FROM files WHERE file_path = ?";
    if (!prepareStatement(QString::fromUtf8(selectSql), &stmt)) {
        return;
    }
    
    sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        qint64 fileId = sqlite3_column_int64(stmt, 0);
        finalizeStatement(stmt);
        stmt = nullptr;
        
        // 删除FTS索引
        const char* deleteFtsSql = "DELETE FROM files_fts WHERE file_id = ?";
        if (prepareStatement(QString::fromUtf8(deleteFtsSql), &stmt)) {
            sqlite3_bind_int64(stmt, 1, fileId);
            sqlite3_step(stmt);
            finalizeStatement(stmt);
            stmt = nullptr;
        }
        
        // 删除文件元数据
        const char* deleteFileSql = "DELETE FROM files WHERE file_id = ?";
        if (prepareStatement(QString::fromUtf8(deleteFileSql), &stmt)) {
            sqlite3_bind_int64(stmt, 1, fileId);
            sqlite3_step(stmt);
            finalizeStatement(stmt);
        }
        
        qDebug() << "Removed from index:" << filePath;
    } else {
        finalizeStatement(stmt);
    }
}

bool FileIndexer::isFileIndexed(const QString& filePath, const QString& hash) const {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT content_hash FROM files WHERE file_path = ?";
    
    if (!const_cast<FileIndexer*>(this)->prepareStatement(QString::fromUtf8(sql), &stmt)) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    
    bool result = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* storedHash = sqlite3_column_text(stmt, 0);
        if (storedHash) {
            result = (QString::fromUtf8(reinterpret_cast<const char*>(storedHash)) == hash);
        }
    }
    
    const_cast<FileIndexer*>(this)->finalizeStatement(stmt);
    return result;
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
        emit logMessage("[FileIndexer] Python not found, cannot extract PDF text");
        return QString();
    }
    
    // 查找提取脚本
    QString scriptPath = QCoreApplication::applicationDirPath() + "/extract_pdf_text.py";
    if (!QFileInfo::exists(scriptPath)) {
        emit logMessage(QString("[FileIndexer] PDF extraction script not found: %1").arg(scriptPath));
        return QString();
    }
    
    // 调用Python脚本提取文本
    QProcess process;
    process.start(python, QStringList() << scriptPath << filePath);
    
    if (!process.waitForStarted(10000)) {
        emit logMessage(QString("[FileIndexer] Failed to start PDF extraction: %1").arg(process.errorString()));
        return QString();
    }
    
    if (!process.waitForFinished(60000)) {
        process.kill();
        emit logMessage(QString("[FileIndexer] PDF extraction timed out: %1").arg(filePath));
        return QString();
    }
    
    if (process.exitCode() != 0) {
        QString error = QString::fromLocal8Bit(process.readAllStandardError());
        emit logMessage(QString("[FileIndexer] PDF extraction failed: %1").arg(error));
        return QString();
    }
    
    QString text = QString::fromUtf8(process.readAllStandardOutput());
    
    // 输出调试信息
    QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError());
    if (!stderr_output.isEmpty()) {
        emit logMessage(QString("[FileIndexer] PDF extraction output: %1").arg(stderr_output));
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
        emit logMessage("[FileIndexer] Python not found, cannot extract Word text");
        return QString();
    }
    
    // 查找提取脚本
    QString scriptPath = QCoreApplication::applicationDirPath() + "/extract_word_text.py";
    if (!QFileInfo::exists(scriptPath)) {
        emit logMessage(QString("[FileIndexer] Word extraction script not found: %1").arg(scriptPath));
        return QString();
    }
    
    // 调用Python脚本提取文本
    QProcess process;
    process.start(python, QStringList() << scriptPath << filePath);
    
    if (!process.waitForStarted(10000)) {
        emit logMessage(QString("[FileIndexer] Failed to start Word extraction: %1").arg(process.errorString()));
        return QString();
    }
    
    if (!process.waitForFinished(60000)) {
        process.kill();
        emit logMessage(QString("[FileIndexer] Word extraction timed out: %1").arg(filePath));
        return QString();
    }
    
    if (process.exitCode() != 0) {
        QString error = QString::fromLocal8Bit(process.readAllStandardError());
        emit logMessage(QString("[FileIndexer] Word extraction failed: %1").arg(error));
        return QString();
    }
    
    QString text = QString::fromUtf8(process.readAllStandardOutput());
    
    // 输出调试信息
    QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError());
    if (!stderr_output.isEmpty()) {
        emit logMessage(QString("[FileIndexer] Word extraction output: %1").arg(stderr_output));
    }
    
    return text;
}

QStringList FileIndexer::search(const QString& query, const QStringList& fileTypes) {
    if (query.trimmed().isEmpty()) {
        return QStringList();
    }
    
    emit logMessage(QString("[FileIndexer] Searching for: '%1'").arg(query));
    
    // 检查是否包含布尔运算符
    QString upperQuery = query.toUpper();
    if (upperQuery.contains(" AND ") || upperQuery.contains(" OR ") || upperQuery.contains(" NOT ")) {
        emit logMessage("[FileIndexer] Detected boolean operators, using boolean search");
        return searchWithBoolean(query, fileTypes);
    }
    
    // 使用 FTS5 MATCH 查询（如果 Simple tokenizer 已加载）
    QString sql;
    QStringList bindValues;
    
    if (simpleLoaded_) {
        // Simple tokenizer: 使用 FTS5 MATCH（自动分词）
        emit logMessage("[FileIndexer] Using FTS5 MATCH with Simple tokenizer");
        
        sql = "SELECT DISTINCT f.file_path, f.file_name "
              "FROM files f "
              "JOIN files_fts fts ON f.file_id = CAST(fts.file_id AS INTEGER) "
              "WHERE files_fts MATCH ? ";
        
        // Simple tokenizer 的 jieba 分词可能需要通配符来匹配部分词
        // 如果查询是中文且长度较短，尝试添加通配符
        QString ftsQuery = query;
        
        // 检测是否全是中文字符
        bool allChinese = true;
        for (const QChar& ch : query) {
            if (ch.unicode() < 0x4E00 || ch.unicode() > 0x9FFF) {
                if (!ch.isSpace()) {
                    allChinese = false;
                    break;
                }
            }
        }
        
        // 如果是中文短词，尝试使用通配符或者多个查询策略
        if (allChinese && query.length() <= 4) {
            // 策略1：尝试精确匹配
            // 策略2：如果失败，使用每个字符的 OR 组合
            emit logMessage(QString("[FileIndexer] Chinese query detected, trying exact match first"));
        }
        
        bindValues << ftsQuery;
    } else {
        // 降级到 LIKE 查询（中文兼容但性能差）
        emit logMessage("[FileIndexer] Using LIKE search (Chinese-compatible, performance warning)");
        
        sql = "SELECT DISTINCT f.file_path, f.file_name "
              "FROM files f "
              "JOIN files_fts fts ON f.file_id = CAST(fts.file_id AS INTEGER) "
              "WHERE fts.content LIKE ? OR fts.file_name LIKE ? ";
        
        QString likePattern = "%" + query + "%";
        bindValues << likePattern << likePattern;
    }
    
    // 添加文件类型过滤
    if (!fileTypes.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < fileTypes.size(); ++i) {
            placeholders << "?";
        }
        sql += "AND f.file_type IN (" + placeholders.join(", ") + ") ";
        bindValues << fileTypes;
    }
    
    sql += "LIMIT 1000";
    
    emit logMessage(QString("[FileIndexer] SQL: %1").arg(sql));
    
    // 执行查询
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return QStringList();
    }
    
    // 绑定参数
    for (int i = 0; i < bindValues.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, bindValues[i].toUtf8().constData(), -1, SQLITE_TRANSIENT);
    }
    
    QStringList results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* filePath = sqlite3_column_text(stmt, 0);
        const unsigned char* fileName = sqlite3_column_text(stmt, 1);
        
        if (filePath) {
            results << QString::fromUtf8(reinterpret_cast<const char*>(filePath));
            if (fileName) {
                emit logMessage(QString("[FileIndexer]   Found: %1")
                    .arg(QString::fromUtf8(reinterpret_cast<const char*>(fileName))));
            }
        }
    }
    
    finalizeStatement(stmt);
    
    emit logMessage(QString("[FileIndexer] Search completed: %1 results").arg(results.size()));
    
    // 如果 FTS5 MATCH 没有结果但使用了 Simple tokenizer，尝试降级到 LIKE
    if (results.isEmpty() && simpleLoaded_) {
        emit logMessage("[FileIndexer] FTS5 returned no results, trying LIKE fallback...");
        
        QString likeSql = "SELECT DISTINCT f.file_path, f.file_name "
                         "FROM files f "
                         "JOIN files_fts fts ON f.file_id = CAST(fts.file_id AS INTEGER) "
                         "WHERE fts.content LIKE ? OR fts.file_name LIKE ? ";
        
        if (!fileTypes.isEmpty()) {
            QStringList placeholders;
            for (int i = 0; i < fileTypes.size(); ++i) {
                placeholders << "?";
            }
            likeSql += "AND f.file_type IN (" + placeholders.join(", ") + ") ";
        }
        
        likeSql += "LIMIT 1000";
        
        if (prepareStatement(likeSql, &stmt)) {
            QString likePattern = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, likePattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, likePattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            
            int paramIndex = 3;
            for (const QString& type : fileTypes) {
                sqlite3_bind_text(stmt, paramIndex++, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            }
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* filePath = sqlite3_column_text(stmt, 0);
                const unsigned char* fileName = sqlite3_column_text(stmt, 1);
                
                if (filePath) {
                    results << QString::fromUtf8(reinterpret_cast<const char*>(filePath));
                    if (fileName) {
                        emit logMessage(QString("[FileIndexer]   Found (LIKE): %1")
                            .arg(QString::fromUtf8(reinterpret_cast<const char*>(fileName))));
                    }
                }
            }
            
            finalizeStatement(stmt);
            emit logMessage(QString("[FileIndexer] LIKE fallback completed: %1 results").arg(results.size()));
        }
    }
    
    return results;
}

QStringList FileIndexer::searchWithBoolean(const QString& query, const QStringList& fileTypes) {
    // 解析布尔查询
    // 支持格式: "term1 AND term2", "term1 OR term2", "term1 NOT term2"
    
    emit logMessage(QString("[FileIndexer] Parsing boolean query: %1").arg(query));
    
    QStringList andTerms;
    QStringList orTerms;
    QStringList notTerms;
    
    // 简单的解析：按 AND/OR/NOT 分割
    QString currentQuery = query;
    
    // 处理 NOT（优先级最高）
    QRegExp notRegex("\\s+NOT\\s+", Qt::CaseInsensitive);
    int notPos = 0;
    while ((notPos = notRegex.indexIn(currentQuery, notPos)) != -1) {
        // 提取 NOT 后面的词
        int endPos = currentQuery.indexOf(QRegExp("\\s+(AND|OR)\\s+", Qt::CaseInsensitive), notPos + 5);
        QString notTerm;
        if (endPos == -1) {
            notTerm = currentQuery.mid(notPos + 5).trimmed();
            currentQuery = currentQuery.left(notPos);
        } else {
            notTerm = currentQuery.mid(notPos + 5, endPos - notPos - 5).trimmed();
            currentQuery = currentQuery.left(notPos) + currentQuery.mid(endPos);
        }
        if (!notTerm.isEmpty()) {
            notTerms << notTerm;
            emit logMessage(QString("[FileIndexer]   NOT term: %1").arg(notTerm));
        }
        notPos = 0; // 重新开始
    }
    
    // 处理 OR
    if (currentQuery.contains(QRegExp("\\s+OR\\s+", Qt::CaseInsensitive))) {
        orTerms = currentQuery.split(QRegExp("\\s+OR\\s+", Qt::CaseInsensitive), QString::SkipEmptyParts);
        for (QString& term : orTerms) {
            term = term.trimmed();
            if (!term.isEmpty()) {
                emit logMessage(QString("[FileIndexer]   OR term: %1").arg(term));
            }
        }
    }
    // 处理 AND
    else if (currentQuery.contains(QRegExp("\\s+AND\\s+", Qt::CaseInsensitive))) {
        andTerms = currentQuery.split(QRegExp("\\s+AND\\s+", Qt::CaseInsensitive), QString::SkipEmptyParts);
        for (QString& term : andTerms) {
            term = term.trimmed();
            if (!term.isEmpty()) {
                emit logMessage(QString("[FileIndexer]   AND term: %1").arg(term));
            }
        }
    }
    // 没有运算符
    else if (!currentQuery.trimmed().isEmpty()) {
        andTerms << currentQuery.trimmed();
    }
    
    // 构建 SQL 查询
    QString sql;
    QStringList bindValues;
    
    if (simpleLoaded_) {
        // 使用 FTS5 MATCH（Simple tokenizer 支持）
        emit logMessage("[FileIndexer] Using FTS5 MATCH for boolean search");
        
        // 构建 FTS5 查询字符串
        QStringList ftsTerms;
        
        if (!andTerms.isEmpty()) {
            ftsTerms << "(" + andTerms.join(" AND ") + ")";
        }
        
        if (!orTerms.isEmpty()) {
            ftsTerms << "(" + orTerms.join(" OR ") + ")";
        }
        
        for (const QString& term : notTerms) {
            ftsTerms << "NOT " + term;
        }
        
        QString ftsQuery = ftsTerms.join(" AND ");
        
        sql = "SELECT DISTINCT f.file_path, f.file_name FROM files f "
              "JOIN files_fts fts ON f.file_id = CAST(fts.file_id AS INTEGER) "
              "WHERE files_fts MATCH ? ";
        
        bindValues << ftsQuery;
    } else {
        // 降级到 LIKE 查询
        emit logMessage("[FileIndexer] Using LIKE for boolean search (performance warning)");
        
        sql = "SELECT DISTINCT f.file_path, f.file_name FROM files f "
              "JOIN files_fts fts ON f.file_id = CAST(fts.file_id AS INTEGER) WHERE ";
        
        QStringList conditions;
        
        // AND 条件：所有词都必须出现
        if (!andTerms.isEmpty()) {
            QStringList andConditions;
            for (const QString& term : andTerms) {
                andConditions << "(fts.content LIKE ? OR fts.file_name LIKE ?)";
                bindValues << ("%" + term + "%") << ("%" + term + "%");
            }
            conditions << "(" + andConditions.join(" AND ") + ")";
        }
        
        // OR 条件：任一词出现即可
        if (!orTerms.isEmpty()) {
            QStringList orConditions;
            for (const QString& term : orTerms) {
                orConditions << "(fts.content LIKE ? OR fts.file_name LIKE ?)";
                bindValues << ("%" + term + "%") << ("%" + term + "%");
            }
            conditions << "(" + orConditions.join(" OR ") + ")";
        }
        
        // NOT 条件：不能包含这些词
        for (const QString& term : notTerms) {
            conditions << "(fts.content NOT LIKE ? AND fts.file_name NOT LIKE ?)";
            bindValues << ("%" + term + "%") << ("%" + term + "%");
        }
        
        if (conditions.isEmpty()) {
            emit logMessage("[FileIndexer] No valid search terms found");
            return QStringList();
        }
        
        sql += conditions.join(" AND ");
    }
    
    // 添加文件类型过滤
    if (!fileTypes.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < fileTypes.size(); ++i) {
            placeholders << "?";
        }
        sql += " AND f.file_type IN (" + placeholders.join(", ") + ")";
        bindValues << fileTypes;
    }
    
    sql += " LIMIT 1000";
    
    emit logMessage(QString("[FileIndexer] Boolean SQL: %1").arg(sql));
    
    // 执行查询
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return QStringList();
    }
    
    // 绑定参数
    for (int i = 0; i < bindValues.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, bindValues[i].toUtf8().constData(), -1, SQLITE_TRANSIENT);
    }
    
    QStringList results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* filePath = sqlite3_column_text(stmt, 0);
        const unsigned char* fileName = sqlite3_column_text(stmt, 1);
        
        if (filePath) {
            results << QString::fromUtf8(reinterpret_cast<const char*>(filePath));
            if (fileName) {
                emit logMessage(QString("[FileIndexer]   Found: %1")
                    .arg(QString::fromUtf8(reinterpret_cast<const char*>(fileName))));
            }
        }
    }
    
    finalizeStatement(stmt);
    
    emit logMessage(QString("[FileIndexer] Boolean search completed: %1 results").arg(results.size()));
    return results;
}

QString FileIndexer::buildFTS5Query(const QString& userQuery) const {
    QString query = userQuery.trimmed();
    
    // 检测是否包含中文字符
    bool hasChinese = false;
    for (const QChar& ch : query) {
        if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) {
            hasChinese = true;
            break;
        }
    }
    
    if (hasChinese) {
        // 中文处理：将每个字符用 AND 连接
        // 例如："雁塔" -> "雁 AND 塔"
        QStringList chars;
        for (const QChar& ch : query) {
            if (!ch.isSpace()) {
                chars << QString(ch);
            }
        }
        return chars.join(" AND ");
    } else {
        // 英文处理：简单的查询转换
        // AND -> 空格（FTS5默认是AND）
        query.replace(" AND ", " ", Qt::CaseInsensitive);
        query.replace(" and ", " ");
        
        // OR 保持原样（FTS5支持）
        query.replace(" or ", " OR ");
        
        // NOT -> NOT（FTS5支持）
        query.replace(" not ", " NOT ");
        
        return query;
    }
}

IndexStats FileIndexer::getStats() {
    IndexStats stats = stats_;
    
    // 更新统计信息
    sqlite3_stmt* stmt = nullptr;
    
    // 文件总数
    const char* sql = "SELECT COUNT(*) FROM files";
    if (prepareStatement(QString::fromUtf8(sql), &stmt)) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.totalFiles = sqlite3_column_int(stmt, 0);
        }
        finalizeStatement(stmt);
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
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT file_path FROM files WHERE file_path LIKE ?";
    
    if (prepareStatement(QString::fromUtf8(sql), &stmt)) {
        QString likePattern = dirPath + "%";
        sqlite3_bind_text(stmt, 1, likePattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* indexedPath = sqlite3_column_text(stmt, 0);
            if (indexedPath) {
                QString pathStr = QString::fromUtf8(reinterpret_cast<const char*>(indexedPath));
                if (!currentFiles.contains(pathStr)) {
                    removeFileFromIndex(pathStr);
                }
            }
        }
        
        finalizeStatement(stmt);
    }
}

void FileIndexer::onScanTimerTimeout() {
    qDebug() << "Scheduled scan triggered";
    
    QtConcurrent::run([this]() {
        scanDirectory(sharedPath_);
    });
}

} // namespace CrossNetShare
