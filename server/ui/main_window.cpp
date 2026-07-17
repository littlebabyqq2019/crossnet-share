#include "main_window.h"
#include "../document_converter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QMessageBox>
#include <QNetworkInterface>

namespace CrossNetShare {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , server_(new Server(this))
    , cacheCleanupTimer_(new QTimer(this))
{
    setupUi();

    connect(server_, &Server::started, this, &MainWindow::onServerStarted);
    connect(server_, &Server::stopped, this, &MainWindow::onServerStopped);
    connect(server_, &Server::clientConnected, this, &MainWindow::onClientConnected);
    connect(server_, &Server::clientDisconnected, this, &MainWindow::onClientDisconnected);
    connect(server_, &Server::logMessage, this, &MainWindow::onLogMessage);
    connect(server_, &Server::error, this, &MainWindow::onServerError);

    connect(cacheCleanupTimer_, &QTimer::timeout, this, &MainWindow::onCleanupCache);
    cacheCleanupTimer_->start(3600000);

    updateServerStatus();
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUi() {
    setWindowTitle("CrossNetShare Server");
    resize(800, 600);

    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);

    // 服务器配置区域
    QGroupBox* configGroup = new QGroupBox("Server Configuration", this);
    QHBoxLayout* configLayout = new QHBoxLayout(configGroup);

    configLayout->addWidget(new QLabel("Port:"));
    portSpinBox_ = new QSpinBox();
    portSpinBox_->setRange(1024, 65535);
    portSpinBox_->setValue(8888);
    configLayout->addWidget(portSpinBox_);

    uploadEnabledCheckBox_ = new QCheckBox("Enable Upload");
    uploadEnabledCheckBox_->setChecked(true);
    configLayout->addWidget(uploadEnabledCheckBox_);

    webEnabledCheckBox_ = new QCheckBox("Enable Web");
    webEnabledCheckBox_->setChecked(true);
    configLayout->addWidget(webEnabledCheckBox_);

    configLayout->addWidget(new QLabel("Web Port:"));
    webPortSpinBox_ = new QSpinBox();
    webPortSpinBox_->setRange(1024, 65535);
    webPortSpinBox_->setValue(8080);
    configLayout->addWidget(webPortSpinBox_);

    startStopButton_ = new QPushButton("Start Server");
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    configLayout->addWidget(startStopButton_);

    refreshButton_ = new QPushButton("Refresh Index");
    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::onRefreshIndexClicked);
    configLayout->addWidget(refreshButton_);

    configLayout->addStretch();

    mainLayout->addWidget(configGroup);

    // 状态显示
    QGroupBox* statusGroup = new QGroupBox("Status", this);
    QVBoxLayout* statusLayout = new QVBoxLayout(statusGroup);

    statusLabel_ = new QLabel("Server is stopped");
    statusLayout->addWidget(statusLabel_);

    // 显示本机IP地址
    QString ipInfo = "Available network interfaces:\n";
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
    ipLabel->setStyleSheet("QLabel { color: gray; font-size: 9pt; }");
    statusLayout->addWidget(ipLabel);

    mainLayout->addWidget(statusGroup);

    // 客户端列表
    QGroupBox* clientGroup = new QGroupBox("Connected Clients", this);
    QVBoxLayout* clientLayout = new QVBoxLayout(clientGroup);

    clientListWidget_ = new QListWidget();
    clientLayout->addWidget(clientListWidget_);

    mainLayout->addWidget(clientGroup);

    // 日志区域
    QGroupBox* logGroup = new QGroupBox("Server Log", this);
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);

    logTextEdit_ = new QTextEdit();
    logTextEdit_->setReadOnly(true);
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
        config.uploadEnabled = uploadEnabledCheckBox_->isChecked();
        config.webEnabled = webEnabledCheckBox_->isChecked();
        config.webPort = webPortSpinBox_->value();

        if (!server_->start(config)) {
            QMessageBox::critical(this, "Error", "Failed to start server");
        }
    }
}

void MainWindow::onRefreshIndexClicked() {
    if (server_->isRunning()) {
        server_->getIndexer()->refreshAll();
        appendLog("File index refreshed");
    } else {
        QMessageBox::information(this, "Info", "Server is not running");
    }
}

void MainWindow::onServerStarted() {
    updateServerStatus();
    portSpinBox_->setEnabled(false);
    webPortSpinBox_->setEnabled(false);
    webEnabledCheckBox_->setEnabled(false);
}

void MainWindow::onServerStopped() {
    updateServerStatus();
    portSpinBox_->setEnabled(true);
    webPortSpinBox_->setEnabled(true);
    webEnabledCheckBox_->setEnabled(true);
    clientListWidget_->clear();
}

void MainWindow::onClientConnected(const QString& clientId, const QString& address) {
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
    appendLog("ERROR: " + errorMsg);
}

void MainWindow::updateServerStatus() {
    if (server_->isRunning()) {
        startStopButton_->setText("Stop Server");
        ServerConfig config = server_->getConfig();
        QString status = "Server is running on port " + QString::number(config.port) +
                        " (Upload " + (config.uploadEnabled ? "Enabled" : "Disabled") + ")";
        if (config.webEnabled) {
            status += "\nWeb: http://0.0.0.0:" + QString::number(config.webPort) + " (admin/admin123)";
        }
        statusLabel_->setText(status);
        statusLabel_->setStyleSheet("QLabel { color: green; font-weight: bold; }");
    } else {
        startStopButton_->setText("Start Server");
        statusLabel_->setText("Server is stopped");
        statusLabel_->setStyleSheet("QLabel { color: red; }");
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
    appendLog("Cleaned up old preview cache files");
}

}
