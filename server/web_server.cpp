#include "web_server.h"
#include "server.h"
#include "client_handler.h"
#include "auth_manager.h"
#include "document_converter.h"
#include "file_indexer.h"
#include "common/protocol.h"
#include <nlohmann/json.hpp>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

namespace CrossNetShare {

namespace {
QString urlDecode(const QString& value) {
    return QUrl::fromPercentEncoding(value.toUtf8());
}

QString cookieValue(const HttpRequest& request, const QString& name) {
    QString cookie = request.headers.value("cookie");
    const QStringList parts = cookie.split(';', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        QStringList kv = part.trimmed().split('=');
        if (kv.size() == 2 && kv[0] == name) {
            return kv[1];
        }
    }
    return QString();
}

QByteArray jsonResponse(const nlohmann::json& json) {
    return QByteArray::fromStdString(json.dump());
}

QString fileId(const FileMetadata& file) {
    return QUrl::toPercentEncoding(QString::fromStdString(file.ownerClient)) + "|" +
           QUrl::toPercentEncoding(QString::fromStdString(file.relativePath));
}

bool parseFileId(const QString& id, QString& clientId, QString& relativePath) {
    const QStringList parts = id.split('|');
    if (parts.size() != 2) {
        return false;
    }
    clientId = QUrl::fromPercentEncoding(parts[0].toUtf8());
    relativePath = QUrl::fromPercentEncoding(parts[1].toUtf8());
    return !clientId.isEmpty() && !relativePath.isEmpty();
}

QString humanSize(quint64 size) {
    if (size < 1024) return QString::number(size) + " B";
    if (size < 1024 * 1024) return QString::number(size / 1024.0, 'f', 1) + " KB";
    if (size < 1024ULL * 1024ULL * 1024ULL) return QString::number(size / 1024.0 / 1024.0, 'f', 1) + " MB";
    return QString::number(size / 1024.0 / 1024.0 / 1024.0, 'f', 1) + " GB";
}
}

WebServer::WebServer(FileIndexer* indexer, QObject* parent)
    : QObject(parent)
    , tcpServer_(new QTcpServer(this))
    , indexer_(indexer)
    , authManager_(nullptr)
    , server_(nullptr)
    , running_(false)
    , port_(8080)
{
    connect(tcpServer_, &QTcpServer::newConnection, this, &WebServer::onNewConnection);
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start(quint16 port) {
    if (running_) {
        return true;
    }

    port_ = port;
    if (!tcpServer_->listen(QHostAddress::Any, port_)) {
        emit error("Web server failed to listen on port " + QString::number(port_) + ": " + tcpServer_->errorString());
        return false;
    }

    running_ = true;
    emit started();
    emit logMessage("Web server listening on http://0.0.0.0:" + QString::number(port_));
    return true;
}

void WebServer::stop() {
    if (!running_) {
        return;
    }

    tcpServer_->close();
    for (QTcpSocket* socket : socketBuffers_.keys()) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    socketBuffers_.clear();

    running_ = false;
    emit stopped();
    emit logMessage("Web server stopped");
}

void WebServer::setAuthManager(AuthManager* authManager) {
    authManager_ = authManager;
}

void WebServer::setServer(Server* server) {
    server_ = server;
}

void WebServer::onNewConnection() {
    while (tcpServer_->hasPendingConnections()) {
        QTcpSocket* socket = tcpServer_->nextPendingConnection();
        socketBuffers_[socket] = QByteArray();
        connect(socket, &QTcpSocket::readyRead, this, &WebServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &WebServer::onDisconnected);
    }
}

void WebServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }

    socketBuffers_[socket].append(socket->readAll());
    HttpRequest request;
    if (!parseHttpRequest(socketBuffers_[socket], request)) {
        return;
    }

    socketBuffers_[socket].clear();
    handleRequest(socket, request);
}

void WebServer::onDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        socketBuffers_.remove(socket);
        socket->deleteLater();
    }
}

