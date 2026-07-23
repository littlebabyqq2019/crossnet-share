#include "user_management_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QCheckBox>
#include <QGroupBox>
#include <QCoreApplication>
#include <QDebug>

namespace CrossNetShare {

UserManagementDialog::UserManagementDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    loadUsers();
    updateButtonStates();
}

void UserManagementDialog::setupUi() {
    setWindowTitle("用户管理");
    resize(800, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 用户表格
    userTable_ = new QTableWidget(this);
    userTable_->setColumnCount(5);
    userTable_->setHorizontalHeaderLabels({"用户名", "权限级别", "管理员", "状态", "创建时间"});
    userTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    userTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    userTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    userTable_->horizontalHeader()->setStretchLastSection(true);
    userTable_->setColumnWidth(0, 150);
    userTable_->setColumnWidth(1, 120);
    userTable_->setColumnWidth(2, 80);
    userTable_->setColumnWidth(3, 80);

    connect(userTable_, &QTableWidget::itemSelectionChanged, this, &UserManagementDialog::onTableSelectionChanged);
    connect(userTable_, &QTableWidget::itemDoubleClicked, this, &UserManagementDialog::onEditUser);

    mainLayout->addWidget(userTable_);

    // 按钮区域
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    addButton_ = new QPushButton("添加用户", this);
    connect(addButton_, &QPushButton::clicked, this, &UserManagementDialog::onAddUser);
    buttonLayout->addWidget(addButton_);

    editButton_ = new QPushButton("编辑", this);
    connect(editButton_, &QPushButton::clicked, this, &UserManagementDialog::onEditUser);
    buttonLayout->addWidget(editButton_);

    deleteButton_ = new QPushButton("删除", this);
    connect(deleteButton_, &QPushButton::clicked, this, &UserManagementDialog::onDeleteUser);
    buttonLayout->addWidget(deleteButton_);

    refreshButton_ = new QPushButton("刷新", this);
    connect(refreshButton_, &QPushButton::clicked, this, &UserManagementDialog::onRefresh);
    buttonLayout->addWidget(refreshButton_);

    closeButton_ = new QPushButton("关闭", this);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton_);

    mainLayout->addLayout(buttonLayout);
}

void UserManagementDialog::loadUsers() {
    userTable_->setRowCount(0);

    QVector<User> users = UserManager::instance()->getAllUsers();
    for (const User& user : users) {
        int row = userTable_->rowCount();
        userTable_->insertRow(row);

        userTable_->setItem(row, 0, new QTableWidgetItem(user.username));
        userTable_->setItem(row, 1, new QTableWidgetItem(permissionsToString(user.permissions)));
        userTable_->setItem(row, 2, new QTableWidgetItem(user.isAdmin ? "是" : "否"));
        userTable_->setItem(row, 3, new QTableWidgetItem(user.isActive ? "启用" : "禁用"));
        userTable_->setItem(row, 4, new QTableWidgetItem(user.createdAt.toString("yyyy-MM-dd HH:mm")));
    }
}

void UserManagementDialog::onAddUser() {
    AddEditUserDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString username = dialog.getUsername();
        QString password = dialog.getPassword();
        bool isAdmin = dialog.isAdmin();
        UserPermissions permissions = dialog.getPermissions();

        if (UserManager::instance()->addUser(username, password, isAdmin, permissions)) {
            // 保存到文件
            QString configFile = QCoreApplication::applicationDirPath() + "/users.json";
            qDebug() << "[UserManagement] About to save to:" << configFile;
            qDebug() << "[UserManagement] Current user count:" << UserManager::instance()->getAllUsers().size();

            bool saved = UserManager::instance()->saveToFile(configFile);

            qDebug() << "[UserManagement] Save result:" << (saved ? "SUCCESS" : "FAILED");

            if (saved) {
                QMessageBox::information(this, "成功", "用户添加成功，已保存到:\n" + configFile);
            } else {
                QMessageBox::warning(this, "保存失败", "用户已添加但保存到文件失败:\n" + configFile);
            }
            loadUsers();
        } else {
            QMessageBox::warning(this, "失败", "用户添加失败，用户名可能已存在");
        }
    }
}

