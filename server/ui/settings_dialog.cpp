#include "settings_dialog.h"
#include "user_management_dialog.h"
#include "common/autostart.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QLabel>
#include <QCoreApplication>

namespace CrossNetShare {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    loadUsers();
}

void SettingsDialog::setupUi() {
    setWindowTitle("服务器设置");
    resize(700, 600);

    // 设置字体
    QFont appFont = font();
    appFont.setPointSize(10);
    setFont(appFont);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 服务器选项区域
    QGroupBox* optionsGroup = new QGroupBox("服务器选项", this);
    optionsGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 11pt; padding-top: 16px; border: 2px solid #e5e7eb; border-radius: 8px; } "
                                "QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; background-color: white; }");
    QVBoxLayout* optionsLayout = new QVBoxLayout(optionsGroup);
    optionsLayout->setSpacing(12);

    uploadEnabledCheckBox_ = new QCheckBox("允许客户端上传文件");
    uploadEnabledCheckBox_->setChecked(true);
    uploadEnabledCheckBox_->setStyleSheet("QCheckBox { font-weight: normal; font-size: 10pt; spacing: 8px; }");
    optionsLayout->addWidget(uploadEnabledCheckBox_);

    webEnabledCheckBox_ = new QCheckBox("启用 Web 浏览器访问");
    webEnabledCheckBox_->setChecked(true);
    webEnabledCheckBox_->setStyleSheet("QCheckBox { font-weight: normal; font-size: 10pt; spacing: 8px; }");
    optionsLayout->addWidget(webEnabledCheckBox_);

    autoStartCheckBox_ = new QCheckBox("Windows 开机自动启动服务器");
    autoStartCheckBox_->setChecked(AutoStart::isAutoStartEnabled("CrossNetShareServer"));
    autoStartCheckBox_->setStyleSheet("QCheckBox { font-weight: normal; font-size: 10pt; spacing: 8px; }");
    connect(autoStartCheckBox_, &QCheckBox::stateChanged, this, [this](int state) {
        bool enable = (state == Qt::Checked);
        QString appPath = QCoreApplication::applicationFilePath();
        if (!AutoStart::setAutoStart("CrossNetShareServer", appPath, enable)) {
            QMessageBox::warning(this, "警告", "修改开机自启动设置失败");
            autoStartCheckBox_->setChecked(!enable);
        }
    });
    optionsLayout->addWidget(autoStartCheckBox_);

    mainLayout->addWidget(optionsGroup);

    // 用户管理区域
    QGroupBox* userGroup = new QGroupBox("用户管理", this);
    userGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 11pt; padding-top: 16px; border: 2px solid #e5e7eb; border-radius: 8px; } "
                             "QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; background-color: white; }");
    QVBoxLayout* userLayout = new QVBoxLayout(userGroup);
    userLayout->setSpacing(12);

    QLabel* infoLabel = new QLabel("管理 Web 访问的用户账户");
    infoLabel->setStyleSheet("color: #6b7280; font-size: 9pt; font-weight: normal;");
    userLayout->addWidget(infoLabel);

    userTable_ = new QTableWidget();
    userTable_->setColumnCount(4);
    userTable_->setHorizontalHeaderLabels({"用户名", "管理员", "状态", "创建时间"});
    userTable_->horizontalHeader()->setStretchLastSection(true);
    userTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    userTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    userTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    userTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    userTable_->setStyleSheet("QTableWidget { border: 1px solid #e5e7eb; border-radius: 4px; font-weight: normal; } "
                              "QHeaderView::section { background-color: #f9fafb; font-weight: bold; padding: 8px; }");
    userLayout->addWidget(userTable_);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    addUserButton_ = new QPushButton("添加用户");
    addUserButton_->setStyleSheet("QPushButton { padding: 8px 20px; background-color: #2563eb; color: white; border: none; border-radius: 6px; } "
                                   "QPushButton:hover { background-color: #1d4ed8; }");
    addUserButton_->setMinimumHeight(36);
    connect(addUserButton_, &QPushButton::clicked, this, &SettingsDialog::onAddUserClicked);
    buttonLayout->addWidget(addUserButton_);

    removeUserButton_ = new QPushButton("删除用户");
    removeUserButton_->setStyleSheet("QPushButton { padding: 8px 20px; background-color: #dc2626; color: white; border: none; border-radius: 6px; } "
                                      "QPushButton:hover { background-color: #b91c1c; }");
    removeUserButton_->setMinimumHeight(36);
    connect(removeUserButton_, &QPushButton::clicked, this, &SettingsDialog::onRemoveUserClicked);
    buttonLayout->addWidget(removeUserButton_);

    changePasswordButton_ = new QPushButton("修改密码");
    changePasswordButton_->setStyleSheet("QPushButton { padding: 8px 20px; background-color: white; border: 1px solid #d1d5db; border-radius: 6px; } "
                                          "QPushButton:hover { background-color: #f3f4f6; }");
    changePasswordButton_->setMinimumHeight(36);
    connect(changePasswordButton_, &QPushButton::clicked, this, &SettingsDialog::onChangePasswordClicked);
    buttonLayout->addWidget(changePasswordButton_);

    refreshButton_ = new QPushButton("刷新");
    refreshButton_->setStyleSheet("QPushButton { padding: 8px 20px; background-color: white; border: 1px solid #d1d5db; border-radius: 6px; } "
                                   "QPushButton:hover { background-color: #f3f4f6; }");
    refreshButton_->setMinimumHeight(36);
    connect(refreshButton_, &QPushButton::clicked, this, &SettingsDialog::onRefreshUsersClicked);
    buttonLayout->addWidget(refreshButton_);

    QPushButton* userManagementButton = new QPushButton("高级用户管理");
    userManagementButton->setStyleSheet("QPushButton { padding: 8px 20px; background-color: #059669; color: white; border: none; border-radius: 6px; } "
                                         "QPushButton:hover { background-color: #047857; }");
    userManagementButton->setMinimumHeight(36);
    connect(userManagementButton, &QPushButton::clicked, this, &SettingsDialog::onUserManagementClicked);
    buttonLayout->addWidget(userManagementButton);

    buttonLayout->addStretch();
    userLayout->addLayout(buttonLayout);

    mainLayout->addWidget(userGroup);

    // 关闭按钮
    QHBoxLayout* closeLayout = new QHBoxLayout();
    closeLayout->addStretch();

    QPushButton* closeButton = new QPushButton("关闭");
    closeButton->setStyleSheet("QPushButton { padding: 8px 24px; background-color: white; border: 1px solid #d1d5db; border-radius: 6px; } "
                                "QPushButton:hover { background-color: #f3f4f6; }");
    closeButton->setMinimumHeight(36);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    closeLayout->addWidget(closeButton);

    mainLayout->addLayout(closeLayout);
}

