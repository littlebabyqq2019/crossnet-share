#pragma once

#include "client.h"
#include <QObject>
#include <QString>
#include <QDir>

namespace CrossNetShare {

class FileManager : public QObject {
    Q_OBJECT

public:
    explicit FileManager(Client* client, QObject* parent = nullptr);

    // 设置本地保存目录
    void setSaveDirectory(const QString& dir);
    QString getSaveDirectory() const { return saveDir_; }

    // 批量下载文件
    void downloadFilteredFiles(const DateFilter& filter, const QString& targetClient = "");

    // 上传文件到服务器
    void uploadFile(const QString& localPath);

signals:
    void downloadStarted();
    void downloadProgress(int current, int total);
    void downloadFinished(int successCount, int totalCount);
    void uploadStarted();
    void uploadFinished(bool success);
    void error(const QString& errorMsg);

private slots:
    void onBatchDownloadProgress(int current, int total);
    void onBatchDownloadComplete(int successCount, int totalCount);
    void onDownloadComplete(const QString& filename, const QString& savePath);
    void onUploadComplete(bool success, const QString& message);

private:
    Client* client_;
    QString saveDir_;
};

}
