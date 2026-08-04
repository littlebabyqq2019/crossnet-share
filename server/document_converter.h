#pragma once

#include <QString>
#include <QByteArray>
#include <QMutex>
#include <functional>

#ifdef Q_OS_WIN
class QAxObject;
#endif

namespace CrossNetShare {

class DocumentConverter {
public:
    struct PreviewResult {
        bool success;
        QString mimeType;
        QByteArray data;
        QString error;
    };

    static void initialize();
    static void cleanup();
    static void cleanupCache();

    static PreviewResult previewFile(const QString& filePath);
    static bool isPreviewSupported(const QString& filePath);

    // 直接将 Word 文档转换为高质量 JPG 图片（绕过 PDF）
    static QString convertWordToJpg(const QString& filePath, const QString& outputDir);

private:
    static PreviewResult previewText(const QString& filePath);
    static PreviewResult previewImage(const QString& filePath);
    static PreviewResult previewPdf(const QString& filePath);
    static PreviewResult previewWord(const QString& filePath);
    static PreviewResult convertWordWithLibreOffice(const QString& filePath);
    static PreviewResult convertWordWithMicrosoftWord(const QString& filePath);
    static QString findLibreOffice();
    static QByteArray htmlEscape(const QString& text);

    static QString getCachePath(const QString& filePath);
    static bool isCacheValid(const QString& cachePath, const QString& originalPath);

#ifdef Q_OS_WIN
    // 检查 wordApp 是否仍然可用，如果失效则重新创建。
    // 调用前必须已持有 wordMutex 锁。
    static bool ensureWordAppReady();
    static void restartWordApp();

    // 在看门狗保护下执行一个可能阻塞的 Word COM 调用。
    // 如果 action 在 timeoutMs 内未返回，说明 WINWORD.EXE 已经假死，
    // 会强制结束该进程，使阻塞的调用因连接中断而返回，避免服务端主线程被无限期卡死。
    // 调用前必须已持有 wordMutex 锁。
    // 返回 true 表示 action 在超时前正常完成；返回 false 表示发生了超时并已强制结束 Word 进程，
    // 此时 wordApp 底层连接已失效，调用方需要自行调用 restartWordApp()。
    static bool runWordActionWithWatchdog(const std::function<void()>& action, int timeoutMs = 20000);
    static void killWordProcess();

    static QAxObject* wordApp;
    static QMutex wordMutex;
#endif
};

}
