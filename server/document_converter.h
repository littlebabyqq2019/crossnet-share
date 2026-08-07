#pragma once

#include <QString>
#include <QByteArray>
#include <QMutex>

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

    static QString convertWordToJpg(const QString& filePath, const QString& outputDir);

private:
    static PreviewResult previewText(const QString& filePath);
    static PreviewResult previewImage(const QString& filePath);
    static PreviewResult previewPdf(const QString& filePath);
    static PreviewResult previewWord(const QString& filePath);
    static PreviewResult convertWordWithMicrosoftWord(const QString& filePath);
    static QByteArray htmlEscape(const QString& text);

    static QString getCachePath(const QString& filePath);
    static bool isCacheValid(const QString& cachePath, const QString& originalPath);

#ifdef Q_OS_WIN
    // Word 自动化在服务端主进程内进行：wordApp 是一个常驻的 QAxObject 实例，
    // 每次转换前用 ensureWordAppReady() 探活；探活失败或长时间运行后失效时
    // 由 restartWordApp() 悄悄重启一个新实例。所有 Word 相关操作都必须在
    // 持有 wordMutex 的前提下进行（Word 自动化不是线程安全的）。
    static bool ensureWordAppReady();
    static void restartWordApp();

    static QAxObject* wordApp;
    static QMutex wordMutex;
#endif
};

}
