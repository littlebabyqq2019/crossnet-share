#pragma once

#include "message.h"
#include <QString>
#include <QDir>
#include <vector>

namespace CrossNetShare {

class FileUtils {
public:
    // 扫描目录，获取所有文件的元数据
    static std::vector<FileMetadata> scanDirectory(const QString& dirPath,
                                                     const QString& basePath = "");

    // 根据日期过滤文件
    static std::vector<FileMetadata> filterByDate(const std::vector<FileMetadata>& files,
                                                    const DateFilter& filter);

    // 读取文件内容
    static QByteArray readFile(const QString& filePath);

    // 写入文件内容
    static bool writeFile(const QString& filePath, const QByteArray& data, bool append = false);

    // 确保目录存在
    static bool ensureDirectory(const QString& dirPath);

    // 获取文件大小
    static uint64_t getFileSize(const QString& filePath);

    // 获取文件时间信息
    static time_t getFileCreateTime(const QString& filePath);
    static time_t getFileModifyTime(const QString& filePath);

    // 路径处理
    static QString normalizePath(const QString& path);
    static QString joinPath(const QString& base, const QString& relative);
};

}
