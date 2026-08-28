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
#include <QThread>
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include <QAxObject>
#endif

namespace CrossNetShare {

#ifdef Q_OS_WIN
QAxObject* DocumentConverter::wordApp = nullptr;
QMutex DocumentConverter::wordMutex;
qint64 DocumentConverter::wordInitializedTime = 0;
#endif

namespace {

void addLibreOfficeCandidate(QStringList& candidates, const QString& candidate) {
    QString path = QDir::fromNativeSeparators(candidate.trimmed());
    if (path.isEmpty()) {
        return;
    }
    if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"')) {
        path = path.mid(1, path.size() - 2);
    }

    QFileInfo info(path);
    QStringList paths;
    if (info.isDir()) {
        paths << path + "/soffice.exe" << path + "/program/soffice.exe" << path + "/soffice" << path + "/program/soffice";
    } else {
        paths << path;
    }

    for (const QString& item : paths) {
        QString clean = QDir::cleanPath(item);
        if (!clean.isEmpty() && !candidates.contains(clean, Qt::CaseInsensitive)) {
            candidates << clean;
        }
    }
}

QString processDetails(QProcess& process) {
    QStringList details;
    QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();

    if (!stdoutText.isEmpty()) {
        details << "stdout: " + stdoutText;
    }
    if (!stderrText.isEmpty()) {
        details << "stderr: " + stderrText;
    }

    return details.join('\n');
}

QString withDetails(const QString& message, const QString& details) {
    return details.isEmpty() ? message : message + "\n" + details;
}

}

void DocumentConverter::initialize() {
#ifdef Q_OS_WIN
    QMutexLocker locker(&wordMutex);
    if (!wordApp) {
        wordApp = new QAxObject("Word.Application");
        if (!wordApp->isNull()) {
            wordApp->setProperty("Visible", false);
            wordApp->setProperty("DisplayAlerts", 0);
            wordInitializedTime = QDateTime::currentMSecsSinceEpoch();
            qDebug() << "Microsoft Word initialized at" << QDateTime::currentDateTime().toString();
        } else {
            delete wordApp;
            wordApp = nullptr;
            wordInitializedTime = 0;
        }
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
        wordInitializedTime = 0;
    }
#endif
}

#ifdef Q_OS_WIN
bool DocumentConverter::shouldReinitializeWord() {
    if (!wordApp || wordApp->isNull()) {
        return true;
    }
    
    // 检查Word运行时长，超过3小时则需要重启
    if (wordInitializedTime > 0) {
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        qint64 runtime = currentTime - wordInitializedTime;
        
        if (runtime > WORD_MAX_LIFETIME_MS) {
            qDebug() << "Word instance has been running for" << (runtime / 1000 / 60) << "minutes, should reinitialize";
            return true;
        }
    }
    
    return false;
}

bool DocumentConverter::reinitializeWord() {
    qDebug() << "Reinitializing Microsoft Word...";
    
    // 清理旧实例
    if (wordApp) {
        try {
            // 尝试快速退出，设置短超时
            wordApp->setProperty("DisplayAlerts", 0);
            wordApp->dynamicCall("Quit()");
        } catch (...) {
            qDebug() << "Exception during Word Quit, ignoring...";
        }
        delete wordApp;
        wordApp = nullptr;
        wordInitializedTime = 0;
        
        // 给Windows一点时间清理COM对象
        QThread::msleep(100);
    }
    
    // 创建新实例
    wordApp = new QAxObject("Word.Application");
    if (!wordApp->isNull()) {
        wordApp->setProperty("Visible", false);
        wordApp->setProperty("DisplayAlerts", 0);
        wordInitializedTime = QDateTime::currentMSecsSinceEpoch();
        qDebug() << "Microsoft Word reinitialized successfully at" << QDateTime::currentDateTime().toString();
        return true;
    } else {
        delete wordApp;
        wordApp = nullptr;
        wordInitializedTime = 0;
        qDebug() << "Failed to reinitialize Microsoft Word";
        return false;
    }
}

