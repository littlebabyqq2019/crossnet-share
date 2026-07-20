#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace CrossNetShare {

enum class MessageType {
    // 客户端注册
    REGISTER_CLIENT,
    REGISTER_RESPONSE,

    // 文件列表和索引
    REQUEST_FILE_LIST,
    FILE_LIST_RESPONSE,
    REFRESH_INDEX_REQUEST,  // 服务器请求客户端刷新文件列表

    // 文件筛选（按日期）
    REQUEST_FILTERED_FILES,
    FILTERED_FILES_RESPONSE,

    // 文件传输
    DOWNLOAD_REQUEST,
    DOWNLOAD_RESPONSE,
    UPLOAD_REQUEST,
    UPLOAD_RESPONSE,
    FILE_DATA,
    FILE_COMPLETE,

    // 批量下载
    BATCH_DOWNLOAD_REQUEST,
    BATCH_DOWNLOAD_RESPONSE,

    // 心跳和状态
    HEARTBEAT,
    ERROR_MESSAGE,

    // 配置
    CONFIG_UPDATE
};

struct FileMetadata {
    std::string filename;
    std::string relativePath;
    uint64_t size;
    time_t createTime;
    time_t modifyTime;
    std::string ownerClient;

    FileMetadata() : size(0), createTime(0), modifyTime(0) {}
};

struct DateFilter {
    time_t startDate;
    time_t endDate;

    DateFilter() : startDate(0), endDate(0) {}
};

struct TransferProgress {
    uint64_t bytesTransferred;
    uint64_t totalBytes;
    int fileIndex;
    int totalFiles;

    TransferProgress() : bytesTransferred(0), totalBytes(0), fileIndex(0), totalFiles(0) {}
};

}