void UserManagementDialog::onEditUser() {
    int currentRow = userTable_->currentRow();
    if (currentRow < 0) {
        return;
    }

    QString username = userTable_->item(currentRow, 0)->text();
    User user = UserManager::instance()->getUser(username);

    AddEditUserDialog dialog(this, &user);
    if (dialog.exec() == QDialog::Accepted) {
        // 更新权限
        UserPermissions permissions = dialog.getPermissions();
        UserManager::instance()->setPermissions(username, permissions);

        // 更新管理员状态
        bool isAdmin = dialog.isAdmin();
        UserManager::instance()->setAdmin(username, isAdmin);

        // 如果修改了密码
        QString password = dialog.getPassword();
        if (!password.isEmpty()) {
            UserManager::instance()->updatePassword(username, password);
        }

        // 保存到文件
        QString configFile = QCoreApplication::applicationDirPath() + "/users.json";
        qDebug() << "[UserManagement] About to save after edit to:" << configFile;

        bool saved = UserManager::instance()->saveToFile(configFile);

        qDebug() << "[UserManagement] Edit save result:" << (saved ? "SUCCESS" : "FAILED");

        if (saved) {
            QMessageBox::information(this, "成功", "用户信息更新成功");
        } else {
            QMessageBox::warning(this, "保存失败", "用户已更新但保存到文件失败");
        }
        loadUsers();
    }
}

void UserManagementDialog::onDeleteUser() {
    int currentRow = userTable_->currentRow();
    if (currentRow < 0) {
        return;
    }

    QString username = userTable_->item(currentRow, 0)->text();

    if (username == "admin") {
        QMessageBox::warning(this, "禁止操作", "不能删除默认管理员账户");
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认删除",
        QString("确定要删除用户 \"%1\" 吗？").arg(username),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        if (UserManager::instance()->removeUser(username)) {
            // 保存到文件
            QString configFile = QCoreApplication::applicationDirPath() + "/users.json";
            qDebug() << "[UserManagement] About to save after delete to:" << configFile;

            bool saved = UserManager::instance()->saveToFile(configFile);

            qDebug() << "[UserManagement] Delete save result:" << (saved ? "SUCCESS" : "FAILED");

            if (saved) {
                QMessageBox::information(this, "成功", "用户删除成功");
            } else {
                QMessageBox::warning(this, "保存失败", "用户已删除但保存到文件失败");
            }
            loadUsers();
        } else {
            QMessageBox::warning(this, "失败", "用户删除失败");
        }
    }
}

void UserManagementDialog::onRefresh() {
    loadUsers();
}

void UserManagementDialog::onTableSelectionChanged() {
    updateButtonStates();
}

void UserManagementDialog::updateButtonStates() {
    bool hasSelection = userTable_->currentRow() >= 0;
    editButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
}

// AddEditUserDialog 实现

AddEditUserDialog::AddEditUserDialog(QWidget* parent, const User* user)
    : QDialog(parent)
    , editingUser_(user)
    , updatingCheckboxes_(false)
{
    setupUi();
}

