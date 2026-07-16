#include "file_watcher.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace CrossNetShare {

FileWatcher::FileWatcher(QObject* parent)
    : QObject(parent)
    , watcher_(new QFileSystemWatcher(this))
{
    connect(watcher_, &QFileSystemWatcher::directoryChanged,
            this, &FileWatcher::onDirectoryChanged);
    connect(watcher_, &QFileSystemWatcher::fileChanged,
            this, &FileWatcher::onFileChanged);
}

FileWatcher::~FileWatcher() {
    clearAll();
}

void FileWatcher::addWatchPath(const QString& clientId, const QString& path) {
    QDir dir(path);
    if (!dir.exists()) {
        emit logMessage("Watch path does not exist: " + path);
        return;
    }

    // 移除旧的监控（如果存在）
    removeWatchPath(clientId);

    // 添加主目录监控
    watcher_->addPath(path);
    pathToClient_[path] = clientId;

    // 递归添加所有子目录监控
    QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString subDir = it.next();
        watcher_->addPath(subDir);
        pathToClient_[subDir] = clientId;
    }

    emit logMessage("Started watching: " + path + " for client " + clientId);
}

void FileWatcher::removeWatchPath(const QString& clientId) {
    // 找到所有属于该客户端的路径
    QStringList pathsToRemove;
    for (auto it = pathToClient_.begin(); it != pathToClient_.end(); ++it) {
        if (it.value() == clientId) {
            pathsToRemove.append(it.key());
        }
    }

    // 移除监控
    for (const QString& path : pathsToRemove) {
        watcher_->removePath(path);
        pathToClient_.remove(path);
    }

    if (!pathsToRemove.isEmpty()) {
        emit logMessage("Stopped watching " + QString::number(pathsToRemove.size()) +
                       " paths for client " + clientId);
    }
}

void FileWatcher::clearAll() {
    if (!watcher_->directories().isEmpty()) {
        watcher_->removePaths(watcher_->directories());
    }
    if (!watcher_->files().isEmpty()) {
        watcher_->removePaths(watcher_->files());
    }
    pathToClient_.clear();
}

void FileWatcher::onDirectoryChanged(const QString& path) {
    if (pathToClient_.contains(path)) {
        QString clientId = pathToClient_[path];
        emit directoryChanged(clientId, path);
        emit logMessage("Directory changed: " + path + " (client: " + clientId + ")");

        // 检查是否有新的子目录，如果有则添加监控
        QDir dir(path);
        QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& subDir : subDirs) {
            QString fullPath = dir.filePath(subDir);
            if (!watcher_->directories().contains(fullPath)) {
                watcher_->addPath(fullPath);
                pathToClient_[fullPath] = clientId;
                emit logMessage("Added watch for new directory: " + fullPath);
            }
        }
    }
}

void FileWatcher::onFileChanged(const QString& path) {
    // 获取文件所在目录
    QFileInfo fileInfo(path);
    QString dirPath = fileInfo.absolutePath();

    if (pathToClient_.contains(dirPath)) {
        QString clientId = pathToClient_[dirPath];
        emit fileChanged(clientId, path);
        emit logMessage("File changed: " + path + " (client: " + clientId + ")");
    }
}

}
