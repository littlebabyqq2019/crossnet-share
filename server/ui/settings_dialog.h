#pragma once

#include "../user_manager.h"
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>

namespace CrossNetShare {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // 获取设置
    bool getUploadEnabled() const;
    bool getWebEnabled() const;
    bool getAutoStartEnabled() const;

signals:
    void settingsChanged();

private slots:
    void onAddUserClicked();
    void onRemoveUserClicked();
    void onChangePasswordClicked();
    void onRefreshUsersClicked();
    void onUserManagementClicked();

private:
    void setupUi();
    void loadUsers();

    QTableWidget* userTable_;
    QPushButton* addUserButton_;
    QPushButton* removeUserButton_;
    QPushButton* changePasswordButton_;
    QPushButton* refreshButton_;

    // 服务器选项
    QCheckBox* uploadEnabledCheckBox_;
    QCheckBox* webEnabledCheckBox_;
    QCheckBox* autoStartCheckBox_;
};

}
