#include "auth_manager.h"
#include "user_manager.h"
#include <nlohmann/json.hpp>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QUuid>

namespace CrossNetShare {

AuthManager::AuthManager(QObject* parent)
    : QObject(parent)
    , sessionTimeout_(1800)
{
    // 使用 UserManager 替代本地用户存储
}

AuthManager::~AuthManager() {
}

bool AuthManager::loadUsers(const QString& configFile) {
    // 从文件加载用户到 UserManager
    if (UserManager::instance()->loadFromFile(configFile)) {
        emit logMessage("已从配置文件加载用户");
        return true;
    } else {
        emit logMessage("用户配置文件未找到，使用默认管理员账户 admin/admin123");
        return saveUsers(configFile);
    }
}

bool AuthManager::saveUsers(const QString& configFile) {
    // 保存用户到文件
    return UserManager::instance()->saveToFile(configFile);
}

QString AuthManager::authenticate(const QString& username, const QString& password) {
    cleanupExpiredSessions();

    // 使用 UserManager 进行认证
    if (!UserManager::instance()->authenticate(username, password)) {
        emit logMessage("Web 登录失败，用户: " + username);
        return QString();
    }

    QString token = generateToken();
    Session session;
    session.token = token;
    session.username = username;
    session.loginTime = QDateTime::currentDateTimeUtc();
    session.lastAccess = session.loginTime;
    sessions_[token] = session;

    emit userLoggedIn(username);
    emit logMessage("Web 用户已登录: " + username);
    return token;
}

bool AuthManager::validateSession(const QString& token, QString& username) {
    cleanupExpiredSessions();

    if (token.isEmpty() || !sessions_.contains(token)) {
        return false;
    }

    Session& session = sessions_[token];
    session.lastAccess = QDateTime::currentDateTimeUtc();
    username = session.username;
    return true;
}

void AuthManager::logout(const QString& token) {
    if (!sessions_.contains(token)) {
        return;
    }

    QString username = sessions_[token].username;
    sessions_.remove(token);
    emit userLoggedOut(username);
    emit logMessage("Web user logged out: " + username);
}

bool AuthManager::addUser(const QString& username, const QString& password, const QString& role) {
    QString normalized = username.trimmed();
    if (normalized.isEmpty() || password.isEmpty() || users_.contains(normalized)) {
        return false;
    }

    User user;
    user.username = normalized;
    user.passwordHash = hashPassword(password);
    user.role = role.isEmpty() ? "user" : role;
    users_[normalized] = user;
    return true;
}

bool AuthManager::removeUser(const QString& username) {
    if (!users_.contains(username)) {
        return false;
    }

    users_.remove(username);

    QStringList tokens;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it.value().username == username) {
            tokens.append(it.key());
        }
    }
    for (const QString& token : tokens) {
        sessions_.remove(token);
    }

    return true;
}

bool AuthManager::changePassword(const QString& username, const QString& oldPassword, const QString& newPassword) {
    if (!users_.contains(username) || newPassword.isEmpty()) {
        return false;
    }

    User& user = users_[username];
    if (hashPassword(oldPassword) != user.passwordHash) {
        return false;
    }

    user.passwordHash = hashPassword(newPassword);
    return true;
}

void AuthManager::cleanupExpiredSessions() {
    QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList expiredTokens;

    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it.value().lastAccess.secsTo(now) > sessionTimeout_) {
            expiredTokens.append(it.key());
        }
    }

    for (const QString& token : expiredTokens) {
        QString username = sessions_[token].username;
        sessions_.remove(token);
        emit userLoggedOut(username);
    }
}

QString AuthManager::hashPassword(const QString& password, const QString& salt) {
    QString value = salt.isEmpty() ? password : password + ":" + salt;
    QByteArray hash = QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

QString AuthManager::generateToken() {
    QByteArray seed = QUuid::createUuid().toByteArray() + QByteArray::number(QRandomGenerator::global()->generate64());
    QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

QString AuthManager::generateSalt() {
    QByteArray seed = QByteArray::number(QRandomGenerator::global()->generate64());
    return QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().left(16));
}

}