void AddEditUserDialog::setupUi() {
    setWindowTitle(editingUser_ ? "编辑用户" : "添加用户");
    resize(500, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 用户名
    QHBoxLayout* usernameLayout = new QHBoxLayout();
    usernameLayout->addWidget(new QLabel("用户名:", this));
    usernameEdit_ = new QLineEdit(this);
    if (editingUser_) {
        usernameEdit_->setText(editingUser_->username);
        usernameEdit_->setReadOnly(true);  // 编辑时不能修改用户名
    }
    usernameLayout->addWidget(usernameEdit_);
    mainLayout->addLayout(usernameLayout);

    // 密码
    QHBoxLayout* passwordLayout = new QHBoxLayout();
    passwordLayout->addWidget(new QLabel("密码:", this));
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    if (editingUser_) {
        passwordEdit_->setPlaceholderText("留空表示不修改密码");
    }
    passwordLayout->addWidget(passwordEdit_);
    mainLayout->addLayout(passwordLayout);

    // 管理员
    adminCheckBox_ = new QCheckBox("管理员", this);
    if (editingUser_) {
        adminCheckBox_->setChecked(editingUser_->isAdmin);
        if (editingUser_->username == "admin") {
            adminCheckBox_->setEnabled(false);  // 默认管理员不能取消管理员身份
        }
    }
    mainLayout->addWidget(adminCheckBox_);

    mainLayout->addSpacing(10);

    // 预设权限快速选择
    QHBoxLayout* presetLayout = new QHBoxLayout();
    presetLayout->addWidget(new QLabel("权限预设:", this));
    presetCombo_ = new QComboBox(this);
    presetCombo_->addItem("自定义", -1);
    presetCombo_->addItem("只读（浏览+预览）", static_cast<int>(PermissionPresets::ReadOnly));
    presetCombo_->addItem("打印（只读+打印）", static_cast<int>(PermissionPresets::Print));
    presetCombo_->addItem("下载（打印+下载+批量+日期筛选）", static_cast<int>(PermissionPresets::Download));
    presetCombo_->addItem("完整权限（所有功能）", static_cast<int>(PermissionPresets::Full));
    presetCombo_->setCurrentIndex(0);  // 默认自定义
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AddEditUserDialog::onPresetSelected);
    presetLayout->addWidget(presetCombo_);
    presetLayout->addStretch();
    mainLayout->addLayout(presetLayout);

    // 细粒度权限复选框
    QGroupBox* permissionGroup = new QGroupBox("详细权限（可单独勾选）", this);
    QVBoxLayout* permissionLayout = new QVBoxLayout(permissionGroup);

    // 创建权限复选框
    struct PermissionItem {
        UserPermissionFlag flag;
        QString label;
        QString tooltip;
    };

    QVector<PermissionItem> permissions = {
        {UserPermissionFlag::ViewFileList, "浏览文件列表", "查看共享文件夹中的文件列表"},
        {UserPermissionFlag::PreviewFile, "预览文件", "在线预览Word、PDF等文档内容"},
        {UserPermissionFlag::PrintFile, "打印", "打印文档（需要预览权限）"},
        {UserPermissionFlag::DownloadFile, "下载文件", "下载单个文件到本地"},
        {UserPermissionFlag::BatchDownload, "批量下载", "批量下载多个文件"},
        {UserPermissionFlag::WatermarkExport, "水印导出", "导出带水印的文档"},
        {UserPermissionFlag::DateFilter, "日期筛选", "按日期范围筛选文件"},
    };

    for (const auto& perm : permissions) {
        QCheckBox* checkbox = new QCheckBox(perm.label, permissionGroup);
        checkbox->setToolTip(perm.tooltip);
        connect(checkbox, &QCheckBox::stateChanged,
                this, &AddEditUserDialog::onPermissionChanged);
        permissionCheckboxes_[perm.flag] = checkbox;
        permissionLayout->addWidget(checkbox);
    }

    mainLayout->addWidget(permissionGroup);

    // 初始化权限状态
    if (editingUser_) {
        updatePermissionCheckboxes(editingUser_->permissions);
    } else {
        updatePermissionCheckboxes(PermissionPresets::Download);  // 默认下载权限
        presetCombo_->setCurrentIndex(3);  // 对应"下载"预设
    }

    mainLayout->addStretch();

    // 按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    okButton_ = new QPushButton("确定", this);
    connect(okButton_, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(okButton_);

    cancelButton_ = new QPushButton("取消", this);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton_);

    mainLayout->addLayout(buttonLayout);
}

void AddEditUserDialog::onPresetSelected(int index) {
    if (updatingCheckboxes_) return;

    int presetValue = presetCombo_->currentData().toInt();
    if (presetValue == -1) {
        // 自定义，不改变复选框状态
        return;
    }

    UserPermissions preset = UserPermissions(presetValue);
    updatePermissionCheckboxes(preset);
}

void AddEditUserDialog::onPermissionChanged() {
    if (updatingCheckboxes_) return;

    // 用户手动修改了复选框，设置为自定义
    updatingCheckboxes_ = true;
    presetCombo_->setCurrentIndex(0);  // 切换到"自定义"
    updatingCheckboxes_ = false;
}

void AddEditUserDialog::updatePermissionCheckboxes(UserPermissions permissions) {
    updatingCheckboxes_ = true;

    for (auto it = permissionCheckboxes_.begin(); it != permissionCheckboxes_.end(); ++it) {
        it.value()->setChecked(permissions.testFlag(it.key()));
    }

    updatingCheckboxes_ = false;
}

UserPermissions AddEditUserDialog::getPermissions() const {
    UserPermissions permissions = UserPermissionFlag::NoPermission;

    for (auto it = permissionCheckboxes_.begin(); it != permissionCheckboxes_.end(); ++it) {
        if (it.value()->isChecked()) {
            permissions |= it.key();
        }
    }

    return permissions;
}

}