bool WebServer::parseHttpRequest(const QByteArray& data, HttpRequest& request) {
    int headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return false;
    }

    QByteArray headerData = data.left(headerEnd);
    QList<QByteArray> lines = headerData.split('\n');
    if (lines.isEmpty()) {
        return false;
    }

    QList<QByteArray> requestLine = lines[0].trimmed().split(' ');
    if (requestLine.size() < 3) {
        return false;
    }

    request.method = QString::fromLatin1(requestLine[0]).toUpper();
    QString rawUrl = QString::fromUtf8(requestLine[1]);
    request.version = QString::fromLatin1(requestLine[2]);

    QUrl url(rawUrl);
    request.path = url.path();
    QUrlQuery query(url);
    const auto items = query.queryItems(QUrl::FullyDecoded);
    for (const auto& item : items) {
        request.params[item.first] = item.second;
    }

    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i].trimmed();
        int colon = line.indexOf(':');
        if (colon > 0) {
            QString key = QString::fromLatin1(line.left(colon)).toLower();
            QString value = QString::fromUtf8(line.mid(colon + 1).trimmed());
            request.headers[key] = value;
        }
    }

    int contentLength = request.headers.value("content-length").toInt();
    if (data.size() < headerEnd + 4 + contentLength) {
        return false;
    }

    request.body = data.mid(headerEnd + 4, contentLength);
    return true;
}

void WebServer::handleRequest(QTcpSocket* socket, const HttpRequest& request) {
    if (request.path == "/" || request.path == "/login") {
        serveLoginPage(socket);
        return;
    }
    if (request.path == "/browse") {
        QString username;
        if (!isAuthenticated(request, username)) {
            HttpResponse response;
            response.statusCode = 302;
            response.statusText = "Found";
            response.headers["Location"] = "/login";
            sendResponse(socket, response);
            return;
        }
        serveBrowsePage(socket);
        return;
    }
    if (request.path == "/api/login") {
        handleLogin(socket, request);
        return;
    }
    if (request.path == "/api/logout") {
        handleLogout(socket, request);
        return;
    }

    QString username;
    if (!isAuthenticated(request, username)) {
        HttpResponse response;
        response.statusCode = 401;
        response.statusText = "Unauthorized";
        response.headers["Content-Type"] = "application/json; charset=utf-8";
        response.body = jsonResponse({{"success", false}, {"error", "Unauthorized"}});
        sendResponse(socket, response);
        return;
    }

    if (request.path == "/api/files") {
        handleFileList(socket, request);
    } else if (request.path == "/api/search") {
        handleFileSearch(socket, request);
    } else if (request.path == "/api/download") {
        handleFileDownload(socket, request);
    } else if (request.path == "/api/batch-download") {
        handleBatchDownload(socket, request);
    } else if (request.path == "/api/preview") {
        handleFilePreview(socket, request);
    } else {
        HttpResponse response;
        response.statusCode = 404;
        response.statusText = "Not Found";
        response.headers["Content-Type"] = "text/plain; charset=utf-8";
        response.body = "Not Found";
        sendResponse(socket, response);
    }
}

void WebServer::sendResponse(QTcpSocket* socket, const HttpResponse& response) {
    socket->write(buildHttpResponse(response));
    socket->flush();
    socket->disconnectFromHost();
}

void WebServer::handleLogin(QTcpSocket* socket, const HttpRequest& request) {
    HttpResponse response;
    response.headers["Content-Type"] = "application/json; charset=utf-8";

    if (!authManager_) {
        response.statusCode = 500;
        response.statusText = "Internal Server Error";
        response.body = jsonResponse({{"success", false}, {"error", "Authentication is not configured"}});
        sendResponse(socket, response);
        return;
    }

    QString username;
    QString password;
    try {
        auto body = nlohmann::json::parse(request.body.constData(), request.body.constData() + request.body.size());
        username = QString::fromStdString(body.value("username", ""));
        password = QString::fromStdString(body.value("password", ""));
    } catch (...) {
        response.statusCode = 400;
        response.statusText = "Bad Request";
        response.body = jsonResponse({{"success", false}, {"error", "Invalid JSON"}});
        sendResponse(socket, response);
        return;
    }

    QString token = authManager_->authenticate(username, password);
    if (token.isEmpty()) {
        response.statusCode = 403;
        response.statusText = "Forbidden";
        response.body = jsonResponse({{"success", false}, {"error", "Invalid username or password"}});
        sendResponse(socket, response);
        return;
    }

    response.headers["Set-Cookie"] = "CNS_SESSION=" + token + "; Path=/; HttpOnly; SameSite=Strict";
    response.body = jsonResponse({{"success", true}, {"redirect", "/browse"}});
    sendResponse(socket, response);
}

