#pragma once

#include "common/message.h"
#include <nlohmann/json.hpp>
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QTimer>
#include <functional>

namespace CrossNetShare {

class Client : public QObject {
    Q_OBJECT

public:
    explicit Client(QObject* parent = nullptr);
    ~Client();

    // 连接到服务器
    bool connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    void enableAutoReconnect(bool enable);

    bool isConnected() const { return connected_; }

    // 注册客户端
    void registerClient(const QString& clientId, const QString& sharePath);

    // 保存/加载配置
    void saveConfig(const QString& configPath);
    bool loadConfig(const QString& configPath);

    // 请求文件列表
    void requestFileList(const QString& targetClient = "");

    // 请求按日期筛选的文件
    void requestFilteredFiles(const DateFilter& filter, const QString& targetClient = "");

    // 下载单个文件
    void downloadFile(const QString& ownerClient, const QString& relativePath,
                     const QString& savePath);

    // 批量下载
    void batchDownload(const DateFilter& filter, const QString& targetClient,
                      const QString& saveDir);

    // 上传文件
    void uploadFile(const QString& localPath, const QString& relativePath);

signals:
    void connected();
    void disconnected();
    void registered(bool success, const QString& message);
    void fileListReceived(const std::vector<FileMetadata>& files);
    void downloadProgress(const QString& filename, qint64 current, qint64 total);
    void downloadComplete(const QString& filename, const QString& savePath);
    void uploadComplete(bool success, const QString& message);
    void batchDownloadProgress(int currentFile, int totalFiles);
    void batchDownloadComplete(int successCount, int totalCount);
    void error(const QString& errorMsg);
    void logMessage(const QString& message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);

private:
    void handleMessage(MessageType type, const nlohmann::json& payload);
    void handleRegisterResponse(const nlohmann::json& payload);
    void handleFileListResponse(const nlohmann::json& payload);
    void handleFilteredFilesResponse(const nlohmann::json& payload);
    void handleDownloadRequest(const nlohmann::json& payload);
    void handleDownloadResponse(const nlohmann::json& payload);
    void handleFileData(const nlohmann::json& payload);
    void handleFileComplete(const nlohmann::json& payload);
    void handleUploadResponse(const nlohmann::json& payload);
    void handleBatchDownloadResponse(const nlohmann::json& payload);
    void handleErrorMessage(const nlohmann::json& payload);

    void sendMessage(MessageType type, const nlohmann::json& payload);

    QTcpSocket* socket_;
    bool connected_;
    QByteArray receiveBuffer_;

    // 自动重连
    QTimer* reconnectTimer_;
    bool autoReconnect_;
    QString serverHost_;
    quint16 serverPort_;
    int reconnectAttempts_;

    // 配置信息
    QString clientId_;
    QString sharePath_;

    // 当前下载状态
    struct DownloadState {
        QString filename;
        QString savePath;
        QByteArray fileData;
        qint64 totalBytes;
        bool active;

        DownloadState() : totalBytes(0), active(false) {}
    };
    DownloadState currentDownload_;

    // 批量下载状态
    struct BatchDownloadState {
        int totalFiles;
        int currentFile;
        int successCount;
        bool active;

        BatchDownloadState() : totalFiles(0), currentFile(0), successCount(0), active(false) {}
    };
    BatchDownloadState batchDownload_;
};

}
