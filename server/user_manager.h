#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMutex>

namespace CrossNetShare {

struct User {
    QString username;
    QString passwordHash;  // SHA256 hash
    QDateTime createdAt;
    bool isAdmin;
    bool isActive;

    User() : isAdmin(false), isActive(true) {}
};

class UserManager {
public:
    static UserManager* instance();

    bool addUser(const QString& username, const QString& password, bool isAdmin = false);
    bool removeUser(const QString& username);
    bool updatePassword(const QString& username, const QString& newPassword);
    bool setAdmin(const QString& username, bool isAdmin);
    bool setActive(const QString& username, bool isActive);

    bool authenticate(const QString& username, const QString& password);
    User getUser(const QString& username) const;
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
