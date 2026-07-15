#include "client.h"
#include "common/protocol.h"
#include "common/file_utils.h"
#include <QDataStream>

namespace CrossNetShare {

Client::Client(QObject* parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , connected_(false)
{
    connect(socket_, &QTcpSocket::connected, this, &Client::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &Client::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &Client::onReadyRead);
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &Client::onError);
}

Client::~Client() {
    disconnectFromServer();
}

bool Client::connectToServer(const QString& host, quint16 port) {
    if (connected_) {
        emit error("Already connected");
        return false;
    }

    emit logMessage("Connecting to " + host + ":" + QString::number(port) + "...");
    socket_->connectToHost(host, port);

    // 等待连接（最多5秒）
    if (!socket_->waitForConnected(5000)) {
        emit error("Connection failed: " + socket_->errorString());
        return false;
    }

    return true;
}

void Client::disconnectFromServer() {
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            socket_->waitForDisconnected(1000);
        }
    }
}

void Client::registerClient(const QString& clientId, const QString& sharePath) {
    nlohmann::json payload;
    payload["clientId"] = clientId.toStdString();
    payload["sharePath"] = sharePath.toStdString();

    sendMessage(MessageType::REGISTER_CLIENT, payload);
    emit logMessage("Registering client: " + clientId);
}

void Client::requestFileList(const QString& targetClient) {
    nlohmann::json payload;
    if (!targetClient.isEmpty()) {
        payload["targetClient"] = targetClient.toStdString();
    }

    sendMessage(MessageType::REQUEST_FILE_LIST, payload);
    emit logMessage("Requesting file list...");
}

void Client::requestFilteredFiles(const DateFilter& filter, const QString& targetClient) {
    nlohmann::json payload;
    payload["filter"] = Protocol::dateFilterToJson(filter);
    if (!targetClient.isEmpty()) {
        payload["targetClient"] = targetClient.toStdString();
    }

    sendMessage(MessageType::REQUEST_FILTERED_FILES, payload);
    emit logMessage("Requesting filtered files...");
}

void Client::downloadFile(const QString& ownerClient, const QString& relativePath,
                         const QString& savePath) {
    currentDownload_.filename = relativePath;
    currentDownload_.savePath = savePath;
    currentDownload_.fileData.clear();
    currentDownload_.totalBytes = 0;
    currentDownload_.active = true;

    nlohmann::json payload;
    payload["ownerClient"] = ownerClient.toStdString();
    payload["relativePath"] = relativePath.toStdString();

    sendMessage(MessageType::DOWNLOAD_REQUEST, payload);
    emit logMessage("Downloading: " + relativePath);
}

void Client::batchDownload(const DateFilter& filter, const QString& targetClient,
                          const QString& saveDir) {
    batchDownload_.active = true;
    batchDownload_.currentFile = 0;
    batchDownload_.successCount = 0;
    batchDownload_.totalFiles = 0;

    nlohmann::json payload;
    payload["filter"] = Protocol::dateFilterToJson(filter);
    if (!targetClient.isEmpty()) {
        payload["targetClient"] = targetClient.toStdString();
    }
    payload["saveDir"] = saveDir.toStdString();

    sendMessage(MessageType::BATCH_DOWNLOAD_REQUEST, payload);
    emit logMessage("Starting batch download...");
}

void Client::uploadFile(const QString& localPath, const QString& relativePath) {
    QByteArray fileData = FileUtils::readFile(localPath);
    if (fileData.isEmpty()) {
        emit error("Failed to read file: " + localPath);
        return;
    }

    FileMetadata meta;
    meta.filename = QFileInfo(localPath).fileName().toStdString();
    meta.relativePath = relativePath.toStdString();
    meta.size = fileData.size();
    meta.createTime = FileUtils::getFileCreateTime(localPath);
    meta.modifyTime = FileUtils::getFileModifyTime(localPath);

    nlohmann::json payload;
    payload["metadata"] = Protocol::fileMetadataToJson(meta);
    payload["data"] = fileData.toBase64().toStdString();

    sendMessage(MessageType::UPLOAD_REQUEST, payload);
    emit logMessage("Uploading: " + relativePath);
}

void Client::onConnected() {
    connected_ = true;
    emit connected();
    emit logMessage("Connected to server");
}

void Client::onDisconnected() {
    connected_ = false;
    emit disconnected();
    emit logMessage("Disconnected from server");
}

void Client::onReadyRead() {
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
            emit error("Failed to parse message");
        }
    }
}

void Client::onError(QAbstractSocket::SocketError socketError) {
    emit error("Socket error: " + socket_->errorString());
}