void WebServer::handleLogout(QTcpSocket* socket, const HttpRequest& request) {
    if (authManager_) {
        authManager_->logout(cookieValue(request, "CNS_SESSION"));
    }

    HttpResponse response;
    response.statusCode = 302;
    response.statusText = "Found";
    response.headers["Location"] = "/login";
    response.headers["Set-Cookie"] = "CNS_SESSION=deleted; Path=/; Max-Age=0";
    sendResponse(socket, response);
}

void WebServer::handleFileList(QTcpSocket* socket, const HttpRequest& request) {
    Q_UNUSED(request)
    std::vector<FileMetadata> files = indexer_->getAllFiles();

    nlohmann::json root;
    root["success"] = true;
    root["files"] = nlohmann::json::array();
    for (const auto& file : files) {
        nlohmann::json item = Protocol::fileMetadataToJson(file);
        item["id"] = fileId(file).toStdString();
        item["humanSize"] = humanSize(file.size).toStdString();
        item["previewSupported"] = DocumentConverter::isPreviewSupported(
            indexer_->resolveFilePath(QString::fromStdString(file.ownerClient), QString::fromStdString(file.relativePath)));
        root["files"].push_back(item);
    }

    HttpResponse response;
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    response.body = jsonResponse(root);
    sendResponse(socket, response);
}

void WebServer::handleFileSearch(QTcpSocket* socket, const HttpRequest& request) {
    QString keyword = request.params.value("q").trimmed();
    std::vector<FileMetadata> files = indexer_->getAllFiles();

    nlohmann::json root;
    root["success"] = true;
    root["files"] = nlohmann::json::array();

    for (const auto& file : files) {
        QString filename = QString::fromStdString(file.filename);
        QString relativePath = QString::fromStdString(file.relativePath);
        if (!keyword.isEmpty() && !filename.contains(keyword, Qt::CaseInsensitive) &&
            !relativePath.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }

        nlohmann::json item = Protocol::fileMetadataToJson(file);
        item["id"] = fileId(file).toStdString();
        item["humanSize"] = humanSize(file.size).toStdString();
        root["files"].push_back(item);
    }

    HttpResponse response;
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    response.body = jsonResponse(root);
    sendResponse(socket, response);
}

void WebServer::handleFileDownload(QTcpSocket* socket, const HttpRequest& request) {
    QString clientId;
    QString relativePath;
    if (!parseFileId(request.params.value("id"), clientId, relativePath)) {
        HttpResponse response;
        response.statusCode = 400;
        response.statusText = "Bad Request";
        response.body = "Invalid file id";
        sendResponse(socket, response);
        return;
    }

    QString fullPath = indexer_->resolveFilePath(clientId, relativePath);
    QFileInfo info(fullPath);

    // 首先尝试本地文件
    if (info.exists() && info.isFile()) {
        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            HttpResponse response;
            response.headers["Content-Type"] = "application/octet-stream";
            response.headers["Content-Disposition"] = "attachment; filename*=UTF-8''" + QString::fromLatin1(QUrl::toPercentEncoding(info.fileName()));
            response.body = file.readAll();
            sendResponse(socket, response);
            return;
        }
    }

    // 本地文件不存在，尝试从远程客户端获取
    if (server_) {
        ClientHandler* handler = server_->findClientHandler(clientId);
        if (handler) {
            auto result = handler->requestFileSync(relativePath, 60000);
            if (result.success) {
                HttpResponse response;
                response.headers["Content-Type"] = "application/octet-stream";
                response.headers["Content-Disposition"] = "attachment; filename*=UTF-8''" + QString::fromLatin1(QUrl::toPercentEncoding(QFileInfo(relativePath).fileName()));
                response.body = result.data;
                sendResponse(socket, response);
                return;
            } else {
                HttpResponse response;
                response.statusCode = 500;
                response.statusText = "Internal Server Error";
                response.headers["Content-Type"] = "text/html; charset=utf-8";
                response.body = "<html><body><h1>Download Failed</h1><p>" + result.error.toHtmlEscaped().toUtf8() + "</p></body></html>";
                sendResponse(socket, response);
                return;
            }
        }
    }

    // 客户端未连接
    HttpResponse response;
    response.statusCode = 503;
    response.statusText = "Service Unavailable";
    response.headers["Content-Type"] = "text/html; charset=utf-8";
    response.body = "<html><body><h1>Client Not Connected</h1>"
                    "<p>The client that owns this file is not currently connected.</p></body></html>";
    sendResponse(socket, response);
}

