#include "document_converter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#ifdef Q_OS_WIN
#include <QAxObject>
#endif

namespace CrossNetShare {

#ifdef Q_OS_WIN
QAxObject* DocumentConverter::wordApp = nullptr;
QMutex DocumentConverter::wordMutex;
#endif

void DocumentConverter::initialize() {
#ifdef Q_OS_WIN
    QMutexLocker locker(&wordMutex);
    if (!wordApp) {
        restartWordApp();
    }
#endif

    QDir cacheDir(QDir::temp().filePath("crossnet_preview_cache"));
    if (!cacheDir.exists()) {
        cacheDir.mkpath(".");
    }
}

void DocumentConverter::cleanup() {
#ifdef Q_OS_WIN
    QMutexLocker locker(&wordMutex);
    if (wordApp) {
        wordApp->dynamicCall("Quit()");
        delete wordApp;
        wordApp = nullptr;
    }
#endif
}

#ifdef Q_OS_WIN
void DocumentConverter::restartWordApp() {
    if (wordApp) {
        // 旧对象可能已经处于异常状态，尝试静默退出并释放，忽略任何失败
        wordApp->dynamicCall("Quit()");
        delete wordApp;
        wordApp = nullptr;
    }

    wordApp = new QAxObject("Word.Application");
    if (!wordApp->isNull()) {
        wordApp->setProperty("Visible", false);
        wordApp->setProperty("DisplayAlerts", 0);
    } else {
        delete wordApp;
        wordApp = nullptr;
    }
}

bool DocumentConverter::ensureWordAppReady() {
    if (!wordApp || wordApp->isNull()) {
        restartWordApp();
        return wordApp && !wordApp->isNull();
    }

    // 通过访问 Documents 集合来验证底层 COM 连接是否仍然存活。
    // Word 长时间运行后可能进入异常状态（进程假死、被系统回收等），
    // wordApp->isNull() 无法检测到这种情况，只能靠实际调用来试探。
    QAxObject* documents = wordApp->querySubObject("Documents");
    bool alive = documents != nullptr;
    delete documents;

    if (!alive) {
        qDebug() << "[DocumentConverter] Word COM connection appears dead, restarting Word.Application";
        restartWordApp();
        return wordApp && !wordApp->isNull();
    }

    return true;
}
#endif

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
    QString text = QString::fromUtf8(content);

    // 如果 UTF-8 解码后包含大量替换字符，尝试 GBK
    if (text.count(QChar::ReplacementCharacter) > content.size() / 10) {
        QTextCodec* gbkCodec = QTextCodec::codecForName("GBK");
        if (gbkCodec) {
            text = gbkCodec->toUnicode(content);
        }
    }

    result.success = true;
    result.mimeType = "text/html; charset=utf-8";
    result.data = "<pre class=\"text-preview\">" + htmlEscape(text) + "</pre>";
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

    QString cachePath = getCachePath(filePath);
    if (isCacheValid(cachePath, filePath)) {
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::ReadOnly)) {
            result.success = true;
            result.mimeType = "application/pdf";
            result.data = cacheFile.readAll();
            return result;
        }
    }

    PreviewResult wordResult = convertWordWithMicrosoftWord(filePath);
    if (wordResult.success) {
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) {
            cacheFile.write(wordResult.data);
            cacheFile.close();
        }
    }
    return wordResult;
}

