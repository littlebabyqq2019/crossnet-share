#include "main_window.h"
#include "settings_dialog.h"
#include "../document_converter.h"
#include "common/autostart.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QCoreApplication>
#include <QApplication>
#include <QCloseEvent>

namespace CrossNetShare {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , server_(new Server(this))
    , cacheCleanupTimer_(new QTimer(this))
    , trayIcon_(nullptr)
    , trayMenu_(nullptr)
{
    setupUi();
    setupTrayIcon();

    connect(server_, &Server::started, this, &MainWindow::onServerStarted);
    connect(server_, &Server::stopped, this, &MainWindow::onServerStopped);
    connect(server_, &Server::clientConnected, this, &MainWindow::onClientConnected);
    connect(server_, &Server::clientDisconnected, this, &MainWindow::onClientDisconnected);
    connect(server_, &Server::logMessage, this, &MainWindow::onLogMessage);
    connect(server_, &Server::error, this, &MainWindow::onServerError);

    connect(cacheCleanupTimer_, &QTimer::timeout, this, &MainWindow::onCleanupCache);
    cacheCleanupTimer_->start(3600000);

    updateServerStatus();

    // 自动启动服务器并隐藏到托盘
    QTimer::singleShot(500, this, [this]() {
        if (!server_->isRunning()) {
            onStartStopClicked();
        }
        // 启动后隐藏到托盘
        hide();
        if (trayIcon_) {
            trayIcon_->showMessage("CrossNetShare 服务器",
                                   "服务器已启动，运行在系统托盘中",
                                   QSystemTrayIcon::Information,
                                   2000);
        }
    });
}

MainWindow::~MainWindow() {
    if (trayIcon_) {
        trayIcon_->hide();
        delete trayIcon_;
    }
    if (trayMenu_) {
        delete trayMenu_;
    }
}

void MainWindow::setupTrayIcon() {
    // 创建托盘菜单
    trayMenu_ = new QMenu(this);

    QAction* showAction = new QAction("显示主窗口", this);
    connect(showAction, &QAction::triggered, this, &MainWindow::onShowWindow);
    trayMenu_->addAction(showAction);

    trayMenu_->addSeparator();

    QAction* quitAction = new QAction("退出", this);
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuitApp);
    trayMenu_->addAction(quitAction);

    // 创建托盘图标
    trayIcon_ = new QSystemTrayIcon(this);
    trayIcon_->setContextMenu(trayMenu_);
    trayIcon_->setToolTip("CrossNetShare 服务器");

    // 使用默认图标
    trayIcon_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));

    connect(trayIcon_, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);

    trayIcon_->show();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // 点击关闭按钮隐藏到托盘而不是退出
    if (trayIcon_ && trayIcon_->isVisible()) {
        hide();
        event->ignore();
        trayIcon_->showMessage("CrossNetShare 服务器",
                               "程序已最小化到系统托盘",
                               QSystemTrayIcon::Information,
                               1000);
    } else {
        event->accept();
    }
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        onShowWindow();
    }
}

void MainWindow::onShowWindow() {
    show();
    raise();
    activateWindow();
}

void MainWindow::onQuitApp() {
    if (server_->isRunning()) {
        server_->stop();
    }
    QApplication::quit();
}