bool DocumentConverter::isWordHealthy() {
    if (!wordApp || wordApp->isNull()) {
        return false;
    }
    
    // 尝试访问Documents集合来检查Word是否健康
    QAxObject* documents = wordApp->querySubObject("Documents");
    if (!documents) {
        return false;
    }
    
    delete documents;
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

    // 优先尝试使用Aspose.Words（最稳定）
    PreviewResult asposeResult = convertWordWithAspose(filePath);
    if (asposeResult.success) {
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) {
            cacheFile.write(asposeResult.data);
            cacheFile.close();
        }
        return asposeResult;
    }

    // 备选方案1: Microsoft Word COM
    PreviewResult wordResult = convertWordWithMicrosoftWord(filePath);
    if (wordResult.success) {
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) {
            cacheFile.write(wordResult.data);
            cacheFile.close();
        }
        return wordResult;
    }

    // 检查是否是损坏的Word文档
    QFile checkFile(filePath);
    if (checkFile.open(QIODevice::ReadOnly)) {
        QByteArray header = checkFile.read(512);
        if (header.contains("<?xml") && header.contains("wordDocument")) {
            return wordResult;
        }
    }

    // 备选方案2: LibreOffice
    PreviewResult libreOfficeResult = convertWordWithLibreOffice(filePath);
    if (libreOfficeResult.success) {
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) {
            cacheFile.write(libreOfficeResult.data);
            cacheFile.close();
        }
    } else {
        // 合并所有错误信息
        QStringList errors;
        if (!asposeResult.error.isEmpty()) {
            errors << "Aspose.Words: " + asposeResult.error;
        }
        if (!wordResult.error.isEmpty()) {
            errors << "Microsoft Word: " + wordResult.error;
        }
        if (!libreOfficeResult.error.isEmpty()) {
            errors << "LibreOffice: " + libreOfficeResult.error;
        }
        libreOfficeResult.error = errors.join("\n");
    }
    return libreOfficeResult;
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

DocumentConverter::PreviewResult DocumentConverter::convertWordWithMicrosoftWord(const QString& filePath) {
    PreviewResult result;
#ifndef Q_OS_WIN
    result.success = false;
    result.error = "Microsoft Word COM conversion is only available on Windows";
    return result;
#else
    QMutexLocker locker(&wordMutex);

    // 首次调用或Word未初始化
    if (!wordApp || wordApp->isNull()) {
        result.success = false;
        result.error = "Microsoft Word is not initialized. Call DocumentConverter::initialize() first.";
        return result;
    }

    // 检查是否需要预防性重启（基于运行时长）
    if (shouldReinitializeWord()) {
        qDebug() << "Proactively reinitializing Word due to long runtime";
        if (!reinitializeWord()) {
            result.success = false;
            result.error = "Failed to reinitialize Microsoft Word";
            return result;
        }
    }

    // 重试机制：最多尝试2次
    const int maxRetries = 2;
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        // 从第二次尝试开始，先检查Word健康状态
        if (attempt > 1) {
            qDebug() << "Attempt" << attempt << "to convert Word document";
            if (!isWordHealthy()) {
                qDebug() << "Word is unhealthy, reinitializing...";
                if (!reinitializeWord()) {
                    result.success = false;
                    result.error = "Failed to reinitialize Microsoft Word after connection loss";
                    return result;
                }
            }
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
            qDebug() << "Failed to access Microsoft Word Documents collection on attempt" << attempt;
            
            // 如果还有重试机会，继续循环
            if (attempt < maxRetries) {
                // 标记Word不健康，下次循环会重新初始化
                continue;
            }
            
            result.success = false;
            result.error = "Failed to access Microsoft Word Documents collection";
            return result;
        }

        QAxObject* document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
            nativeInputPath, false, true, false);

        if (!document) {
            delete documents;
            
            if (attempt < maxRetries) {
                qDebug() << "Failed to open document on attempt" << attempt << ", retrying...";
                continue;
            }
            
            result.success = false;
            result.error = "Microsoft Word failed to open the document";
            return result;
        }

        // 转换为PDF
        bool exportSuccess = false;
        try {
            document->dynamicCall("ExportAsFixedFormat(const QString&, int)",
                nativePdfPath, 17);
            exportSuccess = true;
        } catch (...) {
            qDebug() << "Exception during ExportAsFixedFormat on attempt" << attempt;
        }

        document->dynamicCall("Close(bool)", false);
        delete documents;

        if (!exportSuccess) {
            if (attempt < maxRetries) {
                continue;
            }
            result.success = false;
            result.error = "Microsoft Word failed to export PDF";
            return result;
        }

        QFile pdfFile(pdfPath);
        if (!pdfFile.open(QIODevice::ReadOnly)) {
            if (attempt < maxRetries) {
                continue;
            }
            result.success = false;
            result.error = "Microsoft Word did not generate a PDF file";
            return result;
        }

        // 成功！
        result.success = true;
        result.mimeType = "application/pdf";
        result.data = pdfFile.readAll();
        
        if (attempt > 1) {
            qDebug() << "Word document converted successfully on attempt" << attempt;
        }
        return result;
    }

    // 不应该到达这里，但为了安全
    result.success = false;
    result.error = "Failed to convert Word document after multiple attempts";
    return result;
