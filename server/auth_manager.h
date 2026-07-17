#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <QCryptographicHash>

namespace CrossNetShare {

struct Session {
    QString token;
    QString username;
    QDateTime loginTime;
    QDateTime lastAccess;
};

class AuthManager : public QObject {
    Q_OBJECT

public:
    explicit AuthManager(QObject* parent = nullptr);
    ~AuthManager();

    // 加载用户配置
    bool loadUsers(const QString& configFile);

    // 保存用户配置
    bool saveUsers(const QString& configFile);

    // 用户认证
    QString authenticate(const QString& username, const QString& password);

    // 验证会话
    bool validateSession(const QString& token, QString& username);

    // 登出
    void logout(const QString& token);

    // 添加用户
    bool addUser(const QString& username, const QString& password, const QString& role = "user");

    // 删除用户
    bool removeUser(const QString& username);

    // 修改密码
    bool changePassword(const QString& username, const QString& oldPassword, const QString& newPassword);

    // 清理过期会话
    void cleanupExpiredSessions();

signals:
    void userLoggedIn(const QString& username);
    void userLoggedOut(const QString& username);
    void logMessage(const QString& message);

private:
    QString hashPassword(const QString& password, const QString& salt = "");
    QString generateToken();
    QString generateSalt();

    QMap<QString, Session> sessions_;     // token -> Session
    int sessionTimeout_;                   // 会话超时时间（秒），默认1800（30分钟）
};

}