void MainWindow::setupUi() {
    setWindowTitle("CrossNetShare 服务器 v1.1.0");
    resize(980, 800);

    // 设置全局字体
    QFont appFont = font();
    appFont.setPointSize(10);
    appFont.setFamily("Microsoft YaHei UI, Segoe UI");
    setFont(appFont);

    QWidget* centralWidget = new QWidget(this);
    centralWidget->setStyleSheet(
        "QGroupBox { font-weight: bold; font-size: 11pt; padding-top: 16px; margin-top: 8px; border: 2px solid #e5e7eb; border-radius: 8px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; background-color: white; }"
    );
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 配置区域
    QGroupBox* configGroup = new QGroupBox("服务器配置", this);
    QVBoxLayout* configMainLayout = new QVBoxLayout(configGroup);
    configMainLayout->setSpacing(12);

    QGridLayout* configGrid = new QGridLayout();
    configGrid->setSpacing(12);
    configGrid->setColumnStretch(1, 1);
    configGrid->setColumnStretch(3, 1);

    QLabel* portLabel = new QLabel("监听端口:");
    portLabel->setStyleSheet("font-weight: normal; font-size: 10pt;");
    configGrid->addWidget(portLabel, 0, 0);

    portSpinBox_ = new QSpinBox();
    portSpinBox_->setRange(1024, 65535);
    portSpinBox_->setValue(8888);
    portSpinBox_->setMinimumWidth(120);
    portSpinBox_->setMinimumHeight(32);
    portSpinBox_->setStyleSheet("QSpinBox { padding: 4px 8px; border: 1px solid #d1d5db; border-radius: 4px; }");
    configGrid->addWidget(portSpinBox_, 0, 1);

    QLabel* webPortLabel = new QLabel("Web 端口:");
    webPortLabel->setStyleSheet("font-weight: normal; font-size: 10pt;");
    configGrid->addWidget(webPortLabel, 0, 2);

    webPortSpinBox_ = new QSpinBox();
    webPortSpinBox_->setRange(1024, 65535);
    webPortSpinBox_->setValue(8080);
    webPortSpinBox_->setMinimumWidth(120);
    webPortSpinBox_->setMinimumHeight(32);
    webPortSpinBox_->setStyleSheet("QSpinBox { padding: 4px 8px; border: 1px solid #d1d5db; border-radius: 4px; }");
    configGrid->addWidget(webPortSpinBox_, 0, 3);

    configMainLayout->addLayout(configGrid);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);

    startStopButton_ = new QPushButton("启动服务器");
    startStopButton_->setStyleSheet(
        "QPushButton { padding: 10px 28px; font-weight: bold; font-size: 11pt; background-color: #2563eb; color: white; border: none; border-radius: 6px; } "
        "QPushButton:hover { background-color: #1d4ed8; } "
        "QPushButton:pressed { background-color: #1e40af; }"
    );
    startStopButton_->setMinimumHeight(42);
    startStopButton_->setMinimumWidth(140);
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    buttonLayout->addWidget(startStopButton_);

    refreshButton_ = new QPushButton("刷新文件索引");
    refreshButton_->setStyleSheet(
        "QPushButton { padding: 10px 24px; font-size: 10pt; background-color: white; border: 1px solid #d1d5db; border-radius: 6px; } "
        "QPushButton:hover { background-color: #f3f4f6; }"
    );
    refreshButton_->setMinimumHeight(42);
    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::onRefreshIndexClicked);
    buttonLayout->addWidget(refreshButton_);

    QPushButton* settingsButton = new QPushButton("设置");
    settingsButton->setStyleSheet(
        "QPushButton { padding: 10px 24px; font-size: 10pt; background-color: white; border: 1px solid #d1d5db; border-radius: 6px; } "
        "QPushButton:hover { background-color: #f3f4f6; }"
    );
    settingsButton->setMinimumHeight(42);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    buttonLayout->addWidget(settingsButton);

    buttonLayout->addStretch();
    configMainLayout->addLayout(buttonLayout);

    mainLayout->addWidget(configGroup);

    // 状态显示
    QGroupBox* statusGroup = new QGroupBox("服务器状态", this);
    QVBoxLayout* statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->setSpacing(8);

    statusLabel_ = new QLabel("服务器已停止");
    statusLabel_->setStyleSheet("font-weight: normal; font-size: 10pt; padding: 8px; background-color: #fef2f2; border-radius: 4px; color: #991b1b;");
    statusLayout->addWidget(statusLabel_);

    // 显示本机IP地址
    QString ipInfo = "可用的网络接口：\n";
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& interface : interfaces) {
        if (interface.flags().testFlag(QNetworkInterface::IsUp) &&
            !interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {

            QList<QNetworkAddressEntry> entries = interface.addressEntries();
            for (const QNetworkAddressEntry& entry : entries) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    ipInfo += "  " + interface.name() + ": " + entry.ip().toString() + "\n";
                }
            }
        }
    }

    QLabel* ipLabel = new QLabel(ipInfo);
    ipLabel->setStyleSheet("font-weight: normal; font-size: 9pt; color: #6b7280; padding: 8px; background-color: #f9fafb; border-radius: 4px;");
    ipLabel->setWordWrap(true);
    statusLayout->addWidget(ipLabel);

    mainLayout->addWidget(statusGroup);

    // 已连接客户端
    QGroupBox* clientGroup = new QGroupBox("已连接客户端", this);
    QVBoxLayout* clientLayout = new QVBoxLayout(clientGroup);

    clientListWidget_ = new QListWidget();
    clientListWidget_->setStyleSheet("QListWidget { font-size: 10pt; border: 1px solid #e5e7eb; border-radius: 4px; } QListWidget::item { padding: 8px; }");
    clientLayout->addWidget(clientListWidget_);

    mainLayout->addWidget(clientGroup);

    // 日志区域
    QGroupBox* logGroup = new QGroupBox("服务器日志", this);
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);

    logTextEdit_ = new QTextEdit();
    logTextEdit_->setReadOnly(true);
    logTextEdit_->setStyleSheet("QTextEdit { font-family: 'Consolas', 'Courier New', monospace; font-size: 9pt; border: 1px solid #e5e7eb; border-radius: 4px; background-color: #f9fafb; }");
    logLayout->addWidget(logTextEdit_);

    mainLayout->addWidget(logGroup);

    // 设置布局比例
    mainLayout->setStretch(0, 0);  // 配置区域固定
    mainLayout->setStretch(1, 0);  // 状态区域固定
    mainLayout->setStretch(2, 1);  // 客户端列表
    mainLayout->setStretch(3, 2);  // 日志区域占更多空间
}

