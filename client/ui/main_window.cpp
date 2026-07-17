#include "main_window.h"
#include "common/autostart.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QDateTime>
#include <QMessageBox>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>

namespace CrossNetShare {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , client_(new Client(this))
    , fileManager_(new FileManager(client_, this))
{
    setupUi();

    // 连接信号
    connect(client_, &Client::connected, this, &MainWindow::onClientConnected);
    connect(client_, &Client::disconnected, this, &MainWindow::onClientDisconnected);
    connect(client_, &Client::registered, this, &MainWindow::onClientRegistered);
    connect(client_, &Client::fileListReceived, this, &MainWindow::onFileListReceived);
    connect(client_, &Client::logMessage, this, &MainWindow::onLogMessage);
    connect(client_, &Client::error, this, &MainWindow::onError);

    connect(fileManager_, &FileManager::downloadStarted, this, &MainWindow::onDownloadStarted);
    connect(fileManager_, &FileManager::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(fileManager_, &FileManager::downloadFinished, this, &MainWindow::onDownloadFinished);
    connect(fileManager_, &FileManager::uploadStarted, this, &MainWindow::onUploadStarted);
    connect(fileManager_, &FileManager::uploadFinished, this, &MainWindow::onUploadFinished);

    // 启用自动重连
    client_->enableAutoReconnect(true);

    // 加载配置
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/crossnet_client_config.json";
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));

    if (client_->loadConfig(configPath)) {
        onLogMessage("Configuration loaded successfully");

        // 自动连接到服务器
        QString host = client_->getServerHost();
        quint16 port = client_->getServerPort();
        QString clientId = client_->getClientId();
        QString sharePath = client_->getSharePath();

        if (!host.isEmpty() && port > 0 && !clientId.isEmpty() && !sharePath.isEmpty()) {
            onLogMessage("Auto-connecting to " + host + ":" + QString::number(port));

            // 更新UI显示
            serverAddressEdit_->setText(host);
            serverPortSpinBox_->setValue(port);
            clientIdEdit_->setText(clientId);
            sharePathEdit_->setText(sharePath);

            // 连接
            if (client_->connectToServer(host, port)) {
                onLogMessage("Auto-connect initiated, will auto-register after connection");
            }
        }
    }

    updateConnectionStatus();
}

MainWindow::~MainWindow() {
    // 保存配置
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/crossnet_client_config.json";
    client_->saveConfig(configPath);
}

