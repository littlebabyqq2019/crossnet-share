#include "audit_log_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>

namespace CrossNetShare {

AuditLogDialog::AuditLogDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    loadLogs();
}

void AuditLogDialog::setupUi() {
    setWindowTitle("审计日志");
    resize(1000, 700);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 筛选区域
    QHBoxLayout* filterLayout = new QHBoxLayout();

    filterLayout->addWidget(new QLabel("用户名:"));
    usernameFilter_ = new QLineEdit(this);
    usernameFilter_->setPlaceholderText("全部");
    usernameFilter_->setMaximumWidth(150);
    connect(usernameFilter_, &QLineEdit::textChanged, this, &AuditLogDialog::onFilterChanged);
    filterLayout->addWidget(usernameFilter_);

    filterLayout->addWidget(new QLabel("操作类型:"));
    actionFilter_ = new QComboBox(this);
    actionFilter_->addItem("全部", -1);
    actionFilter_->addItem("登录", static_cast<int>(AuditAction::Login));
    actionFilter_->addItem("登出", static_cast<int>(AuditAction::Logout));
    actionFilter_->addItem("查看文件", static_cast<int>(AuditAction::ViewFile));
    actionFilter_->addItem("预览文件", static_cast<int>(AuditAction::PreviewFile));
    actionFilter_->addItem("打印文件", static_cast<int>(AuditAction::PrintFile));
    actionFilter_->addItem("下载文件", static_cast<int>(AuditAction::DownloadFile));
    actionFilter_->addItem("批量下载", static_cast<int>(AuditAction::BatchDownload));
    actionFilter_->addItem("搜索文件", static_cast<int>(AuditAction::SearchFiles));
    actionFilter_->setMaximumWidth(120);
    connect(actionFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AuditLogDialog::onFilterChanged);
    filterLayout->addWidget(actionFilter_);

    filterLayout->addWidget(new QLabel("开始时间:"));
    startTimeEdit_ = new QDateTimeEdit(this);
    startTimeEdit_->setDateTime(QDateTime::currentDateTime().addDays(-7));
    startTimeEdit_->setCalendarPopup(true);
    startTimeEdit_->setDisplayFormat("yyyy-MM-dd HH:mm");
    connect(startTimeEdit_, &QDateTimeEdit::dateTimeChanged, this, &AuditLogDialog::onFilterChanged);
    filterLayout->addWidget(startTimeEdit_);

    filterLayout->addWidget(new QLabel("结束时间:"));
    endTimeEdit_ = new QDateTimeEdit(this);
    endTimeEdit_->setDateTime(QDateTime::currentDateTime());
    endTimeEdit_->setCalendarPopup(true);
    endTimeEdit_->setDisplayFormat("yyyy-MM-dd HH:mm");
    connect(endTimeEdit_, &QDateTimeEdit::dateTimeChanged, this, &AuditLogDialog::onFilterChanged);
    filterLayout->addWidget(endTimeEdit_);

    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // 日志表格
    logTable_ = new QTableWidget(this);
    logTable_->setColumnCount(6);
    logTable_->setHorizontalHeaderLabels({"时间", "用户名", "操作", "目标", "IP地址", "状态"});
    logTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable_->horizontalHeader()->setStretchLastSection(true);
    logTable_->setColumnWidth(0, 150);
    logTable_->setColumnWidth(1, 100);
    logTable_->setColumnWidth(2, 100);
    logTable_->setColumnWidth(3, 300);
    logTable_->setColumnWidth(4, 120);

    mainLayout->addWidget(logTable_);

    // 按钮区域
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    refreshButton_ = new QPushButton("刷新", this);
    connect(refreshButton_, &QPushButton::clicked, this, &AuditLogDialog::onRefresh);
    buttonLayout->addWidget(refreshButton_);

    exportButton_ = new QPushButton("导出", this);
    connect(exportButton_, &QPushButton::clicked, this, &AuditLogDialog::onExport);
    buttonLayout->addWidget(exportButton_);

    clearButton_ = new QPushButton("清空日志", this);
    clearButton_->setStyleSheet("QPushButton { background-color: #dc2626; color: white; }");
    connect(clearButton_, &QPushButton::clicked, this, &AuditLogDialog::onClear);
    buttonLayout->addWidget(clearButton_);

    closeButton_ = new QPushButton("关闭", this);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton_);

    mainLayout->addLayout(buttonLayout);
}

void AuditLogDialog::loadLogs() {
    logTable_->setRowCount(0);

    QString username = usernameFilter_->text().trimmed();
    QDateTime startTime = startTimeEdit_->dateTime();
    QDateTime endTime = endTimeEdit_->dateTime();

    QVector<AuditLogEntry> logs = AuditLogger::instance()->getLogs(username, startTime, endTime);

    // 应用操作类型过滤
    int actionType = actionFilter_->currentData().toInt();
    if (actionType >= 0) {
        QVector<AuditLogEntry> filtered;
        for (const AuditLogEntry& entry : logs) {
            if (static_cast<int>(entry.action) == actionType) {
                filtered.append(entry);
            }
        }
        logs = filtered;
    }

    // 按时间倒序显示
    std::sort(logs.begin(), logs.end(), [](const AuditLogEntry& a, const AuditLogEntry& b) {
        return a.timestamp > b.timestamp;
    });

    for (const AuditLogEntry& entry : logs) {
        int row = logTable_->rowCount();
        logTable_->insertRow(row);

        logTable_->setItem(row, 0, new QTableWidgetItem(entry.timestamp.toString("yyyy-MM-dd HH:mm:ss")));
        logTable_->setItem(row, 1, new QTableWidgetItem(entry.username));
        logTable_->setItem(row, 2, new QTableWidgetItem(actionToString(entry.action)));
        logTable_->setItem(row, 3, new QTableWidgetItem(entry.target));
        logTable_->setItem(row, 4, new QTableWidgetItem(entry.ipAddress));
        logTable_->setItem(row, 5, new QTableWidgetItem(entry.success ? "成功" : "失败"));

        // 失败的操作用红色标记
        if (!entry.success) {
            for (int col = 0; col < 6; col++) {
                logTable_->item(row, col)->setForeground(QColor(220, 38, 38));
            }
        }
    }
}

void AuditLogDialog::onRefresh() {
    loadLogs();
}

void AuditLogDialog::onExport() {
    QString filename = QFileDialog::getSaveFileName(this, "导出审计日志", "", "JSON文件 (*.json)");
    if (filename.isEmpty()) {
        return;
    }

    if (!filename.endsWith(".json", Qt::CaseInsensitive)) {
        filename += ".json";
    }

    if (AuditLogger::instance()->saveToFile(filename)) {
        QMessageBox::information(this, "成功", "审计日志已导出到:\n" + filename);
    } else {
        QMessageBox::warning(this, "失败", "导出审计日志失败");
    }
}

void AuditLogDialog::onClear() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认清空",
        "确定要清空所有审计日志吗？此操作不可恢复！",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        AuditLogger::instance()->clearLogs();
        loadLogs();
        QMessageBox::information(this, "成功", "审计日志已清空");
    }
}

void AuditLogDialog::onFilterChanged() {
    loadLogs();
}

}
