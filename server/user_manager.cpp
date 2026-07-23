#include "user_manager.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>
#include <QDebug>

namespace CrossNetShare {

UserManager* UserManager::instance_ = nullptr;

UserManager::UserManager() {
    // 添加默认管理员账户
    User defaultAdmin;
    defaultAdmin.username = "admin";
    defaultAdmin.passwordHash = hashPassword("admin123");
    defaultAdmin.isAdmin = true;
    defaultAdmin.isActive = true;
    defaultAdmin.permissions = PermissionPresets::Full;  // 管理员默认完整权限
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

bool UserManager::addUser(const QString& username, const QString& password, bool isAdmin, UserPermissions permissions) {
    QMutexLocker locker(&mutex_);

    qDebug() << "[UserManager] addUser called - username:" << username << "isAdmin:" << isAdmin;

    if (username.isEmpty() || password.isEmpty()) {
        qDebug() << "[UserManager] addUser failed - empty username or password";
        return false;
    }

    if (findUserIndex(username) >= 0) {
        qDebug() << "[UserManager] addUser failed - user already exists";
        return false;  // 用户已存在
    }

    User user;
    user.username = username;
    user.passwordHash = hashPassword(password);
    user.isAdmin = isAdmin;
    user.isActive = true;
    user.permissions = permissions;
    user.createdAt = QDateTime::currentDateTime();

    users_.append(user);
    qDebug() << "[UserManager] addUser success - total users now:" << users_.size();
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

bool UserManager::setPermissions(const QString& username, UserPermissions permissions) {
    QMutexLocker locker(&mutex_);

    int index = findUserIndex(username);
    if (index < 0) {
        return false;
    }

    users_[index].permissions = permissions;
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

UserPermissions UserManager::getUserPermissions(const QString& username) const {
    QMutexLocker locker(&mutex_);

    int index = findUserIndex(username);
    if (index >= 0) {
        return users_[index].permissions;
    }

    return UserPermissionFlag::NoPermission;  // 默认无权限
}

bool UserManager::hasPermission(const QString& username, UserPermissionFlag permission) const {
    UserPermissions permissions = getUserPermissions(username);
    return permissions.testFlag(permission);
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

        // 加载权限位标志
        if (userObj.contains("permissions")) {
            // 新格式：直接读取权限位
            user.permissions = UserPermissions(userObj["permissions"].toInt());
        } else if (userObj.contains("permission")) {
            // 旧格式：从枚举转换为位标志（向后兼容）
            QString permStr = userObj["permission"].toString("Download");
            if (permStr == "只读" || permStr == "ReadOnly") {
                user.permissions = PermissionPresets::ReadOnly;
            } else if (permStr == "打印" || permStr == "Print") {
                user.permissions = PermissionPresets::Print;
            } else {
                user.permissions = PermissionPresets::Download;
            }
        } else {
            // 默认下载权限
            user.permissions = PermissionPresets::Download;
        }

        users_.append(user);
    }

    // 确保至少有默认管理员
    if (findUserIndex("admin") < 0) {
        User defaultAdmin;
        defaultAdmin.username = "admin";
        defaultAdmin.passwordHash = hashPassword("admin123");
        defaultAdmin.isAdmin = true;
        defaultAdmin.isActive = true;
        defaultAdmin.permissions = PermissionPresets::Full;
        defaultAdmin.createdAt = QDateTime::currentDateTime();
        users_.append(defaultAdmin);
    }

    return true;
}

bool UserManager::saveToFile(const QString& filePath) {
    QMutexLocker locker(&mutex_);

    qDebug() << "[UserManager] ========== Saving users to file ==========";
    qDebug() << "[UserManager] Total users:" << users_.size();
    qDebug() << "[UserManager] File path:" << filePath;

    QJsonArray usersArray;
    for (const User& user : users_) {
        QJsonObject userObj;
        userObj["username"] = user.username;
        userObj["passwordHash"] = user.passwordHash;
        userObj["isAdmin"] = user.isAdmin;
        userObj["isActive"] = user.isActive;
        userObj["createdAt"] = user.createdAt.toString(Qt::ISODate);
        userObj["permissions"] = static_cast<int>(user.permissions);  // 保存权限位标志

        usersArray.append(userObj);
        qDebug() << "[UserManager]   User:" << user.username
                 << "| Admin:" << user.isAdmin
                 << "| Active:" << user.isActive
                 << "| Permissions:" << permissionsToString(user.permissions);
    }

    QJsonObject root;
    root["users"] = usersArray;

    QJsonDocument doc(root);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "[UserManager] ERROR: Failed to open file for writing:" << file.errorString();
        return false;
    }

    qint64 written = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    if (written == -1) {
        qDebug() << "[UserManager] ERROR: Failed to write to file";
        return false;
    }

    qDebug() << "[UserManager] SUCCESS: Wrote" << written << "bytes to file";
    qDebug() << "[UserManager] ========================================";
    return true;
}

}