void MainWindow::setupUi() {
    setWindowTitle("CrossNetShare Client");
    resize(900, 700);

    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);

    // 连接配置区域
    QGroupBox* connectionGroup = new QGroupBox("Server Connection", this);
    QHBoxLayout* connectionLayout = new QHBoxLayout(connectionGroup);

    connectionLayout->addWidget(new QLabel("Server:"));
    serverAddressEdit_ = new QLineEdit("127.0.0.1");
    connectionLayout->addWidget(serverAddressEdit_);

    connectionLayout->addWidget(new QLabel("Port:"));
    serverPortSpinBox_ = new QSpinBox();
    serverPortSpinBox_->setRange(1024, 65535);
    serverPortSpinBox_->setValue(8888);
    connectionLayout->addWidget(serverPortSpinBox_);

    connectButton_ = new QPushButton("Connect");
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connectionLayout->addWidget(connectButton_);

    connectionStatusLabel_ = new QLabel("Disconnected");
    connectionLayout->addWidget(connectionStatusLabel_);

    autoStartCheckBox_ = new QCheckBox("Start on Windows startup");
    autoStartCheckBox_->setChecked(AutoStart::isAutoStartEnabled("CrossNetShareClient"));
    connect(autoStartCheckBox_, &QCheckBox::stateChanged, this, &MainWindow::onAutoStartChanged);
    connectionLayout->addWidget(autoStartCheckBox_);

    connectionLayout->addStretch();

    mainLayout->addWidget(connectionGroup);

    // 客户端注册区域
    QGroupBox* registerGroup = new QGroupBox("Client Registration", this);
    QHBoxLayout* registerLayout = new QHBoxLayout(registerGroup);

    registerLayout->addWidget(new QLabel("Client ID:"));
    clientIdEdit_ = new QLineEdit();
    clientIdEdit_->setPlaceholderText("e.g., ClientB or ClientC");
    registerLayout->addWidget(clientIdEdit_);

    registerLayout->addWidget(new QLabel("Share Path:"));
    sharePathEdit_ = new QLineEdit();
    sharePathEdit_->setPlaceholderText("Select a folder to share");
    registerLayout->addWidget(sharePathEdit_);

    browseSharePathButton_ = new QPushButton("Browse...");
    connect(browseSharePathButton_, &QPushButton::clicked, this, &MainWindow::onBrowseSharePathClicked);
    registerLayout->addWidget(browseSharePathButton_);

    registerButton_ = new QPushButton("Register");
    registerButton_->setEnabled(false);
    connect(registerButton_, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
    registerLayout->addWidget(registerButton_);

    mainLayout->addWidget(registerGroup);

    // 文件浏览区域
    QGroupBox* browseGroup = new QGroupBox("Browse Files", this);
    QVBoxLayout* browseLayout = new QVBoxLayout(browseGroup);

    // 筛选控件
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->addWidget(new QLabel("Target Client:"));
    targetClientCombo_ = new QComboBox();
    targetClientCombo_->addItem("All Clients", "");
    filterLayout->addWidget(targetClientCombo_);

    filterLayout->addWidget(new QLabel("From:"));
    startDateEdit_ = new QDateEdit();
    startDateEdit_->setCalendarPopup(true);
    startDateEdit_->setDate(QDate::currentDate().addMonths(-1));
    filterLayout->addWidget(startDateEdit_);

    filterLayout->addWidget(new QLabel("To:"));
    endDateEdit_ = new QDateEdit();
    endDateEdit_->setCalendarPopup(true);
    endDateEdit_->setDate(QDate::currentDate());
    filterLayout->addWidget(endDateEdit_);

    refreshButton_ = new QPushButton("Refresh");
    refreshButton_->setEnabled(false);
    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::onRefreshFilesClicked);
    filterLayout->addWidget(refreshButton_);

    filterLayout->addStretch();
    browseLayout->addLayout(filterLayout);

    // 文件树
    fileTreeWidget_ = new QTreeWidget();
    fileTreeWidget_->setHeaderLabels({"Filename", "Size", "Create Time", "Owner"});
    fileTreeWidget_->setSelectionMode(QAbstractItemView::MultiSelection);
    fileTreeWidget_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    browseLayout->addWidget(fileTreeWidget_);

    mainLayout->addWidget(browseGroup);

    // 下载区域
    QGroupBox* downloadGroup = new QGroupBox("Download", this);
    QVBoxLayout* downloadLayout = new QVBoxLayout(downloadGroup);

    QHBoxLayout* downloadPathLayout = new QHBoxLayout();
    downloadPathLayout->addWidget(new QLabel("Save to:"));
    savePathEdit_ = new QLineEdit();
    downloadPathLayout->addWidget(savePathEdit_);

    browseSavePathButton_ = new QPushButton("Browse...");
    connect(browseSavePathButton_, &QPushButton::clicked, this, &MainWindow::onBrowseSavePathClicked);
    downloadPathLayout->addWidget(browseSavePathButton_);

    downloadButton_ = new QPushButton("Download Selected");
    downloadButton_->setEnabled(false);
    connect(downloadButton_, &QPushButton::clicked, this, &MainWindow::onDownloadSelectedClicked);
    downloadPathLayout->addWidget(downloadButton_);

    downloadLayout->addLayout(downloadPathLayout);

    downloadProgressBar_ = new QProgressBar();
    downloadProgressBar_->setVisible(false);
    downloadLayout->addWidget(downloadProgressBar_);

    downloadStatusLabel_ = new QLabel();
    downloadLayout->addWidget(downloadStatusLabel_);

    mainLayout->addWidget(downloadGroup);

    // 上传区域
    QGroupBox* uploadGroup = new QGroupBox("Upload", this);
    QHBoxLayout* uploadLayout = new QHBoxLayout(uploadGroup);

    uploadButton_ = new QPushButton("Upload File...");
    uploadButton_->setEnabled(false);
    connect(uploadButton_, &QPushButton::clicked, this, &MainWindow::onUploadFileClicked);
    uploadLayout->addWidget(uploadButton_);

    uploadStatusLabel_ = new QLabel();
    uploadLayout->addWidget(uploadStatusLabel_);

    uploadLayout->addStretch();

    mainLayout->addWidget(uploadGroup);

    // 日志区域
    QGroupBox* logGroup = new QGroupBox("Log", this);
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);

    logTextEdit_ = new QTextEdit();
    logTextEdit_->setReadOnly(true);
    logTextEdit_->setMaximumHeight(150);
    logLayout->addWidget(logTextEdit_);

    mainLayout->addWidget(logGroup);

    // 设置布局比例
    mainLayout->setStretch(0, 0);  // 连接区域
    mainLayout->setStretch(1, 0);  // 注册区域
    mainLayout->setStretch(2, 3);  // 文件浏览区域
    mainLayout->setStretch(3, 0);  // 下载区域
    mainLayout->setStretch(4, 0);  // 上传区域
    mainLayout->setStretch(5, 1);  // 日志区域
}