void WebServer::handleBatchDownload(QTcpSocket* socket, const HttpRequest& request) {
    QString idsParam = request.params.value("ids");
    if (idsParam.isEmpty()) {
        HttpResponse response;
        response.statusCode = 400;
        response.statusText = "Bad Request";
        response.body = "Missing ids parameter";
        sendResponse(socket, response);
        return;
    }

    QStringList ids = idsParam.split(',', Qt::SkipEmptyParts);
    if (ids.isEmpty()) {
        HttpResponse response;
        response.statusCode = 400;
        response.statusText = "Bad Request";
        response.body = "No file IDs provided";
        sendResponse(socket, response);
        return;
    }

    // 收集所有文件路径
    QStringList filePaths;
    QStringList fileNames;
    for (const QString& id : ids) {
        QString clientId;
        QString relativePath;
        if (!parseFileId(id, clientId, relativePath)) {
            continue;
        }

        QString fullPath = indexer_->resolveFilePath(clientId, relativePath);
        QFileInfo info(fullPath);

        // 尝试本地文件
        if (info.exists() && info.isFile()) {
            filePaths.append(fullPath);
            fileNames.append(info.fileName());
        } else if (server_) {
            // 尝试从远程客户端获取
            ClientHandler* handler = server_->findClientHandler(clientId);
            if (handler) {
                auto result = handler->requestFileSync(relativePath, 60000);
                if (result.success && !result.data.isEmpty()) {
                    // 保存到临时文件
                    QString tempPath = QDir::temp().filePath("crossnet_batch_" + info.fileName());
                    QFile tempFile(tempPath);
                    if (tempFile.open(QIODevice::WriteOnly)) {
                        tempFile.write(result.data);
                        tempFile.close();
                        filePaths.append(tempPath);
                        fileNames.append(info.fileName());
                    }
                }
            }
        }
    }

    if (filePaths.isEmpty()) {
        HttpResponse response;
        response.statusCode = 404;
        response.statusText = "Not Found";
        response.body = "No files found";
        sendResponse(socket, response);
        return;
    }

    // 如果只有一个文件，直接下载
    if (filePaths.size() == 1) {
        QFile file(filePaths[0]);
        if (file.open(QIODevice::ReadOnly)) {
            HttpResponse response;
            response.headers["Content-Type"] = "application/octet-stream";
            response.headers["Content-Disposition"] = "attachment; filename*=UTF-8''" + QString::fromLatin1(QUrl::toPercentEncoding(fileNames[0]));
            response.body = file.readAll();
            sendResponse(socket, response);
            return;
        }
    }

    // 多个文件需要打包成 ZIP
    QString zipPath = QDir::temp().filePath("crossnet_batch_" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + ".zip");

    // 使用系统命令创建 ZIP（简单实现）
    QProcess zipProcess;
    QStringList zipArgs;

#ifdef Q_OS_WIN
    // Windows 使用 PowerShell 的 Compress-Archive
    zipArgs << "-NoProfile" << "-Command";
    QString psCmd = "Compress-Archive -Path ";
    for (int i = 0; i < filePaths.size(); ++i) {
        if (i > 0) psCmd += ",";
        psCmd += "'" + filePaths[i].replace("'", "''") + "'";
    }
    psCmd += " -DestinationPath '" + zipPath.replace("'", "''") + "' -Force";
    zipArgs << psCmd;
    zipProcess.start("powershell.exe", zipArgs);
#else
    // Linux/Mac 使用 zip 命令
    zipArgs << zipPath;
    zipArgs << filePaths;
    zipProcess.start("zip", zipArgs);
#endif

    if (!zipProcess.waitForFinished(30000) || zipProcess.exitCode() != 0) {
        HttpResponse response;
        response.statusCode = 500;
        response.statusText = "Internal Server Error";
        response.body = "Failed to create ZIP archive";
        sendResponse(socket, response);
        return;
    }

    // 发送 ZIP 文件
    QFile zipFile(zipPath);
    if (zipFile.open(QIODevice::ReadOnly)) {
        HttpResponse response;
        response.headers["Content-Type"] = "application/zip";
        response.headers["Content-Disposition"] = "attachment; filename*=UTF-8''" + QString::fromLatin1(QUrl::toPercentEncoding("files_" + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + ".zip"));
        response.body = zipFile.readAll();
        zipFile.close();
        sendResponse(socket, response);

        // 清理临时文件
        QFile::remove(zipPath);
        for (const QString& path : filePaths) {
            if (path.startsWith(QDir::temp().path())) {
                QFile::remove(path);
            }
        }
    } else {
        HttpResponse response;
        response.statusCode = 500;
        response.statusText = "Internal Server Error";
        response.body = "Failed to read ZIP archive";
        sendResponse(socket, response);
    }
}

