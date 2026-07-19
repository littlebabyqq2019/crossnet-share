#pragma once

#include "../audit_logger.h"
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QComboBox>

namespace CrossNetShare {

class AuditLogDialog : public QDialog {
    Q_OBJECT

public:
    explicit AuditLogDialog(QWidget* parent = nullptr);

private slots:
    void onRefresh();
    void onExport();
    void onClear();
    void onFilterChanged();

private:
    void setupUi();
    void loadLogs();

    QTableWidget* logTable_;
    QLineEdit* usernameFilter_;
    QDateTimeEdit* startTimeEdit_;
    QDateTimeEdit* endTimeEdit_;
    QComboBox* actionFilter_;
    QPushButton* refreshButton_;
    QPushButton* exportButton_;
    QPushButton* clearButton_;
    QPushButton* closeButton_;
};

}
