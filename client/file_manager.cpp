#include "file_manager.h"
#include "common/file_utils.h"

namespace CrossNetShare {

FileManager::FileManager(Client* client, QObject* parent)
    : QObject(parent)
    , client_(client)
{
    connect(client_, &Client::batchDownloadProgress,
            this, &FileManager::onBatchDownloadProgress);
    connect(client_, &Client::batchDownloadComplete,
            this, &FileManager::onBatchDownloadComplete);
    connect(client_, &Client::downloadComplete,
            this, &FileManager::onDownloadComplete);
    connect(client_, &Client::uploadComplete,
            this, &FileManager::onUploadComplete);
}

void FileManager::setSaveDirectory(const QString& dir) {
    saveDir_ = dir;
    FileUtils::ensureDirectory(dir);
}

void FileManager::downloadFilteredFiles(const DateFilter& filter, const QString& targetClient) {
    if (saveDir_.isEmpty()) {
        emit error("Save directory not set");
        return;
    }

    emit downloadStarted();
    client_->batchDownload(filter, targetClient, saveDir_);
}

void FileManager::uploadFile(const QString& localPath) {
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists()) {
        emit error("File not found: " + localPath);
        return;
    }

    emit uploadStarted();

    // 使用文件名作为相对路径
    QString relativePath = fileInfo.fileName();
    client_->uploadFile(localPath, relativePath);
}

void FileManager::onBatchDownloadProgress(int current, int total) {
    emit downloadProgress(current, total);
}

void FileManager::onBatchDownloadComplete(int successCount, int totalCount) {
    emit downloadFinished(successCount, totalCount);
}

void FileManager::onDownloadComplete(const QString& filename, const QString& savePath) {
    // 单个文件下载完成的处理（如果需要）
}

void FileManager::onUploadComplete(bool success, const QString& message) {
    emit uploadFinished(success);

    if (!success) {
        emit error("Upload failed: " + message);
    }
}

}