void WebServer::handleFilePreview(QTcpSocket* socket, const HttpRequest& request) {
    QString clientId;
    QString relativePath;
    if (!parseFileId(request.params.value("id"), clientId, relativePath)) {
        HttpResponse response;
        response.statusCode = 400;
        response.statusText = "Bad Request";
        response.body = "Invalid file id";
        sendResponse(socket, response);
        return;
    }

    QString fullPath = indexer_->resolveFilePath(clientId, relativePath);
    QFileInfo info(fullPath);

    // 首先尝试本地文件预览
    if (info.exists()) {
        auto preview = DocumentConverter::previewFile(fullPath);
        HttpResponse response;
        if (!preview.success) {
            response.statusCode = 415;
            response.statusText = "Unsupported Media Type";
            response.headers["Content-Type"] = "text/html; charset=utf-8";
            response.body = "<div class=\"empty\">" + preview.error.toHtmlEscaped().toUtf8() + "</div>";
        } else {
            response.headers["Content-Type"] = preview.mimeType;
            response.body = preview.data;
        }
        sendResponse(socket, response);
        return;
    }

    // 本地文件不存在，尝试从远程客户端获取
    if (server_) {
        ClientHandler* handler = server_->findClientHandler(clientId);
        if (handler) {
            auto result = handler->requestFileSync(relativePath, 60000);
            if (result.success) {
                // 将文件数据写入临时文件用于预览
                QString tempPath = QDir::temp().filePath("crossnet_preview_" + QFileInfo(relativePath).fileName());
                QFile tempFile(tempPath);
                if (tempFile.open(QIODevice::WriteOnly)) {
                    tempFile.write(result.data);
                    tempFile.close();

                    auto preview = DocumentConverter::previewFile(tempPath);
                    HttpResponse response;
                    if (!preview.success) {
                        response.statusCode = 415;
                        response.statusText = "Unsupported Media Type";
                        response.headers["Content-Type"] = "text/html; charset=utf-8";
                        response.body = "<div class=\"empty\">" + preview.error.toHtmlEscaped().toUtf8() + "</div>";
                    } else {
                        response.headers["Content-Type"] = preview.mimeType;
                        response.body = preview.data;
                    }

                    // 清理临时文件
                    QFile::remove(tempPath);
                    sendResponse(socket, response);
                    return;
                }
            } else {
                HttpResponse response;
                response.statusCode = 500;
                response.statusText = "Internal Server Error";
                response.headers["Content-Type"] = "text/html; charset=utf-8";
                response.body = "<div class=\"empty\">Preview failed: " + result.error.toHtmlEscaped().toUtf8() + "</div>";
                sendResponse(socket, response);
                return;
            }
        }
    }

    // 客户端未连接
    HttpResponse response;
    response.statusCode = 503;
    response.statusText = "Service Unavailable";
    response.headers["Content-Type"] = "text/html; charset=utf-8";
    response.body = "<div class=\"empty\">Client not connected. Cannot preview remote file.</div>";
    sendResponse(socket, response);
}

