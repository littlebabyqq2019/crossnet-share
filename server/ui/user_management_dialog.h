#pragma once

#include "../user_manager.h"
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>

namespace CrossNetShare {

class UserManagementDialog : public QDialog {
    Q_OBJECT

public:
    explicit UserManagementDialog(QWidget* parent = nullptr);

private slots:
    void onAddUser();
    void onEditUser();
    void onDeleteUser();
    void onRefresh();
    void onTableSelectionChanged();

private:
    void setupUi();
    void loadUsers();
    void updateButtonStates();

    QTableWidget* userTable_;
    QPushButton* addButton_;
    QPushButton* editButton_;
    QPushButton* deleteButton_;
    QPushButton* refreshButton_;
    QPushButton* closeButton_;
};

class AddEditUserDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddEditUserDialog(QWidget* parent = nullptr, const User* user = nullptr);

    QString getUsername() const { return usernameEdit_->text(); }
    QString getPassword() const { return passwordEdit_->text(); }
    bool isAdmin() const { return adminCheckBox_->isChecked(); }
    UserPermission getPermission() const;

private:
    void setupUi();

    QLineEdit* usernameEdit_;
    QLineEdit* passwordEdit_;
    QCheckBox* adminCheckBox_;
    QComboBox* permissionCombo_;
    QPushButton* okButton_;
    QPushButton* cancelButton_;

    const User* editingUser_;
};

}