void SettingsDialog::loadUsers() {
    userTable_->setRowCount(0);

    QVector<User> users = UserManager::instance()->getAllUsers();
    for (const User& user : users) {
        int row = userTable_->rowCount();
        userTable_->insertRow(row);

        userTable_->setItem(row, 0, new QTableWidgetItem(user.username));
        userTable_->setItem(row, 1, new QTableWidgetItem(user.isAdmin ? "是" : "否"));
        userTable_->setItem(row, 2, new QTableWidgetItem(user.isActive ? "启用" : "禁用"));
        userTable_->setItem(row, 3, new QTableWidgetItem(user.createdAt.toString("yyyy-MM-dd HH:mm:ss")));
    }
}

void SettingsDialog::onAddUserClicked() {
    bool ok;
    QString username = QInputDialog::getText(this, "添加用户", "用户名:", QLineEdit::Normal, "", &ok);
    if (!ok || username.isEmpty()) {
        return;
    }

    QString password = QInputDialog::getText(this, "添加用户", "密码:", QLineEdit::Password, "", &ok);
    if (!ok || password.isEmpty()) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this, "添加用户",
                                                               "是否设置为管理员?",
                                                               QMessageBox::Yes | QMessageBox::No);
    bool isAdmin = (reply == QMessageBox::Yes);

    if (UserManager::instance()->addUser(username, password, isAdmin)) {
        QMessageBox::information(this, "成功", "用户添加成功！");
        loadUsers();
    } else {
        QMessageBox::warning(this, "失败", "用户添加失败！可能用户名已存在。");
    }
}

void SettingsDialog::onRemoveUserClicked() {
    int row = userTable_->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先选择要删除的用户！");
        return;
    }

    QString username = userTable_->item(row, 0)->text();

    if (username == "admin") {
        QMessageBox::warning(this, "提示", "不能删除默认管理员账户！");
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this, "确认删除",
                                                               "确定要删除用户 \"" + username + "\" 吗？",
                                                               QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (UserManager::instance()->removeUser(username)) {
        QMessageBox::information(this, "成功", "用户删除成功！");
        loadUsers();
    } else {
        QMessageBox::warning(this, "失败", "用户删除失败！");
    }
}

void SettingsDialog::onChangePasswordClicked() {
    int row = userTable_->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先选择要修改密码的用户！");
        return;
    }

    QString username = userTable_->item(row, 0)->text();

    bool ok;
    QString newPassword = QInputDialog::getText(this, "修改密码", "新密码:", QLineEdit::Password, "", &ok);
    if (!ok || newPassword.isEmpty()) {
        return;
    }

    if (UserManager::instance()->updatePassword(username, newPassword)) {
        QMessageBox::information(this, "成功", "密码修改成功！");
    } else {
        QMessageBox::warning(this, "失败", "密码修改失败！");
    }
}

void SettingsDialog::onRefreshUsersClicked() {
    loadUsers();
}

bool SettingsDialog::getUploadEnabled() const {
    return uploadEnabledCheckBox_->isChecked();
}

bool SettingsDialog::getWebEnabled() const {
    return webEnabledCheckBox_->isChecked();
}

bool SettingsDialog::getAutoStartEnabled() const {
    return autoStartCheckBox_->isChecked();
}

void SettingsDialog::onUserManagementClicked() {
    UserManagementDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // 刷新用户列表
        loadUsers();
    }
}

}