void WebServer::serveStaticFile(QTcpSocket* socket, const QString& path) {
    Q_UNUSED(path)
    HttpResponse response;
    response.statusCode = 404;
    response.statusText = "Not Found";
    response.body = "Not Found";
    sendResponse(socket, response);
}

void WebServer::serveLoginPage(QTcpSocket* socket) {
    static const char* html = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CrossNetShare 登录</title>
<style>body{font-family:Segoe UI,Microsoft YaHei,sans-serif;background:#f3f6fb;margin:0;display:grid;place-items:center;height:100vh}.card{width:360px;background:white;border-radius:14px;box-shadow:0 20px 50px #1d35571f;padding:32px}h1{margin:0 0 8px;color:#1d3557}.muted{color:#667085;margin-bottom:24px}.field{margin:14px 0}label{display:block;font-size:13px;color:#344054;margin-bottom:6px}input{width:100%;box-sizing:border-box;border:1px solid #d0d5dd;border-radius:8px;padding:11px;font-size:15px}button{width:100%;margin-top:18px;background:#2563eb;color:white;border:0;border-radius:8px;padding:12px;font-size:15px;cursor:pointer}.error{color:#b42318;margin-top:14px;min-height:20px}</style></head><body><div class="card"><h1>CrossNetShare</h1><div class="muted">请输入用户名和密码访问共享文件</div><div class="field"><label>用户名</label><input id="u" value="admin"></div><div class="field"><label>密码</label><input id="p" type="password" placeholder="默认：admin123"></div><button onclick="login()">登录</button><div id="e" class="error"></div></div><script>
async function login(){const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u.value,password:p.value})});const j=await r.json();if(j.success) location.href=j.redirect; else e.textContent=j.error||'登录失败'}
document.addEventListener('keydown',ev=>{if(ev.key==='Enter')login()});
</script></body></html>)HTML";
    HttpResponse response;
    response.headers["Content-Type"] = "text/html; charset=utf-8";
    response.body = html;
    sendResponse(socket, response);
}

