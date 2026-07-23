#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMutex>
#include <QFlags>

namespace CrossNetShare {

// 用户权限位标志（细粒度权限控制）
enum UserPermissionFlag : unsigned int {
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
    const UserPermissions ReadOnly = UserPermissionFlag::ViewFileList | UserPermissionFlag::PreviewFile;
    const UserPermissions Print = ReadOnly | UserPermissionFlag::PrintFile;
    const UserPermissions Download = Print | UserPermissionFlag::DownloadFile | UserPermissionFlag::BatchDownload | UserPermissionFlag::DateFilter;
    const UserPermissions Full = UserPermissionFlag::AllPermissions;
}

// 权限转字符串（用于显示）
inline QString permissionFlagToString(UserPermissionFlag flag) {
    switch (flag) {
        case UserPermissionFlag::ViewFileList: return "浏览文件列表";
        case UserPermissionFlag::PreviewFile: return "预览文件";
        case UserPermissionFlag::PrintFile: return "打印";
        case UserPermissionFlag::DownloadFile: return "下载";
        case UserPermissionFlag::BatchDownload: return "批量下载";
        case UserPermissionFlag::WatermarkExport: return "水印导出";
        case UserPermissionFlag::DateFilter: return "日期筛选";
        default: return "未知";
    }
}

// 权限组合转描述字符串
inline QString permissionsToString(UserPermissions permissions) {
    if (!permissions) return "无权限";  // NoPermission == 0
    if (permissions == PermissionPresets::Full) return "完整权限";

    QStringList items;
    if (permissions & UserPermissionFlag::ViewFileList) items << "浏览";
    if (permissions & UserPermissionFlag::PreviewFile) items << "预览";
    if (permissions & UserPermissionFlag::PrintFile) items << "打印";
    if (permissions & UserPermissionFlag::DownloadFile) items << "下载";
    if (permissions & UserPermissionFlag::BatchDownload) items << "批量下载";
    if (permissions & UserPermissionFlag::WatermarkExport) items << "水印";
    if (permissions & UserPermissionFlag::DateFilter) items << "日期筛选";

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
