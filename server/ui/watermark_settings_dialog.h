#pragma once

#include "../watermark_service.h"
#include <QDialog>
#include <QTableWidget>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>

namespace CrossNetShare {

class WatermarkSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit WatermarkSettingsDialog(WatermarkService* service, QWidget* parent = nullptr);
    ~WatermarkSettingsDialog();

private slots:
    void onAddKeywordClicked();
    void onEditKeywordClicked();
    void onDeleteKeywordClicked();
    void onSaveClicked();
    void onCancelClicked();
    void onFontSizeChanged(int value);
    void onOpacityChanged(int value);
    void onRotationChanged(int value);
    void onColorButtonClicked();
    void onEnableWatermarkToggled(bool checked);

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void updateKeywordsTable();

    WatermarkService* watermarkService_;

    // 全局启用
    QCheckBox* enableWatermarkCheckBox_;

    // 关键词管理
    QTableWidget* keywordsTable_;
    QPushButton* addButton_;
    QPushButton* editButton_;
    QPushButton* deleteButton_;

    // 统一水印
    QCheckBox* enableUnifiedCheckBox_;
    QLineEdit* unifiedTextEdit_;

    // 水印样式
    QSpinBox* fontSizeSpinBox_;
    QPushButton* colorButton_;
    QSlider* opacitySlider_;
    QLabel* opacityLabel_;
    QSlider* rotationSlider_;
    QLabel* rotationLabel_;
    QComboBox* densityComboBox_;

    // 对话框按钮
    QPushButton* saveButton_;
    QPushButton* cancelButton_;

    // 当前颜色
    QColor currentColor_;
};

}
