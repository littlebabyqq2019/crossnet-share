#include "user_manager.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>

namespace CrossNetShare {

UserManager* UserManager::instance_ = nullptr;

UserManager::UserManager() {
    // 添加默认管理员账户
    User defaultAdmin;
    defaultAdmin.username = "admin";
    defaultAdmin.passwordHash = hashPassword("admin123");
    defaultAdmin.isAdmin = true;
    defaultAdmin.isActive = true;
    defaultAdmin.permission = UserPermission::Download;  // 管理员默认完整权限
    defaultAdmin.createdAt = QDateTime::currentDateTime();
    users_.append(defaultAdmin);
}

UserManager* UserManager::instance() {
    if (!instance_) {
        instance_ = new UserManager();
    }
    return instance_;
}

QString UserManager::hashPassword(const QString& password) const {
    QByteArray hash = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

int UserManager::findUserIndex(const QString& username) const {
    for (int i = 0; i < users_.size(); ++i) {
        if (users_[i].username == username) {
            return i;
        }
    }
    return -1;
}

bool UserManager::addUser(const QString& username, const QString& password, bool isAdmin, UserPermission permission) {
    QMutexLocker locker(&mutex_);

    if (username.isEmpty() || password.isEmpty()) {
        return false;
    }

    if (findUserIndex(username) >= 0) {
        return false;  // 用户已存在
    }

    User user;
    user.username = username;
    user.passwordHash = hashPassword(password);
    user.isAdmin = isAdmin;
    user.isActive = true;
    user.permission = permission;
    user.createdAt = QDateTime::currentDateTime();

    users_.append(user);
    return true;
}

bool UserManager::removeUser(const QString& username) {
    QMutexLocker locker(&mutex_);

    // 不能删除默认管理员
    if (username == "admin") {
        return false;
    }

    int index = findUserIndex(username);
    if (index < 0) {
        return false;
    }

    users_.remove(index);
    return true;
}

bool UserManager::updatePassword(const QString& username, const QString& newPassword) {
    QMutexLocker locker(&mutex_);

    if (newPassword.isEmpty()) {
        return false;
    }

    int index = findUserIndex(username);
    if (index < 0) {
        return false;
    }

    users_[index].passwordHash = hashPassword(newPassword);
    return true;
}

bool UserManager::setAdmin(const QString& username, bool isAdmin) {
    QMutexLocker locker(&mutex_);

    // 不能修改默认管理员权限
    if (username == "admin") {
        return false;
    }

    int index = findUserIndex(username);
    if (index < 0) {
        return false;
    }

    users_[index].isAdmin = isAdmin;
    return true;
}

bool UserManager::setActive(const QString& username, bool isActive) {
    QMutexLocker locker(&mutex_);

    // 不能禁用默认管理员
    if (username == "admin") {
        return false;
    }

    int index = findUserIndex(username);
    if (index < 0) {
        return false;
    }

    users_[index].isActive = isActive;
    return true;
}

bool UserManager::setPermission(const QString& username, UserPermission permission) {
    QMutexLocker locker(&mutex_);

    int index = findUserIndex(username);
    if (index < 0) {
        return false;
    }

    users_[index].permission = permission;
    return true;
}

bool UserManager::authenticate(const QString& username, const QString& password) {
    QMutexLocker locker(&mutex_);

    int index = findUserIndex(username);
    if (index < 0) {
        return false;
    }

    const User& user = users_[index];
    if (!user.isActive) {
        return false;
    }

    return user.passwordHash == hashPassword(password);
}

User UserManager::getUser(const QString& username) const {
    QMutexLocker locker(&mutex_);

    int index = findUserIndex(username);
    if (index >= 0) {
        return users_[index];
    }

    return User();
}

UserPermission UserManager::getUserPermission(const QString& username) const {
    QMutexLocker locker(&mutex_);

    int index = findUserIndex(username);
    if (index >= 0) {
        return users_[index].permission;
    }

    return UserPermission::ReadOnly;  // 默认最低权限
}

QVector<User> UserManager::getAllUsers() const {
    QMutexLocker locker(&mutex_);
    return users_;
}

bool UserManager::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray usersArray = root["users"].toArray();

    QMutexLocker locker(&mutex_);
    users_.clear();

    for (const QJsonValue& value : usersArray) {
        QJsonObject userObj = value.toObject();

        User user;
        user.username = userObj["username"].toString();
        user.passwordHash = userObj["passwordHash"].toString();
        user.isAdmin = userObj["isAdmin"].toBool();
        user.isActive = userObj["isActive"].toBool(true);
        user.createdAt = QDateTime::fromString(userObj["createdAt"].toString(), Qt::ISODate);

        // 加载权限，默认为下载权限以兼容旧数据
        QString permStr = userObj["permission"].toString("Download");
        user.permission = stringToPermission(permStr);

        users_.append(user);
    }

    // 确保至少有默认管理员
    if (findUserIndex("admin") < 0) {
        User defaultAdmin;
        defaultAdmin.username = "admin";
        defaultAdmin.passwordHash = hashPassword("admin123");
        defaultAdmin.isAdmin = true;
        defaultAdmin.isActive = true;
        defaultAdmin.permission = UserPermission::Download;
        defaultAdmin.createdAt = QDateTime::currentDateTime();
        users_.append(defaultAdmin);
    }

    return true;
}

bool UserManager::saveToFile(const QString& filePath) {
    QMutexLocker locker(&mutex_);

    QJsonArray usersArray;
    for (const User& user : users_) {
        QJsonObject userObj;
        userObj["username"] = user.username;
        userObj["passwordHash"] = user.passwordHash;
        userObj["isAdmin"] = user.isAdmin;
        userObj["isActive"] = user.isActive;
        userObj["createdAt"] = user.createdAt.toString(Qt::ISODate);
        userObj["permission"] = permissionToString(user.permission);

        usersArray.append(userObj);
    }

    QJsonObject root;
    root["users"] = usersArray;

    QJsonDocument doc(root);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(doc.toJson());
    file.close();

    return true;
}

}
