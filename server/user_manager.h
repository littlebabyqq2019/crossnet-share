#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMutex>

namespace CrossNetShare {

// 用户权限级别
enum class UserPermission {
    ReadOnly = 0,   // 只读：仅查看文件列表和预览
    Print = 1,      // 打印：预览 + 打印
    Download = 2    // 下载：完整功能（预览、打印、下载）
};

// 权限枚举转字符串
inline QString permissionToString(UserPermission permission) {
    switch (permission) {
        case UserPermission::ReadOnly: return "只读";
        case UserPermission::Print: return "打印";
        case UserPermission::Download: return "下载";
        default: return "未知";
    }
}

// 字符串转权限枚举
inline UserPermission stringToPermission(const QString& str) {
    if (str == "只读" || str == "ReadOnly") return UserPermission::ReadOnly;
    if (str == "打印" || str == "Print") return UserPermission::Print;
    if (str == "下载" || str == "Download") return UserPermission::Download;
    return UserPermission::ReadOnly;  // 默认最低权限
}

struct User {
    QString username;
    QString passwordHash;  // SHA256 hash
    QDateTime createdAt;
    bool isAdmin;
    bool isActive;
    UserPermission permission;  // 用户权限级别

    User() : isAdmin(false), isActive(true), permission(UserPermission::Download) {}
};

class UserManager {
public:
    static UserManager* instance();

    bool addUser(const QString& username, const QString& password, bool isAdmin = false, UserPermission permission = UserPermission::Download);
    bool removeUser(const QString& username);
    bool updatePassword(const QString& username, const QString& newPassword);
    bool setAdmin(const QString& username, bool isAdmin);
    bool setActive(const QString& username, bool isActive);
    bool setPermission(const QString& username, UserPermission permission);

    bool authenticate(const QString& username, const QString& password);
    User getUser(const QString& username) const;
    UserPermission getUserPermission(const QString& username) const;
    QVector<User> getAllUsers() const;

    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath);

private:
    UserManager();
    static UserManager* instance_;

    QVector<User> users_;
    mutable QMutex mutex_;

    QString hashPassword(const QString& password) const;
    int findUserIndex(const QString& username) const;
};

}
