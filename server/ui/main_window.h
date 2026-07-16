#pragma once

#include "../server.h"
#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>

namespace CrossNetShare {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onStartStopClicked();
    void onRefreshIndexClicked();
    void onServerStarted();
    void onServerStopped();
    void onClientConnected(const QString& clientId, const QString& address);
    void onClientDisconnected(const QString& clientId);
    void onLogMessage(const QString& message);
    void onServerError(const QString& errorMsg);

private:
    void setupUi();
    void updateServerStatus();
    void appendLog(const QString& message);

    Server* server_;

    // UI控件
    QSpinBox* portSpinBox_;
    QCheckBox* uploadEnabledCheckBox_;
    QCheckBox* webEnabledCheckBox_;
    QSpinBox* webPortSpinBox_;
    QPushButton* startStopButton_;
    QPushButton* refreshButton_;
    QLabel* statusLabel_;
    QListWidget* clientListWidget_;
    QTextEdit* logTextEdit_;
};

}