#endif
}

DocumentConverter::PreviewResult DocumentConverter::convertWordWithLibreOffice(const QString& filePath) {
    PreviewResult result;
    QString libreOffice = findLibreOffice();
    if (libreOffice.isEmpty()) {
        result.success = false;
        result.error = "LibreOffice not found on server. Install LibreOffice on computer A and ensure soffice.exe can be found.";
        return result;
    }

    QTemporaryDir tempDir(QDir::temp().filePath("crossnet_preview_XXXXXX"));
    if (!tempDir.isValid()) {
        result.success = false;
        result.error = "Failed to create temporary conversion directory";
        return result;
    }

    QTemporaryDir profileDir(QDir::temp().filePath("crossnet_lo_profile_XXXXXX"));
    if (!profileDir.isValid()) {
        result.success = false;
        result.error = "Failed to create temporary LibreOffice profile";
        return result;
    }

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("SAL_USE_VCLPLUGIN", "svp");
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(tempDir.path());

    QStringList args;
    args << "--headless"
         << "--invisible"
         << "--nologo"
         << "--nofirststartwizard"
         << "--nodefault"
         << "--nolockcheck"
         << "-env:UserInstallation=" + QUrl::fromLocalFile(profileDir.path()).toString()
         << "--convert-to"
         << "pdf"
         << "--outdir"
         << tempDir.path()
         << filePath;

    process.start(libreOffice, args);
    if (!process.waitForStarted(10000)) {
        result.success = false;
        result.error = "Failed to start LibreOffice: " + process.errorString();
        return result;
    }

    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(5000);
        result.success = false;
        result.error = withDetails("LibreOffice conversion timed out", processDetails(process));
        return result;
    }

    QString details = processDetails(process);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.success = false;
        result.error = withDetails(QString("LibreOffice conversion failed with exit code %1").arg(process.exitCode()), details);
        return result;
    }

    QString pdfPath = tempDir.path() + "/" + QFileInfo(filePath).completeBaseName() + ".pdf";
    QFile pdfFile(pdfPath);
    if (!pdfFile.open(QIODevice::ReadOnly)) {
        QStringList generatedFiles = QDir(tempDir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
        result.success = false;
        result.error = withDetails("Converted PDF file not found", "Generated files: " + generatedFiles.join(", ") + (details.isEmpty() ? QString() : "\n" + details));
        return result;
    }

    result.success = true;
    result.mimeType = "application/pdf";
    result.data = pdfFile.readAll();
    return result;
}

