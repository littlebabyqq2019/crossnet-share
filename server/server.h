#pragma once

#include "file_indexer.h"
#include "client_handler.h"
#include "file_watcher.h"
#include <QObject>
#include <QTcpServer>
#include <QList>
#include <QHostAddress>

namespace CrossNetShare {

class WebServer;
class AuthManager;

struct ServerConfig {
    quint16 port;
    bool uploadEnabled;
    QList<QHostAddress> listenAddresses;

    // Web server config
    bool webEnabled;
    quint16 webPort;

    ServerConfig() : port(8888), uploadEnabled(true), webEnabled(true), webPort(8080) {}
};

class Server : public QObject {
    Q_OBJECT

public:
    explicit Server(QObject* parent = nullptr);
    ~Server();

    bool start(const ServerConfig& config);
    void stop();

    bool isRunning() const { return running_; }
    ServerConfig getConfig() const { return config_; }
    void setConfig(const ServerConfig& config);

    FileIndexer* getIndexer() { return &indexer_; }
    WebServer* getWebServer() { return webServer_; }

    void setWatermarkService(WatermarkService* watermarkService);

    // 获取已连接的客户端列表
    QStringList getConnectedClients() const;

    // 根据clientId查找对应的ClientHandler
    ClientHandler* findClientHandler(const QString& clientId) const;

    // 请求所有在线客户端刷新文件列表
    void requestAllClientsRefresh();

signals:
    void started();
    void stopped();
    void clientConnected(const QString& clientId, const QString& address);
    void clientDisconnected(const QString& clientId);
    void logMessage(const QString& message);
    void error(const QString& errorMsg);

private slots:
    void onNewConnection();
    void onClientDisconnected(const QString& clientId);
    void onClientLog(const QString& message);
    void onClientRegistered(const QString& clientId, const QString& sharePath);
    void onFileWatcherLog(const QString& message);
    void onDirectoryChanged(const QString& clientId, const QString& path);

private:
    ServerConfig config_;
    bool running_;
    QList<QTcpServer*> servers_;
    QList<ClientHandler*> clients_;
    FileIndexer indexer_;
    FileWatcher* fileWatcher_;
    WebServer* webServer_;
    AuthManager* authManager_;
};

}
