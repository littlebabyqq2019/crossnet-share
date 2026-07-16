#include "web_server.h"
#include "auth_manager.h"
#include "document_converter.h"
#include "file_indexer.h"
#include "common/protocol.h"
#include <nlohmann/json.hpp>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
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
    QFile file(fullPath);
    QFileInfo info(fullPath);
    if (!info.exists() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
        HttpResponse response;
        response.statusCode = 404;
        response.statusText = "Not Found";
        response.body = "File not found";
        sendResponse(socket, response);
        return;
    }

    HttpResponse response;
    response.headers["Content-Type"] = "application/octet-stream";
    response.headers["Content-Disposition"] = "attachment; filename*=UTF-8''" + QString::fromLatin1(QUrl::toPercentEncoding(info.fileName()));
    response.body = file.readAll();
    sendResponse(socket, response);
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
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CrossNetShare Web</title>
<style>body{font-family:Segoe UI,Microsoft YaHei,sans-serif;margin:0;color:#101828;background:#f6f8fb}.top{height:64px;background:#1d3557;color:white;display:flex;align-items:center;gap:16px;padding:0 22px}.top h1{font-size:20px;margin:0}.search{flex:1;display:flex;gap:8px}.search input{flex:1;border:0;border-radius:8px;padding:10px}.btn{border:0;border-radius:8px;padding:10px 14px;cursor:pointer;background:#2563eb;color:white;text-decoration:none}.btn.secondary{background:#e5e7eb;color:#111827}.layout{display:grid;grid-template-columns:420px 1fr;height:calc(100vh - 64px)}.list{border-right:1px solid #e4e7ec;background:white;overflow:auto}.item{padding:12px 14px;border-bottom:1px solid #eef2f7;cursor:pointer}.item:hover,.item.active{background:#eff6ff}.name{font-weight:600}.meta{font-size:12px;color:#667085;margin-top:4px}.preview{display:flex;flex-direction:column;min-width:0}.toolbar{background:white;border-bottom:1px solid #e4e7ec;padding:10px;display:flex;gap:8px;align-items:center}.frame{flex:1;border:0;background:white;margin:16px;border-radius:10px;box-shadow:0 1px 3px #0000001a}.empty{padding:40px;color:#667085}.text-preview{white-space:pre-wrap;padding:20px}.count{color:#d0d5dd;font-size:13px}</style></head><body><div class="top"><h1>CrossNetShare Web</h1><div class="search"><input id="q" placeholder="输入文件名关键词搜索"><button class="btn" onclick="loadFiles()">搜索</button><button class="btn secondary" onclick="resetSearch()">全部</button></div><span id="count" class="count"></span><a class="btn secondary" href="/api/logout">退出</a></div><div class="layout"><div id="list" class="list"><div class="empty">正在加载...</div></div><div class="preview"><div class="toolbar"><strong id="title">请选择文件</strong><button id="download" class="btn" style="display:none" onclick="downloadFile()">下载</button><button id="print" class="btn secondary" style="display:none" onclick="printPreview()">打印</button></div><iframe id="frame" class="frame" srcdoc="<div class='empty'>选择左侧文件后在这里预览</div>"></iframe></div></div><script>
let files=[],selected=null;async function loadFiles(){const qs=q.value.trim();const url=qs?'/api/search?q='+encodeURIComponent(qs):'/api/files';const r=await fetch(url);if(r.status===401){location.href='/login';return}const j=await r.json();files=j.files||[];render()}function resetSearch(){q.value='';loadFiles()}function render(){count.textContent=files.length+' 个文件';list.innerHTML='';if(!files.length){list.innerHTML='<div class="empty">没有匹配的文件</div>';return}for(const f of files){const div=document.createElement('div');div.className='item';div.onclick=()=>selectFile(f,div);div.innerHTML='<div class="name">'+escapeHtml(f.filename)+'</div><div class="meta">'+escapeHtml(f.ownerClient)+' · '+escapeHtml(f.relativePath)+' · '+escapeHtml(f.humanSize||'')+'</div><div class="meta">创建时间：'+escapeHtml(f.createTime||'')+'</div>';list.appendChild(div)}}function selectFile(f,el){selected=f;document.querySelectorAll('.item').forEach(x=>x.classList.remove('active'));el.classList.add('active');title.textContent=f.filename;download.style.display='inline-block';print.style.display='inline-block';frame.src='/api/preview?id='+encodeURIComponent(f.id)}function downloadFile(){if(selected)location.href='/api/download?id='+encodeURIComponent(selected.id)}function printPreview(){frame.contentWindow.focus();frame.contentWindow.print()}function escapeHtml(s){return String(s||'').replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]))}q.addEventListener('keydown',e=>{if(e.key==='Enter')loadFiles()});loadFiles();
</script></body></html>)HTML";
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
