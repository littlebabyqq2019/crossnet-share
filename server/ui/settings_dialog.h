#pragma once

#include "../user_manager.h"
#include "../watermark_service.h"
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QTabWidget>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QLabel>

namespace CrossNetShare {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(WatermarkService* watermarkService, QWidget* parent = nullptr);

    // 获取设置
    bool getUploadEnabled() const;
    bool getWebEnabled() const;
    bool getAutoStartEnabled() const;

    // 保存水印设置（公开方法）
    void saveWatermarkSettings();

signals:
    void settingsChanged();

private slots:
    void onAddUserClicked();
    void onRemoveUserClicked();
    void onChangePasswordClicked();
    void onRefreshUsersClicked();
    void onUserManagementClicked();
    void onAuditLogClicked();

    // 水印设置槽
    void onAddKeywordClicked();
    void onEditKeywordClicked();
    void onDeleteKeywordClicked();
    void onFontSizeChanged(int value);
    void onOpacityChanged(int value);
    void onRotationChanged(int value);
    void onColorButtonClicked();
    void onEnableWatermarkToggled(bool checked);
    void onDetectLibreOfficeClicked();
    void onSaveClicked();

private:
    void setupUi();
    void loadUsers();
    void loadWatermarkSettings();
    void updateKeywordsTable();
    void updateColorButton();

    QTabWidget* tabWidget_;

    // 用户管理标签页组件
    QTableWidget* userTable_;
    QPushButton* addUserButton_;
    QPushButton* removeUserButton_;
    QPushButton* changePasswordButton_;
    QPushButton* refreshButton_;

    // 服务器选项
    QCheckBox* uploadEnabledCheckBox_;
    QCheckBox* webEnabledCheckBox_;
    QCheckBox* autoStartCheckBox_;

    // 水印设置标签页组件
    WatermarkService* watermarkService_;
    QCheckBox* enableWatermarkCheckBox_;
    QTableWidget* keywordsTable_;
    QPushButton* addKeywordButton_;
    QPushButton* editKeywordButton_;
    QPushButton* deleteKeywordButton_;
    QCheckBox* enableUnifiedCheckBox_;
    QLineEdit* unifiedTextEdit_;
    QSpinBox* fontSizeSpinBox_;
    QPushButton* colorButton_;
    QSlider* opacitySlider_;
    QLabel* opacityLabel_;
    QSlider* rotationSlider_;
    QLabel* rotationLabel_;
    QComboBox* densityComboBox_;
    QPushButton* detectLibreOfficeButton_;
    QColor currentColor_;
};

}
