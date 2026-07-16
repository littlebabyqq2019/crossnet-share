#include "server.h"
#include "web_server.h"
#include "auth_manager.h"
#include <QCoreApplication>
#include <QNetworkInterface>

namespace CrossNetShare {

Server::Server(QObject* parent)
    : QObject(parent)
    , running_(false)
    , fileWatcher_(new FileWatcher(this))
    , webServer_(new WebServer(&indexer_, this))
    , authManager_(new AuthManager(this))
{
    webServer_->setAuthManager(authManager_);
    webServer_->setServer(this);
    authManager_->loadUsers(QCoreApplication::applicationDirPath() + "/users.json");

    connect(fileWatcher_, &FileWatcher::logMessage, this, &Server::onFileWatcherLog);
    connect(fileWatcher_, &FileWatcher::directoryChanged, this, &Server::onDirectoryChanged);
    connect(webServer_, &WebServer::logMessage, this, &Server::logMessage);
    connect(webServer_, &WebServer::error, this, &Server::error);
}

Server::~Server() {
    stop();
}

bool Server::start(const ServerConfig& config) {
    if (running_) {
        emit error("Server is already running");
        return false;
    }

    config_ = config;

    // 如果没有指定监听地址，监听所有网卡
    QList<QHostAddress> addresses = config_.listenAddresses;
    if (addresses.isEmpty()) {
        addresses.append(QHostAddress::Any);
    }

    // 为每个地址创建一个服务器
    for (const QHostAddress& address : addresses) {
        QTcpServer* server = new QTcpServer(this);

        if (!server->listen(address, config_.port)) {
            emit error("Failed to listen on " + address.toString() + ":" +
                      QString::number(config_.port) + " - " + server->errorString());

            // 清理已创建的服务器
            for (QTcpServer* s : servers_) {
                s->close();
                s->deleteLater();
            }
            servers_.clear();

            return false;
        }

        connect(server, &QTcpServer::newConnection, this, &Server::onNewConnection);
        servers_.append(server);

        emit logMessage("Listening on " + address.toString() + ":" + QString::number(config_.port));
    }

    // 启动Web服务器
    if (config_.webEnabled) {
        if (webServer_->start(config_.webPort)) {
            emit logMessage("Web access: http://0.0.0.0:" + QString::number(config_.webPort));
        } else {
            emit logMessage("Warning: Web server failed to start on port " + QString::number(config_.webPort));
        }
    }

    running_ = true;
    emit started();
    emit logMessage("Server started successfully");

    return true;
}

void Server::stop() {
    if (!running_) {
        return;
    }

    // 停止Web服务器
    if (webServer_) {
        webServer_->stop();
    }

    // 停止文件监控
    if (fileWatcher_) {
        fileWatcher_->clearAll();
    }

    // 关闭所有服务器
    for (QTcpServer* server : servers_) {
        server->close();
        server->deleteLater();
    }
    servers_.clear();

    // 断开所有客户端
    for (ClientHandler* client : clients_) {
        client->deleteLater();
    }
    clients_.clear();

    running_ = false;
    emit stopped();
    emit logMessage("Server stopped");
}

void Server::setConfig(const ServerConfig& config) {
    bool needRestart = running_ && (config.port != config_.port ||
                                     config.listenAddresses != config_.listenAddresses);

    config_ = config;

    if (needRestart) {
        stop();
        start(config_);
    }
}

QStringList Server::getConnectedClients() const {
    QStringList clients;
    for (const ClientHandler* client : clients_) {
        if (client->isRegistered()) {
            clients.append(client->getClientId());
        }
    }
    return clients;
}

ClientHandler* Server::findClientHandler(const QString& clientId) const {
    for (ClientHandler* client : clients_) {
        if (client->isRegistered() && client->getClientId() == clientId) {
            return client;
        }
    }
    return nullptr;
}

void Server::onNewConnection() {
    QTcpServer* server = qobject_cast<QTcpServer*>(sender());
    if (!server) {
        return;
    }

    while (server->hasPendingConnections()) {
        QTcpSocket* socket = server->nextPendingConnection();

        ClientHandler* handler = new ClientHandler(socket, &indexer_, this);
        handler->setServer(this);
        clients_.append(handler);

        connect(handler, &ClientHandler::disconnected, this, &Server::onClientDisconnected);
        connect(handler, &ClientHandler::logMessage, this, &Server::onClientLog);
        connect(handler, &ClientHandler::clientRegistered, this, &Server::onClientRegistered);

        emit logMessage("New connection from " + handler->getClientAddress());
    }
}

void Server::onClientDisconnected(const QString& clientId) {
    ClientHandler* handler = qobject_cast<ClientHandler*>(sender());
    if (handler) {
        clients_.removeOne(handler);
        emit clientDisconnected(clientId);
    }
}

void Server::onClientLog(const QString& message) {
    emit logMessage(message);
}

void Server::onClientRegistered(const QString& clientId, const QString& sharePath) {
    ClientHandler* handler = qobject_cast<ClientHandler*>(sender());
    if (handler) {
        emit clientConnected(clientId, handler->getClientAddress());
        emit logMessage("Client '" + clientId + "' registered with share path: " + sharePath);

        // 开始监控该客户端的共享目录
        fileWatcher_->addWatchPath(clientId, sharePath);
    }
}

void Server::onFileWatcherLog(const QString& message) {
    emit logMessage("[FileWatcher] " + message);
}

void Server::onDirectoryChanged(const QString& clientId, const QString& path) {
    Q_UNUSED(path)
    // 目录内容变化，刷新该客户端的文件索引
    indexer_.refreshIndex(clientId);
    emit logMessage("Auto-refreshed index for client: " + clientId);
}

}
