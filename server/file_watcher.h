#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QString>
#include <QSet>
#include <QMap>

namespace CrossNetShare {

class FileWatcher : public QObject {
    Q_OBJECT

public:
    explicit FileWatcher(QObject* parent = nullptr);
    ~FileWatcher();

    // 添加监控目录
    void addWatchPath(const QString& clientId, const QString& path);

    // 移除监控目录
    void removeWatchPath(const QString& clientId);

    // 移除所有监控
    void clearAll();

signals:
    // 目录内容变化信号
    void directoryChanged(const QString& clientId, const QString& path);

    // 文件变化信号
    void fileChanged(const QString& clientId, const QString& filePath);

    // 日志信息
    void logMessage(const QString& message);

private slots:
    void onDirectoryChanged(const QString& path);
    void onFileChanged(const QString& path);

private:
    QFileSystemWatcher* watcher_;
    QMap<QString, QString> pathToClient_;  // path -> clientId
};

}