QString DocumentConverter::findLibreOffice() {
    QStringList candidates;
    QString found = QStandardPaths::findExecutable("soffice");
    if (!found.isEmpty()) {
        return found;
    }

    addLibreOfficeCandidate(candidates, qEnvironmentVariable("PROGRAMFILES") + "/LibreOffice/program/soffice.exe");
    addLibreOfficeCandidate(candidates, qEnvironmentVariable("PROGRAMFILES(X86)") + "/LibreOffice/program/soffice.exe");
    addLibreOfficeCandidate(candidates, qEnvironmentVariable("PROGRAMW6432") + "/LibreOffice/program/soffice.exe");
    addLibreOfficeCandidate(candidates, "C:/Program Files/LibreOffice/program/soffice.exe");
    addLibreOfficeCandidate(candidates, "C:/Program Files (x86)/LibreOffice/program/soffice.exe");

#ifdef Q_OS_WIN
    const QStringList registryKeys = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\LibreOffice\\UNO\\InstallPath",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\LibreOffice\\UNO\\InstallPath",
        "HKEY_CURRENT_USER\\SOFTWARE\\LibreOffice\\UNO\\InstallPath"
    };

    for (const QString& key : registryKeys) {
        QSettings settings(key, QSettings::NativeFormat);
        addLibreOfficeCandidate(candidates, settings.value(".").toString());
    }
#endif

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return QString();
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
        QCoreApplication::applicationDirPath() + "/aspose.lic",
        QCoreApplication::applicationDirPath() + "/1.lic",
        QCoreApplication::applicationDirPath() + "/../Aspose.Words/python专用whl包/1.lic",
        QDir::currentPath() + "/Aspose.Words/python专用whl包/1.lic",
        QDir::currentPath() + "/aspose.lic",
        QDir::currentPath() + "/1.lic"
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
#ifdef Q_OS_WIN
    QMutexLocker locker(&wordMutex);

    if (!wordApp || wordApp->isNull()) {
        qDebug() << "Microsoft Word not available for conversion";
        return QString();
    }

    // 检查是否需要预防性重启
    if (shouldReinitializeWord()) {
        qDebug() << "Proactively reinitializing Word in convertWordToJpg due to long runtime";
        if (!reinitializeWord()) {
            qDebug() << "Failed to reinitialize Word in convertWordToJpg";
            return QString();
        }
    }

    // 检查Word健康状态，如果不健康则重新初始化
    if (!isWordHealthy()) {
        qDebug() << "Word is unhealthy in convertWordToJpg, reinitializing...";
        if (!reinitializeWord()) {
            qDebug() << "Failed to reinitialize Word in convertWordToJpg";
            return QString();
        }
    }

    QDir().mkpath(outputDir);

    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QString baseName = QFileInfo(filePath).completeBaseName();
    QString outputPath = outputDir + "/" + baseName + ".png";
    QString nativeOutputPath = QDir::toNativeSeparators(outputPath);

    QAxObject* documents = wordApp->querySubObject("Documents");
    if (!documents) {
        qDebug() << "Failed to access Word Documents";
        
        // 尝试重新初始化
        if (reinitializeWord()) {
            documents = wordApp->querySubObject("Documents");
            if (!documents) {
                qDebug() << "Failed to access Word Documents after reinitialization";
                return QString();
            }
        } else {
            return QString();
        }
    }

    QAxObject* document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
        nativeInputPath, false, true, false);

    if (!document) {
        qDebug() << "Failed to open document in Word";
        delete documents;
        return QString();
    }

    // 导出高质量 PDF
    QString tempPdfPath = outputDir + "/" + baseName + "_temp.pdf";
    QString nativeTempPdfPath = QDir::toNativeSeparators(tempPdfPath);

    document->dynamicCall("ExportAsFixedFormat(const QString&, int)",
        nativeTempPdfPath, 17);

    document->dynamicCall("Close(bool)", false);
    delete documents;

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
