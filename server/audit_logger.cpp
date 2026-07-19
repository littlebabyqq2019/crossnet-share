#include "audit_logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>

namespace CrossNetShare {

AuditLogger* AuditLogger::instance_ = nullptr;

AuditLogger::AuditLogger() {
}

AuditLogger* AuditLogger::instance() {
    if (!instance_) {
        instance_ = new AuditLogger();
    }
    return instance_;
}

void AuditLogger::log(const QString& username, AuditAction action, const QString& target, const QString& ipAddress, bool success) {
    QMutexLocker locker(&mutex_);

    AuditLogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.username = username;
    entry.action = action;
    entry.target = target;
    entry.ipAddress = ipAddress;
    entry.success = success;

    logs_.append(entry);

    // 自动清理旧日志
    if (logs_.size() > MAX_LOG_ENTRIES) {
        pruneOldLogs();
    }
}

QVector<AuditLogEntry> AuditLogger::getLogs(const QString& username, const QDateTime& startTime, const QDateTime& endTime) const {
    QMutexLocker locker(&mutex_);

    QVector<AuditLogEntry> filtered;

    for (const AuditLogEntry& entry : logs_) {
        // 用户名过滤
        if (!username.isEmpty() && entry.username != username) {
            continue;
        }

        // 开始时间过滤
        if (startTime.isValid() && entry.timestamp < startTime) {
            continue;
        }

        // 结束时间过滤
        if (endTime.isValid() && entry.timestamp > endTime) {
            continue;
        }

        filtered.append(entry);
    }

    return filtered;
}

bool AuditLogger::saveToFile(const QString& filePath) {
    QMutexLocker locker(&mutex_);

    QJsonArray logsArray;
    for (const AuditLogEntry& entry : logs_) {
        QJsonObject logObj;
        logObj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
        logObj["username"] = entry.username;
        logObj["action"] = static_cast<int>(entry.action);
        logObj["target"] = entry.target;
        logObj["ipAddress"] = entry.ipAddress;
        logObj["success"] = entry.success;

        logsArray.append(logObj);
    }

    QJsonObject root;
    root["logs"] = logsArray;

    QJsonDocument doc(root);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(doc.toJson());
    file.close();

    return true;
}

bool AuditLogger::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray logsArray = root["logs"].toArray();

    QMutexLocker locker(&mutex_);
    logs_.clear();

    for (const QJsonValue& value : logsArray) {
        QJsonObject logObj = value.toObject();

        AuditLogEntry entry;
        entry.timestamp = QDateTime::fromString(logObj["timestamp"].toString(), Qt::ISODate);
        entry.username = logObj["username"].toString();
        entry.action = static_cast<AuditAction>(logObj["action"].toInt());
        entry.target = logObj["target"].toString();
        entry.ipAddress = logObj["ipAddress"].toString();
        entry.success = logObj["success"].toBool(true);

        logs_.append(entry);
    }

    return true;
}

void AuditLogger::clearLogs(const QDateTime& beforeTime) {
    QMutexLocker locker(&mutex_);

    if (!beforeTime.isValid()) {
        logs_.clear();
        return;
    }

    // 删除指定时间之前的日志
    QVector<AuditLogEntry> kept;
    for (const AuditLogEntry& entry : logs_) {
        if (entry.timestamp >= beforeTime) {
            kept.append(entry);
        }
    }

    logs_ = kept;
}

void AuditLogger::pruneOldLogs() {
    // 保留最新的 MAX_LOG_ENTRIES 条日志
    if (logs_.size() > MAX_LOG_ENTRIES) {
        int toRemove = logs_.size() - MAX_LOG_ENTRIES;
        logs_.remove(0, toRemove);
    }
}

}
