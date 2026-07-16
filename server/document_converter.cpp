#include "document_converter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace CrossNetShare {

DocumentConverter::PreviewResult DocumentConverter::previewFile(const QString& filePath) {
    QFileInfo info(filePath);
    QString suffix = info.suffix().toLower();

    if (suffix == "txt" || suffix == "log" || suffix == "csv" || suffix == "md" || suffix == "json" || suffix == "xml") {
        return previewText(filePath);
    }
    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "gif" || suffix == "bmp" || suffix == "webp") {
        return previewImage(filePath);
    }
    if (suffix == "pdf") {
        return previewPdf(filePath);
    }
    if (suffix == "doc" || suffix == "docx" || suffix == "rtf") {
        return previewWord(filePath);
    }

    PreviewResult result;
    result.success = false;
    result.error = "Unsupported preview type";
    return result;
}

bool DocumentConverter::isPreviewSupported(const QString& filePath) {
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == "txt" || suffix == "log" || suffix == "csv" || suffix == "md" ||
           suffix == "json" || suffix == "xml" || suffix == "png" || suffix == "jpg" ||
           suffix == "jpeg" || suffix == "gif" || suffix == "bmp" || suffix == "webp" ||
           suffix == "pdf" || suffix == "doc" || suffix == "docx" || suffix == "rtf";
}

DocumentConverter::PreviewResult DocumentConverter::previewText(const QString& filePath) {
    QFile file(filePath);
    PreviewResult result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.success = false;
        result.error = "Failed to open text file";
        return result;
    }

    QByteArray content = file.read(1024 * 1024);
    result.success = true;
    result.mimeType = "text/html; charset=utf-8";
    result.data = "<pre class=\"text-preview\">" + htmlEscape(QString::fromUtf8(content)) + "</pre>";
    return result;
}

DocumentConverter::PreviewResult DocumentConverter::previewImage(const QString& filePath) {
    QFile file(filePath);
    PreviewResult result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.success = false;
        result.error = "Failed to open image file";
        return result;
    }

    QString suffix = QFileInfo(filePath).suffix().toLower();
    result.success = true;
    result.mimeType = suffix == "jpg" ? "image/jpeg" : "image/" + suffix;
    result.data = file.readAll();
    return result;
}

DocumentConverter::PreviewResult DocumentConverter::previewPdf(const QString& filePath) {
    QFile file(filePath);
    PreviewResult result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.success = false;
        result.error = "Failed to open PDF file";
        return result;
    }

    result.success = true;
    result.mimeType = "application/pdf";
    result.data = file.readAll();
    return result;
}

DocumentConverter::PreviewResult DocumentConverter::previewWord(const QString& filePath) {
    PreviewResult result;
    QString libreOffice = findLibreOffice();
    if (libreOffice.isEmpty()) {
        result.success = false;
        result.error = "LibreOffice not found on server. Install LibreOffice on computer A and ensure soffice is in PATH.";
        return result;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        result.success = false;
        result.error = "Failed to create temporary conversion directory";
        return result;
    }

    QProcess process;
    QStringList args;
    args << "--headless" << "--convert-to" << "html" << "--outdir" << tempDir.path() << filePath;
    process.start(libreOffice, args);
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.success = false;
        result.error = "LibreOffice conversion failed: " + QString::fromUtf8(process.readAllStandardError());
        return result;
    }

    QString htmlPath = tempDir.path() + "/" + QFileInfo(filePath).completeBaseName() + ".html";
    QFile htmlFile(htmlPath);
    if (!htmlFile.open(QIODevice::ReadOnly)) {
        result.success = false;
        result.error = "Converted HTML file not found";
        return result;
    }

    result.success = true;
    result.mimeType = "text/html; charset=utf-8";
    result.data = htmlFile.readAll();
    return result;
}

QString DocumentConverter::findLibreOffice() {
    QStringList candidates;
    candidates << "soffice"
               << "C:/Program Files/LibreOffice/program/soffice.exe"
               << "C:/Program Files (x86)/LibreOffice/program/soffice.exe";

    for (const QString& candidate : candidates) {
        if (candidate == "soffice") {
            QString found = QStandardPaths::findExecutable(candidate);
            if (!found.isEmpty()) {
                return found;
            }
        } else if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return QString();
}

QByteArray DocumentConverter::htmlEscape(const QString& text) {
    QString escaped = text.toHtmlEscaped();
    return escaped.toUtf8();
}

}
