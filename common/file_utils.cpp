#include "file_utils.h"
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QDateTime>

namespace CrossNetShare {

std::vector<FileMetadata> FileUtils::scanDirectory(const QString& dirPath, const QString& basePath) {
    std::vector<FileMetadata> files;

    QString base = basePath.isEmpty() ? dirPath : basePath;
    QDir baseDir(base);

    QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);

        if (!fileInfo.isFile()) {
            continue;
        }

        FileMetadata meta;
        meta.filename = fileInfo.fileName().toStdString();
        meta.relativePath = baseDir.relativeFilePath(filePath).toStdString();
        meta.size = fileInfo.size();
        meta.createTime = fileInfo.birthTime().toSecsSinceEpoch();
        meta.modifyTime = fileInfo.lastModified().toSecsSinceEpoch();

        files.push_back(meta);
    }

    return files;
}

std::vector<FileMetadata> FileUtils::filterByDate(const std::vector<FileMetadata>& files,
                                                    const DateFilter& filter) {
    std::vector<FileMetadata> filtered;

    for (const auto& file : files) {
        // 使用创建时间进行过滤
        if (file.createTime >= filter.startDate && file.createTime <= filter.endDate) {
            filtered.push_back(file);
        }
    }

    return filtered;
}

QByteArray FileUtils::readFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }

    return file.readAll();
}

bool FileUtils::writeFile(const QString& filePath, const QByteArray& data, bool append) {
    QFileInfo fileInfo(filePath);
    if (!ensureDirectory(fileInfo.absolutePath())) {
        return false;
    }

    QFile file(filePath);
    QIODevice::OpenMode mode = append ? (QIODevice::WriteOnly | QIODevice::Append)
                                       : QIODevice::WriteOnly;

    if (!file.open(mode)) {
        return false;
    }

    qint64 written = file.write(data);
    file.close();

    return written == data.size();
}

bool FileUtils::ensureDirectory(const QString& dirPath) {
    QDir dir;
    return dir.mkpath(dirPath);
}

uint64_t FileUtils::getFileSize(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    return fileInfo.exists() ? fileInfo.size() : 0;
}

time_t FileUtils::getFileCreateTime(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    return fileInfo.exists() ? fileInfo.birthTime().toSecsSinceEpoch() : 0;
}

time_t FileUtils::getFileModifyTime(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    return fileInfo.exists() ? fileInfo.lastModified().toSecsSinceEpoch() : 0;
}

QString FileUtils::normalizePath(const QString& path) {
    return QDir::cleanPath(path);
}

QString FileUtils::joinPath(const QString& base, const QString& relative) {
    QDir dir(base);
    return dir.filePath(relative);
}

}
