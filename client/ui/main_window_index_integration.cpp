// 这个文件包含了主窗口索引器集成的所有新代码
// 需要将这些代码合并到 main_window.cpp 中

#include "main_window.h"
#include "index_settings_dialog.h"
#include <QtConcurrent>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QStandardPaths>

namespace CrossNetShare {

// ============================================================================
// 在 setupUi() 函数末尾添加搜索区域
// ============================================================================
void MainWindow::addSearchGroupToUI(QVBoxLayout* mainLayout) {
    // 内容搜索区域
    QGroupBox* searchGroup = new QGroupBox("内容搜索");
    QHBoxLayout* searchLayout = new QHBoxLayout(searchGroup);
    
    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText("输入搜索关键词（支持 AND OR NOT）...");
    searchButton_ = new QPushButton("搜索内容");
    searchResultLabel_ = new QLabel("");
    searchResultLabel_->setStyleSheet("color: blue;");
    
    searchLayout->addWidget(new QLabel("关键词:"));
    searchLayout->addWidget(searchEdit_);
    searchLayout->addWidget(searchButton_);
    searchLayout->addWidget(searchResultLabel_);
    
    connect(searchEdit_, &QLineEdit::returnPressed, this, &MainWindow::onContentSearchClicked);
    connect(searchButton_, &QPushButton::clicked, this, &MainWindow::onContentSearchClicked);
    
    mainLayout->addWidget(searchGroup);
}

// ============================================================================
// 设置菜单栏
// ============================================================================
void MainWindow::setupMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    
    // 工具菜单
    QMenu* toolsMenu = menuBar->addMenu("工具(&T)");
    
    indexSettingsAction_ = new QAction("索引设置(&I)...", this);
    indexSettingsAction_->setStatusTip("配置全文索引设置");
    connect(indexSettingsAction_, &QAction::triggered, this, &MainWindow::onIndexSettingsClicked);
    toolsMenu->addAction(indexSettingsAction_);
    
    toolsMenu->addSeparator();
    
    QAction* exitAction = new QAction("退出(&X)", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::onQuitApp);
    toolsMenu->addAction(exitAction);
    
    // 帮助菜单
    QMenu* helpMenu = menuBar->addMenu("帮助(&H)");
    
    QAction* aboutAction = new QAction("关于(&A)", this);
    connect(aboutAction, &QAction::triggered, [this]() {
        QMessageBox::about(this, "关于 CrossNetShare",
            "CrossNetShare v" PROJECT_VERSION "\n\n"
            "跨网段文件共享工具\n\n"
            "支持全文检索功能");
    });
    helpMenu->addAction(aboutAction);
}

// ============================================================================
// 初始化索引器
// ============================================================================
void MainWindow::initializeIndexer() {
    QString sharePath = client_->getSharePath();
    
    if (sharePath.isEmpty()) {
        onLogMessage("Share path not set, content indexing disabled");
        return;
    }
    
    // 创建索引器
    indexer_ = new FileIndexer(this);
    
    // 设置索引数据库路径
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataPath);
    QString dbPath = appDataPath + "/content_index.db";
    
    if (!indexer_->initialize(sharePath, dbPath)) {
        onLogMessage("Failed to initialize content indexer");
        delete indexer_;
        indexer_ = nullptr;
        return;
    }
    
    // 设置默认配置
    IndexConfig config;
    config.enabled = true;
    config.realtimeMonitoring = true;
    config.includedExtensions = {"txt", "pdf", "doc", "docx"};
    config.excludedPatterns = {"~$*", "*.tmp", "temp/*"};
    config.maxFileSizeMB = 50;
    config.scanIntervalMinutes = 60;
    
    indexer_->setConfig(config);
    
    // 连接信号
    connect(indexer_, &FileIndexer::indexingStarted, [this]() {
        onLogMessage("Content indexing started...");
    });
    
    connect(indexer_, &FileIndexer::indexingFinished, [this]() {
        onLogMessage("Content indexing finished");
        IndexStats stats = indexer_->getStats();
        onLogMessage(QString("Indexed %1 files, total size: %2 MB")
            .arg(stats.totalFiles)
            .arg(stats.indexSizeMB));
    });
    
    connect(indexer_, &FileIndexer::indexingError, [this](const QString& error) {
        onLogMessage("Indexing error: " + error);
    });
    
    // 启动索引器
    indexer_->start();
    onLogMessage("Content indexer initialized and started");
    
    // 触发首次索引（在后台线程，延迟2秒避免启动时卡顿）
    QTimer::singleShot(2000, [this]() {
        if (indexer_) {
            onLogMessage("Starting initial content indexing (this may take a while)...");
            indexer_->rebuildIndex();
        }
    });
}

// ============================================================================
// 槽函数：打开索引设置对话框
// ============================================================================
void MainWindow::onIndexSettingsClicked() {
    if (!indexer_) {
        QMessageBox::warning(this, "索引未启用",
            "内容索引功能未启用。\n\n"
            "请先设置共享路径并重启客户端。");
        return;
    }
    
    IndexSettingsDialog dialog(indexer_, this);
    dialog.exec();
}

// ============================================================================
// 槽函数：搜索框文本变化（可用于实时搜索提示）
// ============================================================================
void MainWindow::onSearchTextChanged(const QString& text) {
    // 可以在这里实现实时搜索提示
    Q_UNUSED(text);
}

// ============================================================================
// 槽函数：点击搜索按钮
// ============================================================================
void MainWindow::onContentSearchClicked() {
    QString query = searchEdit_->text().trimmed();
    
    if (query.isEmpty()) {
        searchResultLabel_->setText("");
        return;
    }
    
    if (!indexer_) {
        QMessageBox::warning(this, "索引未启用",
            "内容索引功能未启用，无法进行内容搜索。");
        return;
    }
    
    performContentSearch(query);
}

// ============================================================================
// 执行内容搜索
// ============================================================================
void MainWindow::performContentSearch(const QString& query) {
    searchResultLabel_->setText("搜索中...");
    searchResultLabel_->setStyleSheet("color: orange;");
    
    // 在后台线程执行搜索
    QtConcurrent::run([this, query]() {
        QStringList results = indexer_->search(query);
        
        // 回到主线程更新UI
        QMetaObject::invokeMethod(this, [this, results, query]() {
            if (results.isEmpty()) {
                searchResultLabel_->setText("未找到匹配结果");
                searchResultLabel_->setStyleSheet("color: gray;");
                onLogMessage(QString("Content search for '%1': no results").arg(query));
            } else {
                searchResultLabel_->setText(QString("找到 %1 个匹配文件").arg(results.size()));
                searchResultLabel_->setStyleSheet("color: green;");
                onLogMessage(QString("Content search for '%1': %2 results").arg(query).arg(results.size()));
                
                // 在日志中显示搜索结果
                onLogMessage("Search results:");
                int count = 0;
                for (const QString& path : results) {
                    onLogMessage("  - " + path);
                    count++;
                    if (count >= 20) {  // 限制显示前20个结果
                        onLogMessage(QString("  ... and %1 more results").arg(results.size() - 20));
                        break;
                    }
                }
                
                // TODO: 可以在这里过滤文件树，只显示匹配的文件
                // 这需要修改 updateFileTree() 函数来支持过滤
            }
        }, Qt::QueuedConnection);
    });
}

} // namespace CrossNetShare