DocumentConverter::PreviewResult DocumentConverter::convertWordWithMicrosoftWord(const QString& filePath) {
    PreviewResult result;
#ifndef Q_OS_WIN
    Q_UNUSED(filePath);
    result.success = false;
    result.error = "Microsoft Word COM conversion is only available on Windows";
    return result;
#else
    QMutexLocker locker(&wordMutex);

    if (!ensureWordAppReady()) {
        result.success = false;
        result.error = "Microsoft Word is not initialized and could not be restarted.";
        return result;
    }

    QTemporaryDir tempDir(QDir::temp().filePath("crossnet_word_preview_XXXXXX"));
    if (!tempDir.isValid()) {
        result.success = false;
        result.error = "Failed to create temporary Word conversion directory";
        return result;
    }

    QString pdfPath = tempDir.path() + "/" + QFileInfo(filePath).completeBaseName() + ".pdf";
    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QString nativePdfPath = QDir::toNativeSeparators(pdfPath);

    QAxObject* documents = wordApp->querySubObject("Documents");
    if (!documents) {
        // 再尝试一次重启后重试，应对 Documents 集合突然失效的情况
        restartWordApp();
        if (wordApp && !wordApp->isNull()) {
            documents = wordApp->querySubObject("Documents");
        }
    }
    if (!documents) {
        result.success = false;
        result.error = "Failed to access Microsoft Word Documents collection";
        return result;
    }

    QAxObject* document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
        nativeInputPath, false, true, false);
    delete documents;

    if (!document) {
        result.success = false;
        result.error = "Microsoft Word failed to open the document";
        return result;
    }

    document->dynamicCall("ExportAsFixedFormat(const QString&, int)",
        nativePdfPath, 17);

    document->dynamicCall("Close(bool)", false);
    delete document;

    QFile pdfFile(pdfPath);
    if (!pdfFile.open(QIODevice::ReadOnly)) {
        result.success = false;
        result.error = "Microsoft Word did not generate a PDF file";
        return result;
    }

    result.success = true;
    result.mimeType = "application/pdf";
    result.data = pdfFile.readAll();
    return result;
#endif
}

QByteArray DocumentConverter::htmlEscape(const QString& text) {
    QString escaped = text.toHtmlEscaped();
    return escaped.toUtf8();
}

QString DocumentConverter::getCachePath(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    QString hash = QString::fromUtf8(QCryptographicHash::hash(
        fileInfo.absoluteFilePath().toUtf8(),
        QCryptographicHash::Md5).toHex());

    QDir cacheDir(QDir::temp().filePath("crossnet_preview_cache"));
    return cacheDir.filePath(hash + ".pdf");
}

bool DocumentConverter::isCacheValid(const QString& cachePath, const QString& originalPath) {
    QFileInfo cacheInfo(cachePath);
    QFileInfo originalInfo(originalPath);

    if (!cacheInfo.exists()) {
        return false;
    }

    if (originalInfo.lastModified() > cacheInfo.lastModified()) {
        return false;
    }

    return true;
}

void DocumentConverter::cleanupCache() {
    QDir cacheDir(QDir::temp().filePath("crossnet_preview_cache"));
    if (!cacheDir.exists()) {
        return;
    }

    QFileInfoList files = cacheDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    QDateTime now = QDateTime::currentDateTime();

    for (const QFileInfo& fileInfo : files) {
        qint64 ageInDays = fileInfo.lastModified().daysTo(now);
        if (ageInDays > 7) {
            QFile::remove(fileInfo.absoluteFilePath());
        }
    }
}

QString DocumentConverter::convertWordToJpg(const QString& filePath, const QString& outputDir) {
#ifdef Q_OS_WIN
    QMutexLocker locker(&wordMutex);

    if (!ensureWordAppReady()) {
        qDebug() << "Microsoft Word not available for conversion";
        return QString();
    }

    QDir().mkpath(outputDir);

    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QString baseName = QFileInfo(filePath).completeBaseName();

    QAxObject* documents = wordApp->querySubObject("Documents");
    if (!documents) {
        restartWordApp();
        if (wordApp && !wordApp->isNull()) {
            documents = wordApp->querySubObject("Documents");
        }
    }
    if (!documents) {
        qDebug() << "Failed to access Word Documents";
        return QString();
    }

    QAxObject* document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
        nativeInputPath, false, true, false);
    delete documents;

    if (!document) {
        qDebug() << "Failed to open document in Word";
        return QString();
    }

    QString tempPdfPath = outputDir + "/" + baseName + "_temp.pdf";
    QString nativeTempPdfPath = QDir::toNativeSeparators(tempPdfPath);

    document->dynamicCall("ExportAsFixedFormat(const QString&, int)",
        nativeTempPdfPath, 17);

    document->dynamicCall("Close(bool)", false);
    delete document;

    if (!QFile::exists(tempPdfPath)) {
        qDebug() << "Failed to generate PDF";
        return QString();
    }

    return tempPdfPath;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(outputDir);
    return QString();
#endif
}

}
