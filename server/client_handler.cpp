#include "client_handler.h"
#include "server.h"
#include "common/protocol.h"
#include "common/file_utils.h"
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QHostAddress>
#include <QEventLoop>
#include <QTimer>

namespace CrossNetShare {

ClientHandler::ClientHandler(QTcpSocket* socket, FileIndexer* indexer, QObject* parent)
    : QObject(parent)
    , socket_(socket)
    , indexer_(indexer)
    , server_(nullptr)
    , registered_(false)
{
    socket_->setParent(this);

    connect(socket_, &QTcpSocket::readyRead, this, &ClientHandler::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &ClientHandler::onDisconnected);
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &ClientHandler::onError);
}

ClientHandler::~ClientHandler() {
    if (registered_) {
        indexer_->removeClient(clientId_);
    }
}

QString ClientHandler::getClientAddress() const {
    return socket_->peerAddress().toString() + ":" + QString::number(socket_->peerPort());
}

void ClientHandler::onReadyRead() {
    receiveBuffer_.append(socket_->readAll());

    // 尝试解析消息
    while (receiveBuffer_.size() >= 4) {
        QDataStream stream(receiveBuffer_);
        stream.setVersion(QDataStream::Qt_5_15);

        quint32 length;
        stream >> length;

        // 检查是否接收完整消息
        if (receiveBuffer_.size() < static_cast<int>(4 + length)) {
            break;
        }

        // 提取完整消息
        QByteArray messageData = receiveBuffer_.left(4 + length);
        receiveBuffer_.remove(0, 4 + length);

        // 解析消息
        MessageType type;
        nlohmann::json payload;

        if (Protocol::deserializeMessage(messageData, type, payload)) {
            handleMessage(type, payload);
        } else {
            emit logMessage("Failed to parse message from " + getClientAddress());
        }
    }
}

void ClientHandler::onDisconnected() {
    emit logMessage("Client disconnected: " + getClientAddress());
    emit disconnected(clientId_);
    deleteLater();
}

void ClientHandler::onError(QAbstractSocket::SocketError error) {
    emit logMessage("Socket error from " + getClientAddress() + ": " + socket_->errorString());
}

void ClientHandler::handleMessage(MessageType type, const nlohmann::json& payload) {
    // 检查是否需要转发消息（用于文件传输转发）
    if (forwardState_.isActive && forwardState_.requester) {
        // 如果当前有活跃的转发状态，转发这些消息给请求者
        if (type == MessageType::DOWNLOAD_RESPONSE ||
            type == MessageType::FILE_DATA ||
            type == MessageType::FILE_COMPLETE ||
            type == MessageType::ERROR_MESSAGE) {

            forwardState_.requester->forwardMessage(type, payload);

            // 如果是完成或错误消息，清除转发状态
            if (type == MessageType::FILE_COMPLETE || type == MessageType::ERROR_MESSAGE) {
                emit logMessage("File forward completed for " + forwardState_.relativePath);
                forwardState_.isActive = false;
                forwardState_.requester = nullptr;
            }
            return;
        }
    }

    switch (type) {
    case MessageType::REGISTER_CLIENT:
        handleRegister(payload);
        break;

    case MessageType::REQUEST_FILE_LIST:
        handleFileListRequest(payload);
        break;

    case MessageType::REQUEST_FILTERED_FILES:
        handleFilteredFilesRequest(payload);
        break;

    case MessageType::DOWNLOAD_REQUEST:
        handleDownloadRequest(payload);
        break;

    case MessageType::UPLOAD_REQUEST:
        handleUploadRequest(payload);
        break;

    case MessageType::BATCH_DOWNLOAD_REQUEST:
        handleBatchDownloadRequest(payload);
        break;

    case MessageType::HEARTBEAT:
        // 回应心跳
        sendMessage(MessageType::HEARTBEAT, {});
        break;

    default:
        sendError("Unknown message type");
        break;
    }
}

