#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMutex>

namespace CrossNetShare {

// 审计日志类型
enum class AuditAction {
    Login,           // 登录
    Logout,          // 登出
    ViewFile,        // 查看文件
    PreviewFile,     // 预览文件
    PrintFile,       // 打印文件
    DownloadFile,    // 下载文件
    BatchDownload,   // 批量下载
    SearchFiles      // 搜索文件
};

// 审计日志条目
struct AuditLogEntry {
    QDateTime timestamp;      // 时间戳
    QString username;         // 用户名
    AuditAction action;       // 操作类型
    QString target;           // 操作目标（文件名等）
    QString ipAddress;        // IP地址
    bool success;             // 是否成功

    AuditLogEntry()
        : timestamp(QDateTime::currentDateTime())
        , success(true)
    {}
};

// 操作类型转字符串
inline QString actionToString(AuditAction action) {
    switch (action) {
        case AuditAction::Login: return "登录";
        case AuditAction::Logout: return "登出";
        case AuditAction::ViewFile: return "查看文件";
        case AuditAction::PreviewFile: return "预览文件";
        case AuditAction::PrintFile: return "打印文件";
        case AuditAction::DownloadFile: return "下载文件";
        case AuditAction::BatchDownload: return "批量下载";
        case AuditAction::SearchFiles: return "搜索文件";
        default: return "未知操作";
    }
}

class AuditLogger {
public:
    static AuditLogger* instance();

    // 记录操作
    void log(const QString& username, AuditAction action, const QString& target = "", const QString& ipAddress = "", bool success = true);

    // 获取日志（可按用户、时间范围过滤）
    QVector<AuditLogEntry> getLogs(const QString& username = "", const QDateTime& startTime = QDateTime(), const QDateTime& endTime = QDateTime()) const;

    // 保存和加载
    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);

    // 清空日志（可选时间范围）
    void clearLogs(const QDateTime& beforeTime = QDateTime());

private:
    AuditLogger();
    static AuditLogger* instance_;

    QVector<AuditLogEntry> logs_;
    mutable QMutex mutex_;

    // 限制日志数量，防止无限增长
    static const int MAX_LOG_ENTRIES = 10000;
    void pruneOldLogs();
};

}
