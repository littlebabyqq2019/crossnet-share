#include "document_converter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QUrl>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>

namespace CrossNetShare {

void DocumentConverter::initialize() {
    QDir cacheDir(QDir::temp().filePath("crossnet_preview_cache"));
    if (!cacheDir.exists()) {
        cacheDir.mkpath(".");
    }
}

void DocumentConverter::cleanup() {
    // 清理工作（如有需要）
}

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
            qDebug() << "Word preview loaded from cache:" << filePath;
            return result;
        }
    }

    qDebug() << "Converting Word document:" << filePath;

    // 使用 Aspose.Words 进行转换
    qDebug() << "Attempting conversion with Aspose.Words...";
    PreviewResult asposeResult = convertWordWithAspose(filePath);
    if (asposeResult.success) {
        qDebug() << "✓ Successfully converted with Aspose.Words:" << filePath;
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) {
            cacheFile.write(asposeResult.data);
            cacheFile.close();
        }
        return asposeResult;
    } else {
        qDebug() << "✗ Aspose.Words conversion failed:" << asposeResult.error;
    }
    
    return asposeResult;
}

DocumentConverter::PreviewResult DocumentConverter::convertWordWithAspose(const QString& filePath) {
    PreviewResult result;
    
    // 查找Python解释器
    QString python = findPython();
    if (python.isEmpty()) {
        result.success = false;
        result.error = "Python not found. Please install Python 3.8 or later.";
        return result;
    }
    
    // 查找转换脚本
    QString scriptPath = QCoreApplication::applicationDirPath() + "/word_to_pdf.py";
    if (!QFileInfo::exists(scriptPath)) {
        result.success = false;
        result.error = "Aspose conversion script not found: " + scriptPath;
        return result;
    }
    
    // 创建临时目录
    QTemporaryDir tempDir(QDir::temp().filePath("crossnet_aspose_XXXXXX"));
    if (!tempDir.isValid()) {
        result.success = false;
        result.error = "Failed to create temporary Aspose conversion directory";
        return result;
    }
    
    // 生成输出PDF路径
    QString pdfPath = tempDir.path() + "/" + QFileInfo(filePath).completeBaseName() + ".pdf";
    
    // 查找许可证文件
    QString licensePath = findAsposeLicense();
    
    // 构建命令
    QProcess process;
    QStringList args;
    args << scriptPath << filePath << pdfPath;
    if (!licensePath.isEmpty()) {
        args << licensePath;
    }
    
    qDebug() << "Running Aspose.Words converter:" << python << args.join(" ");
    
    process.start(python, args);
    if (!process.waitForStarted(10000)) {
        result.success = false;
        result.error = "Failed to start Aspose Python converter: " + process.errorString();
        return result;
    }
    
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(5000);
        result.success = false;
        result.error = "Aspose conversion timed out";
        return result;
    }
    
    QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.success = false;
        result.error = QString("Aspose conversion failed with exit code %1").arg(process.exitCode());
        if (!stderr_output.isEmpty()) {
            result.error += ": " + stderr_output;
        }
        return result;
    }
    
    // 读取生成的PDF
    QFile pdfFile(pdfPath);
    if (!pdfFile.open(QIODevice::ReadOnly)) {
        result.success = false;
        result.error = "Aspose converter did not generate a PDF file";
        if (!stderr_output.isEmpty()) {
            result.error += ": " + stderr_output;
        }
        return result;
    }
    
    result.success = true;
    result.mimeType = "application/pdf";
    result.data = pdfFile.readAll();
    
    if (!stderr_output.isEmpty()) {
        qDebug() << "Aspose converter output:" << stderr_output;
    }
    
    return result;
}

QString DocumentConverter::findPython() {
    // 尝试在PATH中查找python
    QString python = QStandardPaths::findExecutable("python");
    if (!python.isEmpty()) {
        return python;
    }
    
    python = QStandardPaths::findExecutable("python3");
    if (!python.isEmpty()) {
        return python;
    }
    
    // Windows常见Python安装位置
#ifdef Q_OS_WIN
    QStringList candidates = {
        "C:/Python312/python.exe",
        "C:/Python311/python.exe",
        "C:/Python310/python.exe",
        "C:/Python39/python.exe",
        "C:/Python38/python.exe",
        qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Python/Python312/python.exe",
        qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Python/Python311/python.exe",
        qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Python/Python310/python.exe"
    };
    
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
#endif
    
    return QString();
}

QString DocumentConverter::findAsposeLicense() {
    // 尝试多个可能的许可证位置
    QStringList candidates = {
        // 推荐的许可证文件名
        QCoreApplication::applicationDirPath() + "/aspose.words.lic",
        QCoreApplication::applicationDirPath() + "/1.lic",
        QCoreApplication::applicationDirPath() + "/aspose.lic",
        // 开发环境路径
        QCoreApplication::applicationDirPath() + "/../Aspose.Words/python专用whl包/aspose.words.lic",
        QCoreApplication::applicationDirPath() + "/../Aspose.Words/python专用whl包/1.lic",
        QDir::currentPath() + "/Aspose.Words/python专用whl包/aspose.words.lic",
        QDir::currentPath() + "/Aspose.Words/python专用whl包/1.lic",
        QDir::currentPath() + "/aspose.words.lic",
        QDir::currentPath() + "/1.lic",
        QDir::currentPath() + "/aspose.lic"
    };
    
    for (const QString& candidate : candidates) {
        QString cleanPath = QDir::cleanPath(candidate);
        if (QFileInfo::exists(cleanPath)) {
            qDebug() << "Found Aspose license at:" << cleanPath;
            return cleanPath;
        }
    }
    
    qDebug() << "No Aspose license found, will run in evaluation mode";
    return QString();
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
    // 使用Aspose.Words将Word转换为PDF，然后由调用者转换为JPG
    // 这个函数主要用于水印服务
    
    QDir().mkpath(outputDir);
    
    QString baseName = QFileInfo(filePath).completeBaseName();
    QString tempPdfPath = outputDir + "/" + baseName + "_temp.pdf";
    
    // 使用Aspose转换为PDF
    PreviewResult result = convertWordWithAspose(filePath);
    if (!result.success) {
        qDebug() << "Failed to convert Word to PDF for watermarking:" << result.error;
        return QString();
    }
    
    // 保存PDF到临时文件
    QFile pdfFile(tempPdfPath);
    if (!pdfFile.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to save temp PDF file";
        return QString();
    }
    
    pdfFile.write(result.data);
    pdfFile.close();
    
    return tempPdfPath;
}

}