void ClientHandler::handleRegister(const nlohmann::json& payload) {
    try {
        clientId_ = QString::fromStdString(payload["clientId"].get<std::string>());
        QString sharePath = QString::fromStdString(payload["sharePath"].get<std::string>());

        // 添加到索引器
        indexer_->addClientShare(clientId_, sharePath);
        registered_ = true;

        emit logMessage("Client registered: " + clientId_ + " from " + getClientAddress());
        emit clientRegistered(clientId_, sharePath);

        // 发送注册成功响应
        nlohmann::json response;
        response["success"] = true;
        response["message"] = "Registration successful";
        sendMessage(MessageType::REGISTER_RESPONSE, response);

    } catch (const std::exception& e) {
        sendError("Registration failed: " + QString(e.what()));
    }
}

void ClientHandler::handleFileListRequest(const nlohmann::json& payload) {
    try {
        std::string targetClient = payload.value("targetClient", "");

        std::vector<FileMetadata> files;
        if (targetClient.empty()) {
            // 获取所有文件
            files = indexer_->getAllFiles();
        } else {
            // 获取指定客户端的文件
            files = indexer_->getClientFiles(QString::fromStdString(targetClient));
        }

        nlohmann::json response;
        response["files"] = nlohmann::json::array();

        for (const auto& file : files) {
            response["files"].push_back(Protocol::fileMetadataToJson(file));
        }

        sendMessage(MessageType::FILE_LIST_RESPONSE, response);

    } catch (const std::exception& e) {
        sendError("Failed to get file list: " + QString(e.what()));
    }
}

void ClientHandler::handleFilteredFilesRequest(const nlohmann::json& payload) {
    try {
        DateFilter filter = Protocol::jsonToDateFilter(payload["filter"]);
        std::string targetClient = payload.value("targetClient", "");

        std::vector<FileMetadata> files;
        if (targetClient.empty()) {
            files = indexer_->filterByDate(filter);
        } else {
            files = indexer_->filterByClientAndDate(QString::fromStdString(targetClient), filter);
        }

        nlohmann::json response;
        response["files"] = nlohmann::json::array();

        for (const auto& file : files) {
            response["files"].push_back(Protocol::fileMetadataToJson(file));
        }

        sendMessage(MessageType::FILTERED_FILES_RESPONSE, response);

    } catch (const std::exception& e) {
        sendError("Failed to filter files: " + QString(e.what()));
    }
}

void ClientHandler::handleDownloadRequest(const nlohmann::json& payload) {
    try {
        std::string ownerClient = payload["ownerClient"].get<std::string>();
        std::string relativePath = payload["relativePath"].get<std::string>();
        QString ownerClientId = QString::fromStdString(ownerClient);

        // 检查文件所有者是谁
        if (ownerClientId == clientId_) {
            // 文件所有者就是当前客户端，直接读取本地文件并发送
            QString filePath = indexer_->resolveFilePath(ownerClientId, QString::fromStdString(relativePath));

            if (filePath.isEmpty() || !QFile::exists(filePath)) {
                sendError("File not found");
                return;
            }

            FileMetadata meta;
            meta.filename = QFileInfo(filePath).fileName().toStdString();
            meta.relativePath = relativePath;
            meta.size = FileUtils::getFileSize(filePath);
            meta.ownerClient = ownerClient;

            sendFile(filePath, meta);

        } else if (server_) {
            // 文件属于其他客户端，需要转发
            ClientHandler* ownerHandler = server_->findClientHandler(ownerClientId);

            if (!ownerHandler) {
                sendError("Owner client not connected: " + ownerClientId);
                return;
            }

            // 在文件所有者的Handler上记录转发状态
            ownerHandler->forwardState_.requester = this;
            ownerHandler->forwardState_.relativePath = QString::fromStdString(relativePath);
            ownerHandler->forwardState_.isActive = true;

            // 向文件所有者发送下载请求
            ownerHandler->sendMessage(MessageType::DOWNLOAD_REQUEST, payload);

            emit logMessage("Forwarding download request to " + ownerClientId + " for " + QString::fromStdString(relativePath));

        } else {
            sendError("Cannot forward request: server not available");
        }

    } catch (const std::exception& e) {
        sendError("Download failed: " + QString(e.what()));
    }
}

