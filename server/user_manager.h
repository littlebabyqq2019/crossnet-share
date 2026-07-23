#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMutex>
#include <QFlags>

namespace CrossNetShare {

// 用户权限位标志（细粒度权限控制）
enum UserPermissionFlag {
    NoPermission      = 0x0000,  // 无权限
    ViewFileList      = 0x0001,  // 浏览文件列表
    PreviewFile       = 0x0002,  // 预览文件
    PrintFile         = 0x0004,  // 打印文件
    DownloadFile      = 0x0008,  // 下载单个文件
    BatchDownload     = 0x0010,  // 批量下载
    WatermarkExport   = 0x0020,  // 水印导出
    DateFilter        = 0x0040,  // 日期筛选
    AllPermissions    = 0xFFFF   // 所有权限
};
Q_DECLARE_FLAGS(UserPermissions, UserPermissionFlag)

}  // namespace CrossNetShare

// Q_DECLARE_OPERATORS_FOR_FLAGS 必须在namespace外部声明
Q_DECLARE_OPERATORS_FOR_FLAGS(CrossNetShare::UserPermissions)

namespace CrossNetShare {

// 预设权限组合（为了向后兼容和快速设置）
namespace PermissionPresets {
    const UserPermissions ReadOnly = ViewFileList | PreviewFile;
    const UserPermissions Print = ReadOnly | PrintFile;
    const UserPermissions Download = Print | DownloadFile | BatchDownload | DateFilter;
    const UserPermissions Full = AllPermissions;
}

// 权限转字符串（用于显示）
inline QString permissionFlagToString(UserPermissionFlag flag) {
    switch (flag) {
        case ViewFileList: return "浏览文件列表";
        case PreviewFile: return "预览文件";
        case PrintFile: return "打印";
        case DownloadFile: return "下载";
        case BatchDownload: return "批量下载";
        case WatermarkExport: return "水印导出";
        case DateFilter: return "日期筛选";
        default: return "未知";
    }
}

// 权限组合转描述字符串
inline QString permissionsToString(UserPermissions permissions) {
    if (permissions == NoPermission) return "无权限";
    if (permissions == PermissionPresets::Full) return "完整权限";

    QStringList items;
    if (permissions & ViewFileList) items << "浏览";
    if (permissions & PreviewFile) items << "预览";
    if (permissions & PrintFile) items << "打印";
    if (permissions & DownloadFile) items << "下载";
    if (permissions & BatchDownload) items << "批量下载";
    if (permissions & WatermarkExport) items << "水印";
    if (permissions & DateFilter) items << "日期筛选";

    return items.join("、");
}

struct User {
    QString username;
    QString passwordHash;  // SHA256 hash
    QDateTime createdAt;
    bool isAdmin;
    bool isActive;
    UserPermissions permissions;  // 用户权限位标志

    User() : isAdmin(false), isActive(true), permissions(PermissionPresets::Download) {}
};

class UserManager {
public:
    static UserManager* instance();

    bool addUser(const QString& username, const QString& password, bool isAdmin = false, UserPermissions permissions = PermissionPresets::Download);
    bool removeUser(const QString& username);
    bool updatePassword(const QString& username, const QString& newPassword);
    bool setAdmin(const QString& username, bool isAdmin);
    bool setActive(const QString& username, bool isActive);
    bool setPermissions(const QString& username, UserPermissions permissions);

    bool authenticate(const QString& username, const QString& password);
    User getUser(const QString& username) const;
    UserPermissions getUserPermissions(const QString& username) const;
    bool hasPermission(const QString& username, UserPermissionFlag permission) const;
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
