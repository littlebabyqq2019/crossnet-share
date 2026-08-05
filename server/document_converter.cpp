#include "document_converter.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
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
#include <memory>

namespace CrossNetShare {

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

#ifdef Q_OS_WIN
// Word 的自动化接口本质上是一个进程外 COM 服务器（WINWORD.EXE）。之前两次
// "进程内隔离"的尝试——看门狗强杀 WINWORD.EXE、把 COM 调用挪到服务端进程内
// 的专用线程——实测都会在 Word 假死/崇溃时导致服务端主进程本身毫无提示地
// 整体消失：只要 COM 调用发生在服务端自己的进程地址空间内，一次跨进程 RPC
// 失联就可能以未处理的结构化异常波及整个进程，任何线程级隔离都无法根治。
//
// 真正安全的隔离是进程级的：把"打开 Word 文档→导出 PDF"这个操作整个放进一个
// 独立的辅助程序 CrossNetShareWordHelper.exe（见 server/word_pdf_helper.cpp）
// 里执行。服务端通过 QProcess 启动它、限时等待其退出；无论它卡死、崇溃还是
// 被服务端强制终止，都只影响这一个独立的子进程，操作系统保证不会波及服务端
// 主进程的任何线程或状态。
QString wordHelperExecutablePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath("CrossNetShareWordHelper.exe");
}

// 启动辅助进程转换一次文档，最多等待 timeoutMs。
// 返回值：true 表示辅助进程在超时前正常退出（具体成功/失败还要看 errorOut
// 和调用方对输出文件的检查）；false 表示超时，此时会强制终止这个独立子进程
// ——这是完全安全的操作，因为它是一个与服务端毫无共享状态的外部进程。
bool runWordHelperProcess(const QString& inputPath, const QString& outputPdfPath,
                           int timeoutMs, QString& errorOut) {
    QString helperPath = wordHelperExecutablePath();
    if (!QFile::exists(helperPath)) {
        errorOut = "CrossNetShareWordHelper.exe not found next to the server executable";
        return false;
    }

    QProcess process;
    process.start(helperPath, {inputPath, outputPdfPath});
    if (!process.waitForStarted(10000)) {
        errorOut = "Failed to start CrossNetShareWordHelper.exe: " + process.errorString();
        return false;
    }

    if (!process.waitForFinished(timeoutMs)) {
        qDebug() << "[DocumentConverter] Word helper process did not exit within" << timeoutMs
                  << "ms, killing it (server process is unaffected)";
        process.kill();
        process.waitForFinished(5000);
        errorOut = "Microsoft Word became unresponsive while converting the document";
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit) {
        errorOut = "Word helper process crashed";
        return false;
    }

    int exitCode = process.exitCode();
    if (exitCode != 0) {
        static const QMap<int, QString> exitCodeMeanings = {
            {1, "Invalid arguments"},
            {2, "Failed to start Microsoft Word"},
            {3, "Failed to access Word Documents collection"},
            {4, "Failed to open the document"},
            {5, "Word did not generate a PDF file"},
        };
        errorOut = "Word helper process failed: " + exitCodeMeanings.value(exitCode, "Unknown error " + QString::number(exitCode));
        return false;
    }

    return true;
}
#endif

}

void DocumentConverter::initialize() {
    // Word 转换现在完全由独立的 CrossNetShareWordHelper.exe 子进程按需
    // 执行（见 runWordHelperProcess），这里不再需要预热任何东西，只确保
    // 预览缓存目录存在。

    QDir cacheDir(QDir::temp().filePath("crossnet_preview_cache"));
    if (!cacheDir.exists()) {
        cacheDir.mkpath(".");
    }
}

void DocumentConverter::cleanup() {
    // 每次转换都是一次性启动、转换、退出的独立子进程（见
    // runWordHelperProcess），没有常驻的 Word 实例需要在此关闭。
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
        return wordResult;
    }

    QFile checkFile(filePath);
    if (checkFile.open(QIODevice::ReadOnly)) {
        QByteArray header = checkFile.read(512);
        if (header.contains("<?xml") && header.contains("wordDocument")) {
            return wordResult;
        }
    }

    PreviewResult libreOfficeResult = convertWordWithLibreOffice(filePath);
    if (libreOfficeResult.success) {
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::WriteOnly)) {
            cacheFile.write(libreOfficeResult.data);
            cacheFile.close();
        }
    } else if (!wordResult.error.isEmpty()) {
        libreOfficeResult.error = "Microsoft Word conversion failed: " + wordResult.error + "\nLibreOffice fallback failed: " + libreOfficeResult.error;
    }
    return libreOfficeResult;
}

DocumentConverter::PreviewResult DocumentConverter::convertWordWithMicrosoftWord(const QString& filePath) {
    PreviewResult result;
#ifndef Q_OS_WIN
    result.success = false;
    result.error = "Microsoft Word COM conversion is only available on Windows";
    return result;
#else
    QTemporaryDir tempDir(QDir::temp().filePath("crossnet_word_preview_XXXXXX"));
    if (!tempDir.isValid()) {
        result.success = false;
        result.error = "Failed to create temporary Word conversion directory";
        return result;
    }

    QString pdfPath = tempDir.path() + "/" + QFileInfo(filePath).completeBaseName() + ".pdf";

    // 转换完全在独立子进程 CrossNetShareWordHelper.exe 里进行，详见文件顶部
    // runWordHelperProcess() 的说明。这里的服务端线程只是启动它、限时等待。
    QString helperError;
    bool completed = runWordHelperProcess(filePath, pdfPath, 30000, helperError);

    if (!completed) {
        result.success = false;
        result.error = helperError;
        return result;
    }

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
    QDir().mkpath(outputDir);

    QString baseName = QFileInfo(filePath).completeBaseName();
    QString tempPdfPath = outputDir + "/" + baseName + "_temp.pdf";

    // 转换完全在独立子进程 CrossNetShareWordHelper.exe 里进行，详见文件顶部
    // runWordHelperProcess() 的说明。
    QString helperError;
    bool completed = runWordHelperProcess(filePath, tempPdfPath, 30000, helperError);

    if (!completed) {
        qDebug() << "Word conversion failed:" << helperError;
        return QString();
    }

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
