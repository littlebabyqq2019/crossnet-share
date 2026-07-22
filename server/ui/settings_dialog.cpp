#include "settings_dialog.h"
#include "user_management_dialog.h"
#include "audit_log_dialog.h"
#include "common/autostart.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QLabel>
#include <QCoreApplication>
#include <QColorDialog>
#include <QProcess>

namespace CrossNetShare {

SettingsDialog::SettingsDialog(WatermarkService* watermarkService, QWidget* parent)
    : QDialog(parent)
    , watermarkService_(watermarkService)
    , currentColor_(128, 128, 128)
{
    setupUi();
    loadUsers();
    loadWatermarkSettings();
}

void SettingsDialog::setupUi() {
    setWindowTitle("服务器设置");
    resize(800, 650);

    // 设置字体
    QFont appFont = font();
    appFont.setPointSize(10);
    setFont(appFont);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 创建标签页
    tabWidget_ = new QTabWidget(this);
    mainLayout->addWidget(tabWidget_);

    // 添加"基本设置"标签页
    QWidget* basicTab = new QWidget();
    QVBoxLayout* basicLayout = new QVBoxLayout(basicTab);
    basicLayout->setSpacing(16);
    basicLayout->setContentsMargins(10, 10, 10, 10);

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

    basicLayout->addWidget(optionsGroup);

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

    QPushButton* auditLogButton = new QPushButton("审计日志");
    auditLogButton->setStyleSheet("QPushButton { padding: 8px 20px; background-color: #7c3aed; color: white; border: none; border-radius: 6px; } "
                                   "QPushButton:hover { background-color: #6d28d9; }");
    auditLogButton->setMinimumHeight(36);
    connect(auditLogButton, &QPushButton::clicked, this, &SettingsDialog::onAuditLogClicked);
    buttonLayout->addWidget(auditLogButton);

    buttonLayout->addStretch();
    userLayout->addLayout(buttonLayout);

    basicLayout->addWidget(userGroup);
    basicLayout->addStretch();

    // 添加基本设置标签页
    tabWidget_->addTab(basicTab, "基本设置");

    // 添加水印设置标签页
    QWidget* watermarkTab = new QWidget();
    QVBoxLayout* watermarkLayout = new QVBoxLayout(watermarkTab);
    watermarkLayout->setSpacing(12);
    watermarkLayout->setContentsMargins(10, 10, 10, 10);

    // 启用水印
    enableWatermarkCheckBox_ = new QCheckBox("启用水印功能");
    enableWatermarkCheckBox_->setStyleSheet("font-weight: bold; font-size: 11pt;");
    connect(enableWatermarkCheckBox_, &QCheckBox::toggled, this, &SettingsDialog::onEnableWatermarkToggled);
    watermarkLayout->addWidget(enableWatermarkCheckBox_);

    // 关键词规则
    QGroupBox* keywordGroup = new QGroupBox("关键词水印规则");
    QVBoxLayout* keywordLayout = new QVBoxLayout(keywordGroup);

    keywordsTable_ = new QTableWidget();
    keywordsTable_->setColumnCount(2);
    keywordsTable_->setHorizontalHeaderLabels({"关键词", "水印文字"});
    keywordsTable_->horizontalHeader()->setStretchLastSection(true);
    keywordsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    keywordsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    keywordLayout->addWidget(keywordsTable_);

    QHBoxLayout* keywordButtonLayout = new QHBoxLayout();
    addKeywordButton_ = new QPushButton("添加");
    editKeywordButton_ = new QPushButton("编辑");
    deleteKeywordButton_ = new QPushButton("删除");
    connect(addKeywordButton_, &QPushButton::clicked, this, &SettingsDialog::onAddKeywordClicked);
    connect(editKeywordButton_, &QPushButton::clicked, this, &SettingsDialog::onEditKeywordClicked);
    connect(deleteKeywordButton_, &QPushButton::clicked, this, &SettingsDialog::onDeleteKeywordClicked);
    keywordButtonLayout->addWidget(addKeywordButton_);
    keywordButtonLayout->addWidget(editKeywordButton_);
    keywordButtonLayout->addWidget(deleteKeywordButton_);
    keywordButtonLayout->addStretch();
    keywordLayout->addLayout(keywordButtonLayout);

    watermarkLayout->addWidget(keywordGroup);

    // 统一水印
    QGroupBox* unifiedGroup = new QGroupBox("统一水印");
    QVBoxLayout* unifiedLayout = new QVBoxLayout(unifiedGroup);

    enableUnifiedCheckBox_ = new QCheckBox("启用统一水印（应用于所有文档）");
    unifiedLayout->addWidget(enableUnifiedCheckBox_);

    QHBoxLayout* unifiedTextLayout = new QHBoxLayout();
    unifiedTextLayout->addWidget(new QLabel("水印文字:"));
    unifiedTextEdit_ = new QLineEdit();
    unifiedTextLayout->addWidget(unifiedTextEdit_);
    unifiedLayout->addLayout(unifiedTextLayout);

    watermarkLayout->addWidget(unifiedGroup);

    // 水印样式
    QGroupBox* styleGroup = new QGroupBox("水印样式");
    QGridLayout* styleLayout = new QGridLayout(styleGroup);

    styleLayout->addWidget(new QLabel("字号:"), 0, 0);
    fontSizeSpinBox_ = new QSpinBox();
    fontSizeSpinBox_->setRange(8, 72);
    fontSizeSpinBox_->setValue(14);
    connect(fontSizeSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::onFontSizeChanged);
    styleLayout->addWidget(fontSizeSpinBox_, 0, 1);

    styleLayout->addWidget(new QLabel("颜色:"), 1, 0);
    colorButton_ = new QPushButton("选择颜色");
    connect(colorButton_, &QPushButton::clicked, this, &SettingsDialog::onColorButtonClicked);
    styleLayout->addWidget(colorButton_, 1, 1);

    styleLayout->addWidget(new QLabel("透明度:"), 2, 0);
    QHBoxLayout* opacityLayout = new QHBoxLayout();
    opacitySlider_ = new QSlider(Qt::Horizontal);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setValue(30);
    connect(opacitySlider_, &QSlider::valueChanged, this, &SettingsDialog::onOpacityChanged);
    opacityLabel_ = new QLabel("30%");
    opacityLayout->addWidget(opacitySlider_);
    opacityLayout->addWidget(opacityLabel_);
    styleLayout->addLayout(opacityLayout, 2, 1);

    styleLayout->addWidget(new QLabel("旋转角度:"), 3, 0);
    QHBoxLayout* rotationLayout = new QHBoxLayout();
    rotationSlider_ = new QSlider(Qt::Horizontal);
    rotationSlider_->setRange(-90, 90);
    rotationSlider_->setValue(45);
    connect(rotationSlider_, &QSlider::valueChanged, this, &SettingsDialog::onRotationChanged);
    rotationLabel_ = new QLabel("45°");
    rotationLayout->addWidget(rotationSlider_);
    rotationLayout->addWidget(rotationLabel_);
    styleLayout->addLayout(rotationLayout, 3, 1);

    styleLayout->addWidget(new QLabel("密度:"), 4, 0);
    densityComboBox_ = new QComboBox();
    densityComboBox_->addItems({"稀疏", "中等", "密集"});
    densityComboBox_->setCurrentIndex(1);
    styleLayout->addWidget(densityComboBox_, 4, 1);

    watermarkLayout->addWidget(styleGroup);

    // LibreOffice 检测
    QHBoxLayout* detectLayout = new QHBoxLayout();
    detectLibreOfficeButton_ = new QPushButton("检测 LibreOffice");
    connect(detectLibreOfficeButton_, &QPushButton::clicked, this, &SettingsDialog::onDetectLibreOfficeClicked);
    detectLayout->addWidget(detectLibreOfficeButton_);
    detectLayout->addStretch();
    watermarkLayout->addLayout(detectLayout);

    watermarkLayout->addStretch();
    tabWidget_->addTab(watermarkTab, "水印设置");

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

void SettingsDialog::onAuditLogClicked() {
    AuditLogDialog dialog(this);
    dialog.exec();
}

void SettingsDialog::loadWatermarkSettings() {
    if (!watermarkService_) return;

    WatermarkService::WatermarkConfig config = watermarkService_->getConfig();

    enableWatermarkCheckBox_->setChecked(config.enabled);
    enableUnifiedCheckBox_->setChecked(config.enableUnified);
    unifiedTextEdit_->setText(config.unifiedText);
    fontSizeSpinBox_->setValue(config.fontSize);
    currentColor_ = config.color;
    updateColorButton();
    opacitySlider_->setValue(config.opacity);
    rotationSlider_->setValue(config.rotation);

    int densityIndex = 1; // medium
    if (config.density == "sparse") densityIndex = 0;
    else if (config.density == "dense") densityIndex = 2;
    densityComboBox_->setCurrentIndex(densityIndex);

    updateKeywordsTable();
}

void SettingsDialog::saveWatermarkSettings() {
    if (!watermarkService_) return;

    WatermarkService::WatermarkConfig config;
    config.enabled = enableWatermarkCheckBox_->isChecked();
    config.enableUnified = enableUnifiedCheckBox_->isChecked();
    config.unifiedText = unifiedTextEdit_->text();
    config.fontSize = fontSizeSpinBox_->value();
    config.color = currentColor_;
    config.opacity = opacitySlider_->value();
    config.rotation = rotationSlider_->value();

    QStringList densities = {"sparse", "medium", "dense"};
    config.density = densities[densityComboBox_->currentIndex()];

    watermarkService_->setConfig(config);
    watermarkService_->saveConfig();
}

void SettingsDialog::updateKeywordsTable() {
    if (!watermarkService_) return;

    keywordsTable_->setRowCount(0);
    QList<WatermarkService::KeywordRule> keywords = watermarkService_->getKeywords();

    for (const auto& rule : keywords) {
        int row = keywordsTable_->rowCount();
        keywordsTable_->insertRow(row);
        keywordsTable_->setItem(row, 0, new QTableWidgetItem(rule.keyword));
        keywordsTable_->setItem(row, 1, new QTableWidgetItem(rule.watermarkText));
    }
}

void SettingsDialog::updateColorButton() {
    QString styleSheet = QString("background-color: %1; border: 1px solid #ccc; border-radius: 4px; min-width: 100px; min-height: 30px;")
                            .arg(currentColor_.name());
    colorButton_->setStyleSheet(styleSheet);
}

void SettingsDialog::onAddKeywordClicked() {
    bool okDetect, okWatermark;
    QString detectText = QInputDialog::getText(this, "添加关键词", "检测文本:", QLineEdit::Normal, "", &okDetect);
    if (!okDetect || detectText.isEmpty()) {
        return;
    }

    QString watermarkText = QInputDialog::getText(this, "添加关键词", "水印文本:", QLineEdit::Normal, detectText, &okWatermark);
    if (!okWatermark || watermarkText.isEmpty()) {
        return;
    }

    WatermarkService::KeywordRule rule;
    rule.keyword = detectText;
    rule.watermarkText = watermarkText;

    QList<WatermarkService::KeywordRule> keywords = watermarkService_->getKeywords();
    keywords.append(rule);
    watermarkService_->setKeywords(keywords);
    watermarkService_->saveConfig();
    updateKeywordsTable();
}

void SettingsDialog::onEditKeywordClicked() {
    int row = keywordsTable_->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先选择要编辑的关键词");
        return;
    }

    QList<WatermarkService::KeywordRule> keywords = watermarkService_->getKeywords();
    if (row >= keywords.size()) return;

    bool okDetect, okWatermark;
    QString detectText = QInputDialog::getText(this, "编辑关键词", "检测文本:",
                                                QLineEdit::Normal, keywords[row].keyword, &okDetect);
    if (!okDetect || detectText.isEmpty()) {
        return;
    }

    QString watermarkText = QInputDialog::getText(this, "编辑关键词", "水印文本:",
                                                   QLineEdit::Normal, keywords[row].watermarkText, &okWatermark);
    if (!okWatermark || watermarkText.isEmpty()) {
        return;
    }

    keywords[row].keyword = detectText;
    keywords[row].watermarkText = watermarkText;
    watermarkService_->setKeywords(keywords);
    watermarkService_->saveConfig();
    updateKeywordsTable();
}

void SettingsDialog::onDeleteKeywordClicked() {
    int row = keywordsTable_->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "提示", "请先选择要删除的关键词");
        return;
    }

    QList<WatermarkService::KeywordRule> keywords = watermarkService_->getKeywords();
    if (row >= keywords.size()) return;

    keywords.removeAt(row);
    watermarkService_->setKeywords(keywords);
    watermarkService_->saveConfig();
    updateKeywordsTable();
}