void WebServer::serveBrowsePage(QTcpSocket* socket) {
    static const char* html = R"HTML(
<!doctype html><html lang="zh-CN"><head> <meta charset="utf-8"> <meta name="viewport" content="width=device-width,initial-scale=1"> <title>CrossNetShare Web</title> <style> body{font-family:Segoe UI,Microsoft YaHei,sans-serif;margin:0;color:#101828;background:#f6f8fb} .top{height:64px;background:#1d3557;color:white;display:flex;align-items:center;gap:16px;padding:0 22px} .top h1{font-size:20px;margin:0} .search{max-width:400px;display:flex;gap:8px;align-items:center} .search input{width:100%;border:0;border-radius:8px;padding:10px} .date-filter{display:flex;gap:8px;align-items:center;color:white;font-size:14px} .date-filter input[type="date"]{border:0;border-radius:6px;padding:6px;color:#111827} .top-actions{margin-left:auto;display:flex;gap:8px;align-items:center} .btn{border:0;border-radius:8px;padding:10px 14px;cursor:pointer;background:#2563eb;color:white;text-decoration:none;font-size:14px;white-space:nowrap} .btn.secondary{background:#e5e7eb;color:#111827} .btn:disabled{opacity:0.5;cursor:not-allowed} .layout{display:grid;grid-template-columns:420px 1fr;height:calc(100vh - 64px)} .list{border-right:1px solid #e4e7ec;background:white;overflow:auto} .list-header{padding:10px 14px;border-bottom:2px solid #e4e7ec;background:#f9fafb;display:flex;gap:8px;align-items:center} .item{padding:12px 14px;border-bottom:1px solid #eef2f7;cursor:pointer;display:flex;gap:10px;align-items:start} .item:hover{background:#eff6ff} .item.active{background:#dbeafe} .item input[type="checkbox"]{margin-top:4px;width:16px;height:16px;cursor:pointer} .item-content{flex:1;min-width:0} .name{font-weight:600} .meta{font-size:12px;color:#667085;margin-top:4px} .preview{display:flex;flex-direction:column;min-width:0} .toolbar{background:white;border-bottom:1px solid #e4e7ec;padding:10px;display:flex;gap:8px;align-items:center} .frame{flex:1;border:0;background:white;margin:16px;border-radius:10px;box-shadow:0 1px 3px #0000001a} .empty{padding:40px;color:#667085;text-align:center} .text-preview{white-space:pre-wrap;padding:20px} .count{color:#667085;font-size:13px} </style></head><body> <div class="top"> <h1>CrossNetShare Web</h1> <div class="search"> <input id="q" placeholder="输入关键词实时过滤文件"> </div> <div class="date-filter"> <label>日期筛选:</label> <input type="date" id="dateFrom"> <button class="btn" onclick="selectByDate()">选择</button> <button class="btn secondary" onclick="clearDateFilter()">清除</button> </div> <div class="top-actions"> <button class="btn" onclick="downloadSelected()" id="batchDownloadBtn" disabled>批量下载</button> <a class="btn secondary" href="/api/logout">退出</a> </div> </div> <div class="layout"> <div class="list"> <div class="list-header"> <input type="checkbox" id="selectAll" onchange="toggleSelectAll()"> <label for="selectAll" style="flex:1;cursor:pointer">全选 / 全不选</label> <span id="count" class="count"></span> </div> <div id="list"> <div class="empty">正在加载...</div> </div> </div> <div class="preview"> <div class="toolbar"> <strong id="title">请选择文件</strong> <button id="download" class="btn" style="display:none" onclick="downloadFile()">下载</button> <button id="printBtn" class="btn secondary" style="display:none" onclick="printPreview()">打印</button> </div> <iframe id="frame" class="frame" srcdoc="<div class='empty'>选择左侧文件后在这里预览</div>"></iframe> </div> </div> <script> let files = [], selected = null, selectedIds = new Set(); async function loadFiles() { const r = await fetch('/api/files'); if (r.status === 401) { location.href = '/login'; return; } const j = await r.json(); files = j.files || []; files.sort((a, b) => new Date(b.createTime).getTime() - new Date(a.createTime).getTime()); render(); } function render() { const query = document.getElementById('q').value.trim().toLowerCase(); const filtered = query ? files.filter(f => f.filename.toLowerCase().includes(query) || f.relativePath.toLowerCase().includes(query) ) : files; document.getElementById('count').textContent = filtered.length + ' / ' + files.length + ' 个文件'; const listEl = document.getElementById('list'); listEl.innerHTML = ''; if (!filtered.length) { listEl.innerHTML = '<div class="empty">没有匹配的文件</div>'; return; } for (const f of filtered) { const div = document.createElement('div'); div.className = 'item'; const cb = document.createElement('input'); cb.type = 'checkbox'; cb.checked = selectedIds.has(f.id); cb.onchange = (e) => { e.stopPropagation(); toggleSelect(f.id); }; div.appendChild(cb); const content = document.createElement('div'); content.className = 'item-content'; content.onclick = () => selectFile(f, div); content.innerHTML = '<div class="name">' + escapeHtml(f.filename) + '</div>' + '<div class="meta">' + escapeHtml(f.ownerClient) + ' · ' + escapeHtml(f.relativePath) + ' · ' + escapeHtml(f.humanSize || '') + '</div>' + '<div class="meta">创建时间：' + escapeHtml(f.createTime || '') + '</div>'; div.appendChild(content); listEl.appendChild(div); } updateBatchButton(); } function toggleSelect(id) { if (selectedIds.has(id)) selectedIds.delete(id); else selectedIds.add(id); updateBatchButton(); document.getElementById('selectAll').checked = selectedIds.size === files.length; } function toggleSelectAll() { const checked = document.getElementById('selectAll').checked; if (checked) { files.forEach(f => selectedIds.add(f.id)); } else { selectedIds.clear(); } render(); } function updateBatchButton() { const btn = document.getElementById('batchDownloadBtn'); btn.disabled = selectedIds.size === 0; btn.textContent = '批量下载' + (selectedIds.size > 0 ? ' (' + selectedIds.size + ')' : ''); } function selectByDate() { const dateFrom = document.getElementById('dateFrom').value; if (!dateFrom) { alert('请选择起始日期'); return; } const fromTime = new Date(dateFrom).getTime(); selectedIds.clear(); files.forEach(f => { const createTime = new Date(f.createTime).getTime(); if (createTime >= fromTime) selectedIds.add(f.id); }); render(); } function clearDateFilter() { document.getElementById('dateFrom').value = ''; selectedIds.clear(); render(); } function selectFile(f, el) { selected = f; document.querySelectorAll('.item').forEach(x => x.classList.remove('active')); el.classList.add('active'); document.getElementById('title').textContent = f.filename; document.getElementById('download').style.display = 'inline-block'; document.getElementById('printBtn').style.display = 'inline-block'; document.getElementById('frame').src = '/api/preview?id=' + encodeURIComponent(f.id); } function downloadFile() { if (selected) location.href = '/api/download?id=' + encodeURIComponent(selected.id); } async function downloadSelected() { if (selectedIds.size === 0) return; const ids = Array.from(selectedIds); if (ids.length === 1) { location.href = '/api/download?id=' + encodeURIComponent(ids[0]); return; } location.href = '/api/batch-download?ids=' + encodeURIComponent(ids.join(',')); } function printPreview() { const frameEl = document.getElementById('frame'); frameEl.contentWindow.focus(); frameEl.contentWindow.print(); } function escapeHtml(s) { return String(s || '').replace(/[&<>"']/g, m => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[m])); } document.getElementById('q').addEventListener('input', render); loadFiles(); </script></body></html>)HTML";
    HttpResponse response;
    response.headers["Content-Type"] = "text/html; charset=utf-8";
    response.body = html;
    sendResponse(socket, response);
}

QString WebServer::getMimeType(const QString& filename) {
    QString suffix = QFileInfo(filename).suffix().toLower();
    if (suffix == "html") return "text/html; charset=utf-8";
    if (suffix == "css") return "text/css; charset=utf-8";
    if (suffix == "js") return "application/javascript; charset=utf-8";
    if (suffix == "json") return "application/json; charset=utf-8";
    if (suffix == "png") return "image/png";
    if (suffix == "jpg" || suffix == "jpeg") return "image/jpeg";
    if (suffix == "gif") return "image/gif";
    if (suffix == "pdf") return "application/pdf";
    return "application/octet-stream";
}

bool WebServer::isAuthenticated(const HttpRequest& request, QString& username) {
    if (!authManager_) {
        return false;
    }
    return authManager_->validateSession(cookieValue(request, "CNS_SESSION"), username);
}

QByteArray WebServer::buildHttpResponse(const HttpResponse& response) {
    QByteArray data;
    data += "HTTP/1.1 " + QByteArray::number(response.statusCode) + " " + response.statusText.toUtf8() + "\r\n";
    QMap<QString, QString> headers = response.headers;
    headers["Content-Length"] = QString::number(response.body.size());
    headers["Connection"] = "close";
    headers["X-Content-Type-Options"] = "nosniff";
    if (!headers.contains("Content-Type")) {
        headers["Content-Type"] = "text/plain; charset=utf-8";
    }
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        data += it.key().toUtf8() + ": " + it.value().toUtf8() + "\r\n";
    }
    data += "\r\n";
    data += response.body;
    return data;
}

}