void Client::handleMessage(MessageType type, const nlohmann::json& payload) {
    switch (type) {
    case MessageType::REGISTER_RESPONSE:
        handleRegisterResponse(payload);
        break;

    case MessageType::FILE_LIST_RESPONSE:
        handleFileListResponse(payload);
        break;

    case MessageType::FILTERED_FILES_RESPONSE:
        handleFilteredFilesResponse(payload);
        break;

    case MessageType::DOWNLOAD_RESPONSE:
        handleDownloadResponse(payload);
        break;

    case MessageType::FILE_DATA:
        handleFileData(payload);
        break;

    case MessageType::FILE_COMPLETE:
        handleFileComplete(payload);
        break;

    case MessageType::UPLOAD_RESPONSE:
        handleUploadResponse(payload);
        break;

    case MessageType::BATCH_DOWNLOAD_RESPONSE:
        handleBatchDownloadResponse(payload);
        break;

    case MessageType::ERROR_MESSAGE:
        handleErrorMessage(payload);
        break;

    default:
        emit error("Unknown message type");
        break;
    }
}

void Client::handleRegisterResponse(const nlohmann::json& payload) {
    bool success = payload["success"].get<bool>();
    QString message = QString::fromStdString(payload["message"].get<std::string>());

    emit registered(success, message);
    emit logMessage("Registration: " + message);
}

void Client::handleFileListResponse(const nlohmann::json& payload) {
    std::vector<FileMetadata> files;

    for (const auto& fileJson : payload["files"]) {
        files.push_back(Protocol::jsonToFileMetadata(fileJson));
    }

    emit fileListReceived(files);
    emit logMessage("Received " + QString::number(files.size()) + " files");
}

void Client::handleFilteredFilesResponse(const nlohmann::json& payload) {
    handleFileListResponse(payload);
}

void Client::handleDownloadResponse(const nlohmann::json& payload) {
    if (!currentDownload_.active && !batchDownload_.active) {
        return;
    }

    FileMetadata meta = Protocol::jsonToFileMetadata(payload["metadata"]);
    currentDownload_.filename = QString::fromStdString(meta.filename);
    currentDownload_.totalBytes = meta.size;
    currentDownload_.fileData.clear();

    emit logMessage("Receiving file: " + currentDownload_.filename);
}

void Client::handleFileData(const nlohmann::json& payload) {
    if (!currentDownload_.active && !batchDownload_.active) {
        return;
    }

    std::string dataBase64 = payload["data"].get<std::string>();
    QByteArray chunk = QByteArray::fromBase64(QByteArray::fromStdString(dataBase64));

    currentDownload_.fileData.append(chunk);

    emit downloadProgress(currentDownload_.filename,
                         currentDownload_.fileData.size(),
                         currentDownload_.totalBytes);
}

void Client::handleFileComplete(const nlohmann::json& payload) {
    if (!currentDownload_.active && !batchDownload_.active) {
        return;
    }

    // 保存文件
    if (FileUtils::writeFile(currentDownload_.savePath, currentDownload_.fileData)) {
        emit downloadComplete(currentDownload_.filename, currentDownload_.savePath);
        emit logMessage("Downloaded: " + currentDownload_.filename);

        if (batchDownload_.active) {
            batchDownload_.successCount++;
            batchDownload_.currentFile++;
            emit batchDownloadProgress(batchDownload_.currentFile, batchDownload_.totalFiles);

            if (batchDownload_.currentFile >= batchDownload_.totalFiles) {
                emit batchDownloadComplete(batchDownload_.successCount, batchDownload_.totalFiles);
                batchDownload_.active = false;
            }
        }
    } else {
        emit error("Failed to save file: " + currentDownload_.savePath);
    }

    currentDownload_.active = false;
    currentDownload_.fileData.clear();
}

void Client::handleUploadResponse(const nlohmann::json& payload) {
    bool success = payload["success"].get<bool>();
    QString message = QString::fromStdString(payload["message"].get<std::string>());

    emit uploadComplete(success, message);
    emit logMessage("Upload: " + message);
}

void Client::handleBatchDownloadResponse(const nlohmann::json& payload) {
    batchDownload_.totalFiles = payload["totalFiles"].get<int>();
    batchDownload_.currentFile = 0;
    batchDownload_.successCount = 0;

    emit logMessage("Batch download: " + QString::number(batchDownload_.totalFiles) + " files");
    emit batchDownloadProgress(0, batchDownload_.totalFiles);
}

void Client::handleErrorMessage(const nlohmann::json& payload) {
    QString errorMsg = QString::fromStdString(payload["error"].get<std::string>());
    emit error(errorMsg);
}

void Client::sendMessage(MessageType type, const nlohmann::json& payload) {
    QByteArray data = Protocol::serializeMessage(type, payload);
    socket_->write(data);
    socket_->flush();
}

}