void SettingsDialog::onFontSizeChanged(int value) {
    // 实时预览可以在这里实现
}

void SettingsDialog::onOpacityChanged(int value) {
    opacityLabel_->setText(QString::number(value) + "%");
}

void SettingsDialog::onRotationChanged(int value) {
    rotationLabel_->setText(QString::number(value) + "°");
}

void SettingsDialog::onColorButtonClicked() {
    QColor color = QColorDialog::getColor(currentColor_, this, "选择水印颜色");
    if (color.isValid()) {
        currentColor_ = color;
        updateColorButton();
    }
}

void SettingsDialog::onEnableWatermarkToggled(bool checked) {
    // 可以用来启用/禁用其他控件
}

void SettingsDialog::onDetectLibreOfficeClicked() {
    QStringList possiblePaths = {
        "soffice", "soffice.exe",
        "libreoffice", "libreoffice.exe",
        "C:/Program Files/LibreOffice/program/soffice.exe"
    };

    QString found;
    for (const QString& path : possiblePaths) {
        QProcess process;
        process.start(path, QStringList() << "--version");
        if (process.waitForStarted(1000)) {
            process.waitForFinished(3000);
            found = path;
            break;
        }
    }

    if (found.isEmpty()) {
        QMessageBox::information(this, "检测结果", "未找到 LibreOffice\n\n请从 https://www.libreoffice.org/ 下载安装");
    } else {
        QMessageBox::information(this, "检测结果", "已找到 LibreOffice:\n" + found);
    }
}

void SettingsDialog::onSaveClicked() {
    saveWatermarkSettings();
    accept();
}

}
