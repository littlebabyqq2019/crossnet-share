#include "index_settings_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QTimer>

namespace CrossNetShare {

IndexSettingsDialog::IndexSettingsDialog(FileIndexer* indexer, QWidget* parent)
    : QDialog(parent)
    , indexer_(indexer)
{
    setWindowTitle("全文索引设置");
    resize(600, 700);
    
    setupUi();
    connectSignals();
    loadConfig();
    updateStats();
    
    // 定时更新统计信息
    QTimer* statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, &IndexSettingsDialog::updateStats);
    statsTimer->start(2000);  // 每2秒更新一次
}

IndexSettingsDialog::~IndexSettingsDialog() {
}

void IndexSettingsDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 启用索引
    enabledCheck_ = new QCheckBox("启用全文索引");
    mainLayout->addWidget(enabledCheck_);
    
    // 文件类型选择
    QGroupBox* typesGroup = new QGroupBox("索引文件类型");
    QVBoxLayout* typesLayout = new QVBoxLayout(typesGroup);
    
    QHBoxLayout* row1 = new QHBoxLayout();
    txtCheck_ = new QCheckBox(".txt");
    pdfCheck_ = new QCheckBox(".pdf");
    row1->addWidget(txtCheck_);
    row1->addWidget(pdfCheck_);
    row1->addStretch();
    
    QHBoxLayout* row2 = new QHBoxLayout();
    docCheck_ = new QCheckBox(".doc");
    docxCheck_ = new QCheckBox(".docx");
    row2->addWidget(docCheck_);
    row2->addWidget(docxCheck_);
    row2->addStretch();
    
    typesLayout->addLayout(row1);
    typesLayout->addLayout(row2);
    mainLayout->addWidget(typesGroup);
    
    // 排除规则
    QGroupBox* excludeGroup = new QGroupBox("排除规则");
    QVBoxLayout* excludeLayout = new QVBoxLayout(excludeGroup);
    excludeEdit_ = new QLineEdit();
    excludeEdit_->setPlaceholderText("例如: ~$*, *.tmp, temp/*");
    QLabel* excludeHint = new QLabel("用逗号分隔多个规则，支持通配符 * 和 ?");
    excludeHint->setStyleSheet("color: gray; font-size: 10px;");
    excludeLayout->addWidget(excludeEdit_);
    excludeLayout->addWidget(excludeHint);
    mainLayout->addWidget(excludeGroup);
    
    // 更新策略
    QGroupBox* updateGroup = new QGroupBox("更新策略");
    QVBoxLayout* updateLayout = new QVBoxLayout(updateGroup);
    
    realtimeRadio_ = new QRadioButton("实时监控（推荐）");
    scheduledRadio_ = new QRadioButton("定时扫描");
    manualRadio_ = new QRadioButton("仅手动更新");
    
    QHBoxLayout* intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel("扫描间隔："));
    intervalSpin_ = new QSpinBox();
    intervalSpin_->setRange(1, 1440);
    intervalSpin_->setValue(60);
    intervalSpin_->setSuffix(" 分钟");
    intervalLayout->addWidget(intervalSpin_);
    intervalLayout->addStretch();
    
    updateLayout->addWidget(realtimeRadio_);
    updateLayout->addWidget(scheduledRadio_);
    updateLayout->addLayout(intervalLayout);
    updateLayout->addWidget(manualRadio_);
    mainLayout->addWidget(updateGroup);
    
    // 索引统计
    QGroupBox* statsGroup = new QGroupBox("索引统计");
    QFormLayout* statsLayout = new QFormLayout(statsGroup);
    
    filesLabel_ = new QLabel("0");
    sizeLabel_ = new QLabel("0 MB");
    timeLabel_ = new QLabel("从未");
    pendingLabel_ = new QLabel("0");
    
    statsLayout->addRow("已索引文件：", filesLabel_);
    statsLayout->addRow("索引大小：", sizeLabel_);
    statsLayout->addRow("最后更新：", timeLabel_);
    statsLayout->addRow("待索引文件：", pendingLabel_);
    
    mainLayout->addWidget(statsGroup);
    
    // 进度条
    progressBar_ = new QProgressBar();
    progressBar_->setVisible(false);
    mainLayout->addWidget(progressBar_);
    
    // 日志输出
    QGroupBox* logGroup = new QGroupBox("操作日志");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    logText_ = new QTextEdit();
    logText_->setReadOnly(true);
    logText_->setMaximumHeight(150);
    logLayout->addWidget(logText_);
    mainLayout->addWidget(logGroup);
    
    // 操作按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    rebuildButton_ = new QPushButton("重建索引");
    clearButton_ = new QPushButton("清除索引");
    saveButton_ = new QPushButton("保存设置");
    closeButton_ = new QPushButton("关闭");
    
    buttonLayout->addWidget(rebuildButton_);
    buttonLayout->addWidget(clearButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(saveButton_);
    buttonLayout->addWidget(closeButton_);
    
    mainLayout->addLayout(buttonLayout);
}