void ClientHandler::handleUploadRequest(const nlohmann::json& payload) {
    try {
        FileMetadata meta = Protocol::jsonToFileMetadata(payload["metadata"]);
        std::string fileDataBase64 = payload["data"].get<std::string>();

        // Base64解码
        QByteArray fileData = QByteArray::fromBase64(QByteArray::fromStdString(fileDataBase64));

        // 保存文件到本地共享目录
        QString savePath = indexer_->resolveFilePath(clientId_, QString::fromStdString(meta.relativePath));

        if (FileUtils::writeFile(savePath, fileData)) {
            // 刷新索引
            indexer_->refreshIndex(clientId_);

            nlohmann::json response;
            response["success"] = true;
            response["message"] = "Upload successful";
            sendMessage(MessageType::UPLOAD_RESPONSE, response);

            emit logMessage("File uploaded: " + QString::fromStdString(meta.filename));
        } else {
            sendError("Failed to save file");
        }

    } catch (const std::exception& e) {
        sendError("Upload failed: " + QString(e.what()));
    }
}

void ClientHandler::handleBatchDownloadRequest(const nlohmann::json& payload) {
    try {
        DateFilter filter = Protocol::jsonToDateFilter(payload["filter"]);
        std::string targetClient = payload.value("targetClient", "");

        std::vector<FileMetadata> files;
        if (targetClient.empty()) {
            files = indexer_->filterByDate(filter);
        } else {
            files = indexer_->filterByClientAndDate(QString::fromStdString(targetClient), filter);
        }

        // 发送批量下载响应
        nlohmann::json response;
        response["totalFiles"] = files.size();
        sendMessage(MessageType::BATCH_DOWNLOAD_RESPONSE, response);

        // 依次发送每个文件
        for (size_t i = 0; i < files.size(); ++i) {
            const auto& meta = files[i];

            QString filePath = indexer_->resolveFilePath(
                QString::fromStdString(meta.ownerClient),
                QString::fromStdString(meta.relativePath)
            );

            if (!filePath.isEmpty() && QFile::exists(filePath)) {
                sendFile(filePath, meta);
            }
        }

    } catch (const std::exception& e) {
        sendError("Batch download failed: " + QString(e.what()));
    }
}

void ClientHandler::sendMessage(MessageType type, const nlohmann::json& payload) {
    QByteArray data = Protocol::serializeMessage(type, payload);
    socket_->write(data);
    socket_->flush();
}

void ClientHandler::sendError(const QString& errorMsg) {
    nlohmann::json payload;
    payload["error"] = errorMsg.toStdString();
    sendMessage(MessageType::ERROR_MESSAGE, payload);

    emit logMessage("Error: " + errorMsg);
}

void ClientHandler::sendFile(const QString& filePath, const FileMetadata& meta) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendError("Failed to open file: " + filePath);
        return;
    }

    // 发送下载响应（包含文件元数据）
    nlohmann::json response;
    response["metadata"] = Protocol::fileMetadataToJson(meta);
    sendMessage(MessageType::DOWNLOAD_RESPONSE, response);

    // 分块发送文件数据
    const qint64 chunkSize = 64 * 1024; // 64KB
    qint64 totalSent = 0;

    while (!file.atEnd()) {
        QByteArray chunk = file.read(chunkSize);

        nlohmann::json dataMsg;
        dataMsg["data"] = chunk.toBase64().toStdString();
        dataMsg["size"] = chunk.size();
        sendMessage(MessageType::FILE_DATA, dataMsg);

        totalSent += chunk.size();
    }

    file.close();

    // 发送完成消息
    nlohmann::json completeMsg;
    completeMsg["totalBytes"] = totalSent;
    sendMessage(MessageType::FILE_COMPLETE, completeMsg);

    emit logMessage("File sent: " + QString::fromStdString(meta.filename) +
                    " (" + QString::number(totalSent) + " bytes)");
}

void ClientHandler::forwardMessage(MessageType type, const nlohmann::json& payload) {
    sendMessage(type, payload);
}

}
