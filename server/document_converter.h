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
    static QAxObject* wordApp;
    static QMutex wordMutex;
#endif
};

}