void IndexSettingsDialog::connectSignals() {
    connect(rebuildButton_, &QPushButton::clicked, this, &IndexSettingsDialog::onRebuildClicked);
    connect(clearButton_, &QPushButton::clicked, this, &IndexSettingsDialog::onClearClicked);
    connect(saveButton_, &QPushButton::clicked, this, &IndexSettingsDialog::onSaveClicked);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
    
    connect(indexer_, &FileIndexer::indexingStarted, this, &IndexSettingsDialog::onIndexingStarted);
    connect(indexer_, &FileIndexer::indexingProgress, this, &IndexSettingsDialog::onIndexingProgress);
    connect(indexer_, &FileIndexer::indexingFinished, this, &IndexSettingsDialog::onIndexingFinished);
    
    connect(indexer_, &FileIndexer::fileIndexed, [this](const QString& path) {
        logText_->append("✓ 已索引: " + path);
        // 限制日志行数
        if (logText_->document()->lineCount() > 100) {
            QTextCursor cursor = logText_->textCursor();
            cursor.movePosition(QTextCursor::Start);
            cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 20);
            cursor.removeSelectedText();
        }
        // 滚动到底部
        logText_->verticalScrollBar()->setValue(logText_->verticalScrollBar()->maximum());
    });
    
    // 启用/禁用定时扫描间隔
    connect(scheduledRadio_, &QRadioButton::toggled, [this](bool checked) {
        intervalSpin_->setEnabled(checked);
    });
}

void IndexSettingsDialog::loadConfig() {
    IndexConfig config = indexer_->getConfig();
    
    enabledCheck_->setChecked(config.enabled);
    
    txtCheck_->setChecked(config.includedExtensions.contains("txt"));
    pdfCheck_->setChecked(config.includedExtensions.contains("pdf"));
    docCheck_->setChecked(config.includedExtensions.contains("doc"));
    docxCheck_->setChecked(config.includedExtensions.contains("docx"));
    
    excludeEdit_->setText(config.excludedPatterns.join(", "));
    
    if (config.realtimeMonitoring) {
        realtimeRadio_->setChecked(true);
    } else if (config.scanIntervalMinutes > 0) {
        scheduledRadio_->setChecked(true);
        intervalSpin_->setValue(config.scanIntervalMinutes);
    } else {
        manualRadio_->setChecked(true);
    }
    
    intervalSpin_->setEnabled(scheduledRadio_->isChecked());
}

IndexConfig IndexSettingsDialog::getConfig() const {
    IndexConfig config;
    
    config.enabled = enabledCheck_->isChecked();
    
    config.includedExtensions.clear();
    if (txtCheck_->isChecked()) config.includedExtensions << "txt";
    if (pdfCheck_->isChecked()) config.includedExtensions << "pdf";
    if (docCheck_->isChecked()) config.includedExtensions << "doc";
    if (docxCheck_->isChecked()) config.includedExtensions << "docx";
    
    QString excludeText = excludeEdit_->text();
    config.excludedPatterns = excludeText.split(",", QString::SkipEmptyParts);
    for (QString& pattern : config.excludedPatterns) {
        pattern = pattern.trimmed();
    }
    
    config.realtimeMonitoring = realtimeRadio_->isChecked();
    
    if (scheduledRadio_->isChecked()) {
        config.scanIntervalMinutes = intervalSpin_->value();
    } else if (manualRadio_->isChecked()) {
        config.scanIntervalMinutes = 0;
    }
    
    return config;
}

void IndexSettingsDialog::setConfig(const IndexConfig& config) {
    indexer_->setConfig(config);
    loadConfig();
}

void IndexSettingsDialog::onRebuildClicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "重建索引",
        "重建索引将清除现有索引并重新扫描所有文件。\n\n"
        "这可能需要几分钟到几十分钟，具体取决于文件数量。\n\n"
        "确定要继续吗？",
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        logText_->clear();
        logText_->append("=== 开始重建索引 ===");
        indexer_->rebuildIndex();
    }
}

void IndexSettingsDialog::onClearClicked() {
    QMessageBox::StandardButton reply = QMessageBox::warning(
        this,
        "清除索引",
        "确定要清除所有索引数据吗？\n\n"
        "清除后将无法进行内容搜索，直到重新建立索引。",
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        indexer_->clearIndex();
        logText_->append("=== 索引已清除 ===");
        updateStats();
    }
}

void IndexSettingsDialog::onSaveClicked() {
    IndexConfig config = getConfig();
    indexer_->setConfig(config);
    
    logText_->append("=== 设置已保存 ===");
    QMessageBox::information(this, "保存成功", "索引设置已保存");
}

void IndexSettingsDialog::onIndexingStarted() {
    progressBar_->setVisible(true);
    progressBar_->setValue(0);
    rebuildButton_->setEnabled(false);
    clearButton_->setEnabled(false);
    logText_->append("索引开始...");
}

void IndexSettingsDialog::onIndexingProgress(int current, int total) {
    if (total > 0) {
        int percentage = (current * 100) / total;
        progressBar_->setValue(percentage);
        progressBar_->setFormat(QString("已处理 %1/%2 个文件 (%p%)").arg(current).arg(total));
    }
}

void IndexSettingsDialog::onIndexingFinished() {
    progressBar_->setVisible(false);
    rebuildButton_->setEnabled(true);
    clearButton_->setEnabled(true);
    logText_->append("=== 索引完成 ===");
    updateStats();
}

void IndexSettingsDialog::updateStats() {
    IndexStats stats = indexer_->getStats();
    
    filesLabel_->setText(QString::number(stats.totalFiles));
    sizeLabel_->setText(QString::number(stats.indexSizeMB) + " MB");
    
    if (stats.lastUpdateTime.isValid()) {
        timeLabel_->setText(stats.lastUpdateTime.toString("yyyy-MM-dd HH:mm:ss"));
    } else {
        timeLabel_->setText("从未");
    }
    
    pendingLabel_->setText(QString::number(stats.pendingFiles));
    
    if (stats.pendingFiles > 0) {
        pendingLabel_->setStyleSheet("color: orange; font-weight: bold;");
    } else {
        pendingLabel_->setStyleSheet("");
    }
}

} // namespace CrossNetShare
