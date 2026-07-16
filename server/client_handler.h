#pragma once

#include "common/message.h"
#include "file_indexer.h"
#include <nlohmann/json.hpp>
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>

namespace CrossNetShare {

class ClientHandler : public QObject {
    Q_OBJECT

public:
    explicit ClientHandler(QTcpSocket* socket, FileIndexer* indexer, QObject* parent = nullptr);
    ~ClientHandler();

    QString getClientId() const { return clientId_; }
    QString getClientAddress() const;
    bool isRegistered() const { return registered_; }

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

    QTcpSocket* socket_;
    FileIndexer* indexer_;
    QString clientId_;
    bool registered_;
    QByteArray receiveBuffer_;
};

}
