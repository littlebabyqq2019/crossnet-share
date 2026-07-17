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

private slots:
    void onAddUserClicked();
    void onRemoveUserClicked();
    void onChangePasswordClicked();
    void onRefreshUsersClicked();

private:
    void setupUi();
    void loadUsers();

    QTableWidget* userTable_;
    QPushButton* addUserButton_;
    QPushButton* removeUserButton_;
    QPushButton* changePasswordButton_;
    QPushButton* refreshButton_;
};

}