void MainWindow::onConnectClicked() {
    if (client_->isConnected()) {
        client_->disconnectFromServer();
    } else {
        QString host = serverAddressEdit_->text();
        quint16 port = serverPortSpinBox_->value();

        if (!client_->connectToServer(host, port)) {
            QMessageBox::critical(this, "Error", "Failed to connect to server");
        }
    }
}

void MainWindow::onRegisterClicked() {
    QString clientId = clientIdEdit_->text().trimmed();
    QString sharePath = sharePathEdit_->text().trimmed();

    if (clientId.isEmpty() || sharePath.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please fill in all fields");
        return;
    }

    client_->registerClient(clientId, sharePath);
}

void MainWindow::onClientConnected() {
    updateConnectionStatus();
    registerButton_->setEnabled(true);

    // 如果有保存的配置，自动注册
    QString clientId = client_->getClientId();
    QString sharePath = client_->getSharePath();

    if (!clientId.isEmpty() && !sharePath.isEmpty()) {
        onLogMessage("Auto-registering with clientId: " + clientId);
        client_->registerClient(clientId, sharePath);
    }
}

void MainWindow::onClientDisconnected() {
    updateConnectionStatus();
    registerButton_->setEnabled(false);
    refreshButton_->setEnabled(false);
    downloadButton_->setEnabled(false);
    uploadButton_->setEnabled(false);
    fileTreeWidget_->clear();
}

void MainWindow::onClientRegistered(bool success, const QString& message) {
    if (success) {
        refreshButton_->setEnabled(true);
        downloadButton_->setEnabled(true);
        uploadButton_->setEnabled(true);

        QMessageBox::information(this, "Success", "Registration successful!");
    } else {
        QMessageBox::critical(this, "Error", "Registration failed: " + message);
    }
}

void MainWindow::onBrowseSharePathClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Share Directory");
    if (!dir.isEmpty()) {
        sharePathEdit_->setText(dir);
    }
}

void MainWindow::onBrowseSavePathClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Save Directory");
    if (!dir.isEmpty()) {
        savePathEdit_->setText(dir);
        fileManager_->setSaveDirectory(dir);
    }
}

void MainWindow::onRefreshFilesClicked() {
    DateFilter filter;
    filter.startDate = startDateEdit_->dateTime().toSecsSinceEpoch();
    filter.endDate = endDateEdit_->dateTime().addDays(1).toSecsSinceEpoch() - 1;

    QString targetClient = targetClientCombo_->currentData().toString();

    client_->requestFilteredFiles(filter, targetClient);
}

void MainWindow::onDownloadSelectedClicked() {
    if (savePathEdit_->text().isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a save directory first");
        return;
    }

    DateFilter filter;
    filter.startDate = startDateEdit_->dateTime().toSecsSinceEpoch();
    filter.endDate = endDateEdit_->dateTime().addDays(1).toSecsSinceEpoch() - 1;

    QString targetClient = targetClientCombo_->currentData().toString();

    fileManager_->downloadFilteredFiles(filter, targetClient);
}

void MainWindow::onUploadFileClicked() {
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to Upload");
    if (!filePath.isEmpty()) {
        fileManager_->uploadFile(filePath);
    }
}

void MainWindow::onFileListReceived(const std::vector<FileMetadata>& files) {
    updateFileTree(files);
}

void MainWindow::onDownloadStarted() {
    downloadProgressBar_->setVisible(true);
    downloadProgressBar_->setValue(0);
    downloadStatusLabel_->setText("Downloading...");
    downloadButton_->setEnabled(false);
}

