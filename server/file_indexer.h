#pragma once

#include "common/message.h"
#include <QString>
#include <QMap>
#include <QMutex>
#include <vector>

namespace CrossNetShare {

class FileIndexer {
public:
    FileIndexer();

    // 添加客户端的共享目录
    void addClientShare(const QString& clientId, const QString& sharePath);

    // 移除客户端
    void removeClient(const QString& clientId);

    // 刷新指定客户端的文件索引
    void refreshIndex(const QString& clientId);

    // 刷新所有客户端的索引
    void refreshAll();

    // 获取所有文件列表
    std::vector<FileMetadata> getAllFiles() const;

    // 获取指定客户端的文件列表
    std::vector<FileMetadata> getClientFiles(const QString& clientId) const;

    // 根据日期过滤文件
    std::vector<FileMetadata> filterByDate(const DateFilter& filter) const;

    // 根据客户端和日期过滤
    std::vector<FileMetadata> filterByClientAndDate(const QString& clientId,
                                                      const DateFilter& filter) const;

    // 根据相对路径查找文件的完整路径
    QString resolveFilePath(const QString& clientId, const QString& relativePath) const;

private:
    struct ClientShare {
        QString clientId;
        QString sharePath;
        std::vector<FileMetadata> files;
    };

    mutable QMutex mutex_;
    QMap<QString, ClientShare> clientShares_;

    void scanClientFiles(ClientShare& share);
};

}
