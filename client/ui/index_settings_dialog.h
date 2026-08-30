#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include "../file_indexer.h"

namespace CrossNetShare {

class IndexSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit IndexSettingsDialog(FileIndexer* indexer, QWidget* parent = nullptr);
    ~IndexSettingsDialog();

    // 获取/设置配置
    IndexConfig getConfig() const;
    void setConfig(const IndexConfig& config);

private slots:
    void onRebuildClicked();
    void onClearClicked();
    void onSaveClicked();
    void onIndexingStarted();
    void onIndexingProgress(int current, int total);
    void onIndexingFinished();
    void updateStats();

private:
    void setupUi();
    void connectSignals();
    void loadConfig();

private:
    FileIndexer* indexer_;
    
    // 配置控件
    QCheckBox* enabledCheck_;
    QCheckBox* txtCheck_;
    QCheckBox* pdfCheck_;
    QCheckBox* docCheck_;
    QCheckBox* docxCheck_;
    QLineEdit* excludeEdit_;
    QRadioButton* realtimeRadio_;
    QRadioButton* scheduledRadio_;
    QRadioButton* manualRadio_;
    QSpinBox* intervalSpin_;
    
    // 统计信息
    QLabel* filesLabel_;
    QLabel* sizeLabel_;
    QLabel* timeLabel_;
    QLabel* pendingLabel_;
    
    // 操作按钮
    QPushButton* rebuildButton_;
    QPushButton* clearButton_;
    QPushButton* saveButton_;
    QPushButton* closeButton_;
    
    // 进度显示
    QProgressBar* progressBar_;
    QTextEdit* logText_;
};

} // namespace CrossNetShare