void MainWindow::onDownloadProgress(int current, int total) {
    downloadProgressBar_->setMaximum(total);
    downloadProgressBar_->setValue(current);
    downloadStatusLabel_->setText(QString("Downloading: %1 / %2 files").arg(current).arg(total));
}

void MainWindow::onDownloadFinished(int successCount, int totalCount) {
    downloadProgressBar_->setVisible(false);
    downloadStatusLabel_->setText(QString("Download complete: %1 / %2 files").arg(successCount).arg(totalCount));
    downloadButton_->setEnabled(true);

    QMessageBox::information(this, "Download Complete",
                            QString("Successfully downloaded %1 out of %2 files")
                            .arg(successCount).arg(totalCount));
}

void MainWindow::onUploadStarted() {
    uploadStatusLabel_->setText("Uploading...");
    uploadButton_->setEnabled(false);
}

void MainWindow::onUploadFinished(bool success) {
    if (success) {
        uploadStatusLabel_->setText("Upload complete");
        QMessageBox::information(this, "Success", "File uploaded successfully!");
    } else {
        uploadStatusLabel_->setText("Upload failed");
    }
    uploadButton_->setEnabled(true);
}

void MainWindow::onLogMessage(const QString& message) {
    appendLog(message);
}

void MainWindow::onError(const QString& errorMsg) {
    appendLog("ERROR: " + errorMsg);
}

void MainWindow::updateConnectionStatus() {
    if (client_->isConnected()) {
        connectButton_->setText("Disconnect");
        connectionStatusLabel_->setText("Connected");
        connectionStatusLabel_->setStyleSheet("QLabel { color: green; font-weight: bold; }");
        serverAddressEdit_->setEnabled(false);
        serverPortSpinBox_->setEnabled(false);
    } else {
        connectButton_->setText("Connect");
        connectionStatusLabel_->setText("Disconnected");
        connectionStatusLabel_->setStyleSheet("QLabel { color: red; }");
        serverAddressEdit_->setEnabled(true);
        serverPortSpinBox_->setEnabled(true);
    }
}

void MainWindow::appendLog(const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    logTextEdit_->append("[" + timestamp + "] " + message);
    logTextEdit_->moveCursor(QTextCursor::End);
}

void MainWindow::updateFileTree(const std::vector<FileMetadata>& files) {
    fileTreeWidget_->clear();

    // 更新目标客户端下拉框
    QSet<QString> clients;
    for (const auto& file : files) {
        clients.insert(QString::fromStdString(file.ownerClient));
    }

    targetClientCombo_->clear();
    targetClientCombo_->addItem("All Clients", "");
    for (const QString& client : clients) {
        targetClientCombo_->addItem(client, client);
    }

    // 添加文件到树
    for (const auto& file : files) {
        QTreeWidgetItem* item = new QTreeWidgetItem(fileTreeWidget_);
        item->setText(0, QString::fromStdString(file.filename));

        // 格式化文件大小
        double size = file.size;
        QString sizeStr;
        if (size < 1024) {
            sizeStr = QString::number(size) + " B";
        } else if (size < 1024 * 1024) {
            sizeStr = QString::number(size / 1024.0, 'f', 2) + " KB";
        } else {
            sizeStr = QString::number(size / (1024.0 * 1024.0), 'f', 2) + " MB";
        }
        item->setText(1, sizeStr);

        // 格式化创建时间
        QDateTime createTime = QDateTime::fromSecsSinceEpoch(file.createTime);
        item->setText(2, createTime.toString("yyyy-MM-dd HH:mm:ss"));

        item->setText(3, QString::fromStdString(file.ownerClient));

        fileTreeWidget_->addTopLevelItem(item);
    }

    appendLog("Received " + QString::number(files.size()) + " files");
}

void MainWindow::onAutoStartChanged(int state) {
    bool enable = (state == Qt::Checked);
    QString appPath = QCoreApplication::applicationFilePath();

    if (AutoStart::setAutoStart("CrossNetShareClient", appPath, enable)) {
        appendLog(enable ? "Auto-start enabled" : "Auto-start disabled");
    } else {
        appendLog("Failed to change auto-start setting");
        autoStartCheckBox_->setChecked(!enable);
    }
}

}