void MainWindow::onStartStopClicked() {
    if (server_->isRunning()) {
        server_->stop();
    } else {
        ServerConfig config;
        config.port = portSpinBox_->value();
        config.uploadEnabled = true;  // 默认启用上传
        config.webEnabled = true;     // 默认启用Web
        config.webPort = webPortSpinBox_->value();

        if (!server_->start(config)) {
            QMessageBox::critical(this, "错误", "服务器启动失败！");
        }
    }
}

void MainWindow::onRefreshIndexClicked() {
    // 请求所有在线客户端刷新文件列表
    if (server_) {
        server_->requestAllClientsRefresh();
        appendLog("已向所有在线客户端发送刷新请求");
    } else {
        appendLog("服务器未运行");
    }
}

void MainWindow::onServerStarted() {
    updateServerStatus();
    portSpinBox_->setEnabled(false);
    webPortSpinBox_->setEnabled(false);
}

void MainWindow::onServerStopped() {
    updateServerStatus();
    portSpinBox_->setEnabled(true);
    webPortSpinBox_->setEnabled(true);
    clientListWidget_->clear();
}

void MainWindow::onClientConnected(const QString& clientId, const QString& address) {
    // 先移除已存在的同名客户端（避免重复）
    for (int i = 0; i < clientListWidget_->count(); ++i) {
        QListWidgetItem* item = clientListWidget_->item(i);
        if (item->text().startsWith(clientId + " ")) {
            delete clientListWidget_->takeItem(i);
            break;
        }
    }

    // 添加新条目
    QString displayText = clientId + " (" + address + ")";
    clientListWidget_->addItem(displayText);
}

void MainWindow::onClientDisconnected(const QString& clientId) {
    for (int i = 0; i < clientListWidget_->count(); ++i) {
        QListWidgetItem* item = clientListWidget_->item(i);
        if (item->text().startsWith(clientId + " ")) {
            delete clientListWidget_->takeItem(i);
            break;
        }
    }
}

void MainWindow::onLogMessage(const QString& message) {
    appendLog(message);
}

void MainWindow::onServerError(const QString& errorMsg) {
    appendLog("错误: " + errorMsg);
}

void MainWindow::updateServerStatus() {
    if (server_->isRunning()) {
        startStopButton_->setText("停止服务器");
        startStopButton_->setStyleSheet(
            "QPushButton { padding: 10px 28px; font-weight: bold; font-size: 11pt; background-color: #dc2626; color: white; border: none; border-radius: 6px; } "
            "QPushButton:hover { background-color: #b91c1c; } "
            "QPushButton:pressed { background-color: #991b1b; }"
        );

        ServerConfig config = server_->getConfig();
        QString status = "服务器运行中 - 端口 " + QString::number(config.port) +
                        " (上传 " + (config.uploadEnabled ? "已启用" : "已禁用") + ")";
        if (config.webEnabled) {
            status += "\nWeb 访问: http://0.0.0.0:" + QString::number(config.webPort) + " (用户名/密码: admin/admin123)";
        }
        statusLabel_->setText(status);
        statusLabel_->setStyleSheet("font-weight: normal; font-size: 10pt; padding: 8px; background-color: #dcfce7; border-radius: 4px; color: #166534;");
    } else {
        startStopButton_->setText("启动服务器");
        startStopButton_->setStyleSheet(
            "QPushButton { padding: 10px 28px; font-weight: bold; font-size: 11pt; background-color: #2563eb; color: white; border: none; border-radius: 6px; } "
            "QPushButton:hover { background-color: #1d4ed8; } "
            "QPushButton:pressed { background-color: #1e40af; }"
        );
        statusLabel_->setText("服务器已停止");
        statusLabel_->setStyleSheet("font-weight: normal; font-size: 10pt; padding: 8px; background-color: #fef2f2; border-radius: 4px; color: #991b1b;");
    }
}

void MainWindow::appendLog(const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    logTextEdit_->append("[" + timestamp + "] " + message);

    // 自动滚动到底部
    logTextEdit_->moveCursor(QTextCursor::End);
}

void MainWindow::onCleanupCache() {
    DocumentConverter::cleanupCache();
    appendLog("已清理旧的预览缓存文件");
}

void MainWindow::onSettingsClicked() {
    SettingsDialog dialog(this);
    dialog.exec();
}

}
