#pragma once

#include "../client.h"
#include "../file_manager.h"
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

private:
    void setupUi();
    void setupTrayIcon();
    void updateConnectionStatus();
    void appendLog(const QString& message);
    void updateFileTree(const std::vector<FileMetadata>& files);

    Client* client_;
    FileManager* fileManager_;

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
};

}
