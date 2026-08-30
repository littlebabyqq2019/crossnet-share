#pragma once

#include "../client.h"
#include "../file_manager.h"
#include "../file_indexer.h"
#include <QMainWindow>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QDateEdit>
#include <QTreeWidget>
#include <QProgressBar>
#include <QComboBox>
#include <QCheckBox>
#include <QSystemTrayIcon>
#include <QMenu>

namespace CrossNetShare {

class IndexSettingsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // 连接相关
    void onConnectClicked();
    void onRegisterClicked();
    void onClientConnected();
    void onClientDisconnected();
    void onClientRegistered(bool success, const QString& message);

    // 文件操作
    void onBrowseSharePathClicked();
    void onBrowseSavePathClicked();
    void onRefreshFilesClicked();
    void onDownloadSelectedClicked();
    void onUploadFileClicked();

    // 文件列表
    void onFileListReceived(const std::vector<FileMetadata>& files);

    // 下载进度
    void onDownloadStarted();
    void onDownloadProgress(int current, int total);
    void onDownloadFinished(int successCount, int totalCount);

    // 上传进度
    void onUploadStarted();
    void onUploadFinished(bool success);

    // 日志
    void onLogMessage(const QString& message);
    void onError(const QString& errorMsg);
    void onAutoStartChanged(int state);
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onShowWindow();
    void onQuitApp();
    
    // 索引相关
    void onIndexSettingsClicked();
    void onSearchTextChanged(const QString& text);
    void onContentSearchClicked();

private:
    void setupUi();
    void setupMenuBar();
    void setupTrayIcon();
    void initializeIndexer();
    void updateConnectionStatus();
    void appendLog(const QString& message);
    void updateFileTree(const std::vector<FileMetadata>& files);
    void performContentSearch(const QString& query);

    Client* client_;
    FileManager* fileManager_;
    FileIndexer* indexer_;

    // UI控件 - 连接区域
    QLineEdit* serverAddressEdit_;
    QSpinBox* serverPortSpinBox_;
    QPushButton* connectButton_;
    QLabel* connectionStatusLabel_;
    QCheckBox* autoStartCheckBox_;

    // UI控件 - 注册区域
    QLineEdit* clientIdEdit_;
    QLineEdit* sharePathEdit_;
    QPushButton* browseSharePathButton_;
    QPushButton* registerButton_;

    // UI控件 - 文件浏览区域
    QComboBox* targetClientCombo_;
    QDateEdit* startDateEdit_;
    QDateEdit* endDateEdit_;
    QPushButton* refreshButton_;
    QTreeWidget* fileTreeWidget_;
    
    // UI控件 - 搜索区域
    QLineEdit* searchEdit_;
    QPushButton* searchButton_;
    QLabel* searchResultLabel_;

    // UI控件 - 下载区域
    QLineEdit* savePathEdit_;
    QPushButton* browseSavePathButton_;
    QPushButton* downloadButton_;
    QProgressBar* downloadProgressBar_;
    QLabel* downloadStatusLabel_;

    // UI控件 - 上传区域
    QPushButton* uploadButton_;
    QLabel* uploadStatusLabel_;

    // UI控件 - 日志区域
    QTextEdit* logTextEdit_;

    // 系统托盘
    QSystemTrayIcon* trayIcon_;
    QMenu* trayMenu_;
    
    // 菜单
    QAction* indexSettingsAction_;
};

}
