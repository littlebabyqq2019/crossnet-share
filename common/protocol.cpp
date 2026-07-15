#include "protocol.h"
#include <QDataStream>
#include <sstream>
#include <iomanip>

namespace CrossNetShare {

QByteArray Protocol::serializeMessage(MessageType type, const nlohmann::json& payload) {
    nlohmann::json message;
    message["type"] = static_cast<int>(type);
    message["payload"] = payload;

    std::string jsonStr = message.dump();

    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);

    // 先写入消息长度（4字节）
    quint32 length = static_cast<quint32>(jsonStr.size());
    stream << length;

    // 再写入JSON数据
    result.append(jsonStr.c_str(), jsonStr.size());

    return result;
}

bool Protocol::deserializeMessage(const QByteArray& data, MessageType& type, nlohmann::json& payload) {
    if (data.size() < 4) {
        return false;
    }

    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_15);

    // 读取消息长度
    quint32 length;
    stream >> length;

    if (data.size() < static_cast<int>(4 + length)) {
        return false;
    }

    // 读取JSON数据
    QByteArray jsonData = data.mid(4, length);

    try {
        nlohmann::json message = nlohmann::json::parse(jsonData.constData(),
                                                        jsonData.constData() + jsonData.size());

        type = static_cast<MessageType>(message["type"].get<int>());
        payload = message["payload"];

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

nlohmann::json Protocol::fileMetadataToJson(const FileMetadata& meta) {
    nlohmann::json j;
    j["filename"] = meta.filename;
    j["relativePath"] = meta.relativePath;
    j["size"] = meta.size;
    j["createTime"] = timeToString(meta.createTime);
    j["modifyTime"] = timeToString(meta.modifyTime);
    j["ownerClient"] = meta.ownerClient;
    return j;
}

FileMetadata Protocol::jsonToFileMetadata(const nlohmann::json& j) {
    FileMetadata meta;
    meta.filename = j["filename"].get<std::string>();
    meta.relativePath = j["relativePath"].get<std::string>();
    meta.size = j["size"].get<uint64_t>();
    meta.createTime = stringToTime(j["createTime"].get<std::string>());
    meta.modifyTime = stringToTime(j["modifyTime"].get<std::string>());
    meta.ownerClient = j["ownerClient"].get<std::string>();
    return meta;
}

nlohmann::json Protocol::dateFilterToJson(const DateFilter& filter) {
    nlohmann::json j;
    j["startDate"] = timeToString(filter.startDate);
    j["endDate"] = timeToString(filter.endDate);
    return j;
}

DateFilter Protocol::jsonToDateFilter(const nlohmann::json& j) {
    DateFilter filter;
    filter.startDate = stringToTime(j["startDate"].get<std::string>());
    filter.endDate = stringToTime(j["endDate"].get<std::string>());
    return filter;
}

nlohmann::json Protocol::progressToJson(const TransferProgress& progress) {
    nlohmann::json j;
    j["bytesTransferred"] = progress.bytesTransferred;
    j["totalBytes"] = progress.totalBytes;
    j["fileIndex"] = progress.fileIndex;
    j["totalFiles"] = progress.totalFiles;
    return j;
}

TransferProgress Protocol::jsonToProgress(const nlohmann::json& j) {
    TransferProgress progress;
    progress.bytesTransferred = j["bytesTransferred"].get<uint64_t>();
    progress.totalBytes = j["totalBytes"].get<uint64_t>();
    progress.fileIndex = j["fileIndex"].get<int>();
    progress.totalFiles = j["totalFiles"].get<int>();
    return progress;
}

std::string Protocol::timeToString(time_t t) {
    std::tm* tm = std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

time_t Protocol::stringToTime(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::mktime(&tm);
}

}
