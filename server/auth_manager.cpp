#include "auth_manager.h"
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
    addUser("admin", "admin123", "admin");
}

AuthManager::~AuthManager() {
}

bool AuthManager::loadUsers(const QString& configFile) {
    QFile file(configFile);
    if (!file.exists()) {
        emit logMessage("User config not found, creating default admin/admin123");
        return saveUsers(configFile);
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage("Failed to open user config: " + configFile);
        return false;
    }

    try {
        QByteArray data = file.readAll();
        auto root = nlohmann::json::parse(data.constData(), data.constData() + data.size());

        users_.clear();
        sessionTimeout_ = root.value("sessionTimeout", 1800);

        for (const auto& userJson : root["users"]) {
            User user;
            user.username = QString::fromStdString(userJson["username"].get<std::string>());
            user.passwordHash = QString::fromStdString(userJson["passwordHash"].get<std::string>());
            user.role = QString::fromStdString(userJson.value("role", "user"));
            users_[user.username] = user;
        }

        if (users_.isEmpty()) {
            addUser("admin", "admin123", "admin");
        }

        emit logMessage("Loaded " + QString::number(users_.size()) + " web users");
        return true;
    } catch (const std::exception& e) {
        emit logMessage("Failed to parse user config: " + QString::fromUtf8(e.what()));
        users_.clear();
        addUser("admin", "admin123", "admin");
        return false;
    }
}

bool AuthManager::saveUsers(const QString& configFile) {
    nlohmann::json root;
    root["sessionTimeout"] = sessionTimeout_;
    root["users"] = nlohmann::json::array();

    for (const User& user : users_) {
        nlohmann::json item;
        item["username"] = user.username.toStdString();
        item["passwordHash"] = user.passwordHash.toStdString();
        item["role"] = user.role.toStdString();
        root["users"].push_back(item);
    }

    QFile file(configFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit logMessage("Failed to save user config: " + configFile);
        return false;
    }

    QByteArray data = QByteArray::fromStdString(root.dump(2));
    return file.write(data) == data.size();
}

QString AuthManager::authenticate(const QString& username, const QString& password) {
    cleanupExpiredSessions();

    if (!users_.contains(username)) {
        emit logMessage("Web login failed for unknown user: " + username);
        return QString();
    }

    const User& user = users_[username];
    if (hashPassword(password) != user.passwordHash) {
        emit logMessage("Web login failed for user: " + username);
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
    emit logMessage("Web user logged in: " + username);
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
