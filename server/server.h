#pragma once

#include "file_indexer.h"
#include "client_handler.h"
#include <QObject>
#include <QTcpServer>
#include <QList>
#include <QHostAddress>

namespace CrossNetShare {

struct ServerConfig {
    quint16 port;
    bool uploadEnabled;
    QList<QHostAddress> listenAddresses;

    ServerConfig() : port(8888), uploadEnabled(true) {}
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

    // 获取已连接的客户端列表
    QStringList getConnectedClients() const;

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

private:
    ServerConfig config_;
    bool running_;
    QList<QTcpServer*> servers_;
    QList<ClientHandler*> clients_;
    FileIndexer indexer_;
};

}
