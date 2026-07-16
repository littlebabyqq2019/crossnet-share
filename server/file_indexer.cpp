#include "file_indexer.h"
#include "common/file_utils.h"
#include <QMutexLocker>
#include <algorithm>

namespace CrossNetShare {

FileIndexer::FileIndexer() {
}

void FileIndexer::addClientShare(const QString& clientId, const QString& sharePath) {
    QMutexLocker locker(&mutex_);

    ClientShare share;
    share.clientId = clientId;
    share.sharePath = sharePath;

    clientShares_[clientId] = share;

    // 立即扫描文件
    scanClientFiles(clientShares_[clientId]);
}

void FileIndexer::addClientShare(const QString& clientId, const QString& sharePath, const std::vector<FileMetadata>& files) {
    QMutexLocker locker(&mutex_);

    ClientShare share;
    share.clientId = clientId;
    share.sharePath = sharePath;
    share.files = files;

    clientShares_[clientId] = share;

    // 不需要扫描，直接使用客户端提供的文件列表
}

void FileIndexer::removeClient(const QString& clientId) {
    QMutexLocker locker(&mutex_);
    clientShares_.remove(clientId);
}

void FileIndexer::refreshIndex(const QString& clientId) {
    QMutexLocker locker(&mutex_);

    if (clientShares_.contains(clientId)) {
        scanClientFiles(clientShares_[clientId]);
    }
}

void FileIndexer::refreshAll() {
    QMutexLocker locker(&mutex_);

    for (auto it = clientShares_.begin(); it != clientShares_.end(); ++it) {
        scanClientFiles(it.value());
    }
}

std::vector<FileMetadata> FileIndexer::getAllFiles() const {
    QMutexLocker locker(&mutex_);

    std::vector<FileMetadata> allFiles;

    for (const auto& share : clientShares_) {
        allFiles.insert(allFiles.end(), share.files.begin(), share.files.end());
    }

    return allFiles;
}

std::vector<FileMetadata> FileIndexer::getClientFiles(const QString& clientId) const {
    QMutexLocker locker(&mutex_);

    if (clientShares_.contains(clientId)) {
        return clientShares_[clientId].files;
    }

    return std::vector<FileMetadata>();
}

std::vector<FileMetadata> FileIndexer::filterByDate(const DateFilter& filter) const {
    std::vector<FileMetadata> allFiles = getAllFiles();
    return FileUtils::filterByDate(allFiles, filter);
}

std::vector<FileMetadata> FileIndexer::filterByClientAndDate(const QString& clientId,
                                                              const DateFilter& filter) const {
    std::vector<FileMetadata> clientFiles = getClientFiles(clientId);
    return FileUtils::filterByDate(clientFiles, filter);
}

QString FileIndexer::resolveFilePath(const QString& clientId, const QString& relativePath) const {
    QMutexLocker locker(&mutex_);

    if (clientShares_.contains(clientId)) {
        return FileUtils::joinPath(clientShares_[clientId].sharePath, relativePath);
    }

    return QString();
}

void FileIndexer::scanClientFiles(ClientShare& share) {
    share.files = FileUtils::scanDirectory(share.sharePath);

    // 设置所有文件的所有者
    for (auto& file : share.files) {
        file.ownerClient = share.clientId.toStdString();
    }
}

}
