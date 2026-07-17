#pragma once

#include <QString>
#include <QByteArray>

namespace CrossNetShare {

class DocumentConverter {
public:
    struct PreviewResult {
        bool success;
        QString mimeType;
        QByteArray data;
        QString error;
    };

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
};

}
