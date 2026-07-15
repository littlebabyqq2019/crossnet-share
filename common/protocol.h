#pragma once

#include "message.h"
#include <nlohmann/json.hpp>
#include <QByteArray>
#include <QString>

namespace CrossNetShare {

class Protocol {
public:
    // 消息序列化
    static QByteArray serializeMessage(MessageType type, const nlohmann::json& payload);
    static bool deserializeMessage(const QByteArray& data, MessageType& type, nlohmann::json& payload);

    // 文件元数据转换
    static nlohmann::json fileMetadataToJson(const FileMetadata& meta);
    static FileMetadata jsonToFileMetadata(const nlohmann::json& j);

    // 日期过滤器转换
    static nlohmann::json dateFilterToJson(const DateFilter& filter);
    static DateFilter jsonToDateFilter(const nlohmann::json& j);

    // 进度信息转换
    static nlohmann::json progressToJson(const TransferProgress& progress);
    static TransferProgress jsonToProgress(const nlohmann::json& j);

    // 辅助函数
    static std::string timeToString(time_t t);
    static time_t stringToTime(const std::string& str);
};

}
