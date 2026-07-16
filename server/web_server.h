#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QString>
#include <QByteArray>

namespace CrossNetShare {

class FileIndexer;
class AuthManager;

struct HttpRequest {
    QString method;         // GET, POST, etc.
    QString path;           // /api/login
    QString version;        // HTTP/1.1
    QMap<QString, QString> headers;
    QMap<QString, QString> params;  // Query parameters
    QByteArray body;
};

struct HttpResponse {
    int statusCode;
    QString statusText;
    QMap<QString, QString> headers;
    QByteArray body;

    HttpResponse() : statusCode(200), statusText("OK") {}
};

class WebServer : public QObject {
    Q_OBJECT

public:
    explicit WebServer(FileIndexer* indexer, QObject* parent = nullptr);
    ~WebServer();

    bool start(quint16 port = 8080);
    void stop();

    bool isRunning() const { return running_; }
    quint16 getPort() const { return port_; }

    void setAuthManager(AuthManager* authManager);

signals:
    void started();
    void stopped();
    void logMessage(const QString& message);
    void error(const QString& errorMsg);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    bool parseHttpRequest(const QByteArray& data, HttpRequest& request);
    void handleRequest(QTcpSocket* socket, const HttpRequest& request);
    void sendResponse(QTcpSocket* socket, const HttpResponse& response);

    // API handlers
    void handleLogin(QTcpSocket* socket, const HttpRequest& request);
    void handleLogout(QTcpSocket* socket, const HttpRequest& request);
    void handleFileList(QTcpSocket* socket, const HttpRequest& request);
    void handleFileSearch(QTcpSocket* socket, const HttpRequest& request);
    void handleFileDownload(QTcpSocket* socket, const HttpRequest& request);
    void handleFilePreview(QTcpSocket* socket, const HttpRequest& request);

    // Static file serving
    void serveStaticFile(QTcpSocket* socket, const QString& path);
    void serveLoginPage(QTcpSocket* socket);
    void serveBrowsePage(QTcpSocket* socket);

    // Helper functions
    QString getMimeType(const QString& filename);
    bool isAuthenticated(const HttpRequest& request, QString& username);
    QByteArray buildHttpResponse(const HttpResponse& response);

    QTcpServer* tcpServer_;
    FileIndexer* indexer_;
    AuthManager* authManager_;
    bool running_;
    quint16 port_;

    QMap<QTcpSocket*, QByteArray> socketBuffers_;
};

}
