#pragma once

#include "common/message.h"
#include "file_indexer.h"
#include <nlohmann/json.hpp>
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>

namespace CrossNetShare {

class Server;
class ClientHandler;

// 文件转发状态
struct FileForwardState {
    ClientHandler* requester;  // 请求者的Handler
    QString relativePath;      // 文件相对路径
    bool isActive;             // 是否正在转发

    FileForwardState() : requester(nullptr), isActive(false) {}
};

class ClientHandler : public QObject {
    Q_OBJECT

public:
    explicit ClientHandler(QTcpSocket* socket, FileIndexer* indexer, QObject* parent = nullptr);
    ~ClientHandler();

    void setServer(Server* server) { server_ = server; }

    QString getClientId() const { return clientId_; }
    QString getClientAddress() const;
    bool isRegistered() const { return registered_; }

    // 同步请求文件数据（用于Web服务器）
    struct FileRequestResult {
        bool success;
        QByteArray data;
        QString error;
    };
    FileRequestResult requestFileSync(const QString& relativePath, int timeoutMs = 30000);

signals:
    void disconnected(const QString& clientId);
    void logMessage(const QString& message);
    void clientRegistered(const QString& clientId, const QString& sharePath);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);

private:
    void handleMessage(MessageType type, const nlohmann::json& payload);
    void handleRegister(const nlohmann::json& payload);
    void handleFileListRequest(const nlohmann::json& payload);
    void handleFilteredFilesRequest(const nlohmann::json& payload);
    void handleDownloadRequest(const nlohmann::json& payload);
    void handleUploadRequest(const nlohmann::json& payload);
    void handleBatchDownloadRequest(const nlohmann::json& payload);

    void sendMessage(MessageType type, const nlohmann::json& payload);
    void sendError(const QString& errorMsg);
    void sendFile(const QString& filePath, const FileMetadata& meta);

    // 文件转发相关
    void forwardMessage(MessageType type, const nlohmann::json& payload);

    QTcpSocket* socket_;
    FileIndexer* indexer_;
    Server* server_;
    QString clientId_;
    bool registered_;
    QByteArray receiveBuffer_;

    // 转发状态：当前这个Handler正在为哪个请求者转发文件
    FileForwardState forwardState_;

    // 同步文件请求状态（用于Web服务器）
    bool syncRequestActive_;
    QByteArray syncRequestData_;
    bool syncRequestCompleted_;
    QString syncRequestError_;
};

}
