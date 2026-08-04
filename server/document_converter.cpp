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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef Q_OS_WIN
#include <QAxObject>
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace CrossNetShare {

#ifdef Q_OS_WIN
QAxObject* DocumentConverter::wordApp = nullptr;
QMutex DocumentConverter::wordMutex;
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
        runWordActionWithWatchdog([]() {
            DocumentConverter::wordApp->dynamicCall("Quit()");
        });
        delete wordApp;
        wordApp = nullptr;
    }
#endif
}

#ifdef Q_OS_WIN
void DocumentConverter::restartWordApp() {
    if (wordApp) {
        // 旧对象可能已经处于异常状态。Quit() 本身也可能挂死，
        // 因此同样放在看门狗保护下执行，忽略任何失败。
        runWordActionWithWatchdog([]() {
            DocumentConverter::wordApp->dynamicCall("Quit()");
        });
        delete wordApp;
        wordApp = nullptr;
    }

    // 强杀残留的 WINWORD.EXE 进程，确保接下来创建的是一个全新、干净的实例，
    // 不会意外附着到一个仍处于异常状态的旧进程上。
    killWordProcess();

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
    bool alive = false;
    bool watchdogOk = runWordActionWithWatchdog([&alive]() {
        QAxObject* documents = DocumentConverter::wordApp->querySubObject("Documents");
        alive = documents != nullptr;
        delete documents;
    });

    if (!watchdogOk || !alive || !wordApp || wordApp->isNull()) {
        qDebug() << "[DocumentConverter] Word COM connection appears dead, restarting Word.Application";
        restartWordApp();
        return wordApp && !wordApp->isNull();
    }

    return true;
}

void DocumentConverter::killWordProcess() {
    // 只终止 WINWORD.EXE 进程本身，绝不在这里触碰 wordApp 指针——
    // 该指针始终只由持有 wordMutex 的调用线程管理，避免两个线程
    // 同时操作/释放同一个 QAxObject 造成的数据竞争。
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, "WINWORD.EXE") == 0) {
                HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (process) {
                    TerminateProcess(process, 1);
                    CloseHandle(process);
                }
            }
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
}

bool DocumentConverter::runWordActionWithWatchdog(const std::function<void()>& action, int timeoutMs) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    auto killedByWatchdog = std::make_shared<std::atomic<bool>>(false);
    auto mutex = std::make_shared<std::mutex>();
    auto cv = std::make_shared<std::condition_variable>();

    // action 始终在当前线程（持有 wordMutex 的调用线程）同步执行；
    // 看门狗线程只负责计时和在超时后强杀 WINWORD.EXE 进程本身。
    // 一旦目标进程被杀死，卡在 IDispatch::Invoke 里的 COM RPC 调用会因为
    // 服务端进程消失而返回错误，从而让 action() 在调用线程上正常返回，
    // 而不是永久阻塞——这样就不会有两个线程同时访问同一个 QAxObject。
    std::thread watchdog([done, killedByWatchdog, mutex, cv, timeoutMs]() {
        std::unique_lock<std::mutex> lock(*mutex);
        if (!cv->wait_for(lock, std::chrono::milliseconds(timeoutMs), [done]() { return done->load(); })) {
            qDebug() << "[DocumentConverter] Word COM call exceeded" << timeoutMs << "ms, force-killing WINWORD.EXE";
            killedByWatchdog->store(true);
            DocumentConverter::killWordProcess();
        }
    });

    action();

    {
        std::lock_guard<std::mutex> lock(*mutex);
        done->store(true);
    }
    cv->notify_all();
    watchdog.join();

    if (killedByWatchdog->load()) {
        // 进程已被强杀，wordApp 包装的 COM 连接必然失效。
        // 此刻已经脱离 action() 的执行、回到调用线程的正常控制流，
        // 在这里安全地丢弃旧指针，避免悬挂引用。
        delete wordApp;
        wordApp = nullptr;
        return false;
    }

    return wordApp && !wordApp->isNull();
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

    QAxObject* documents = nullptr;
    runWordActionWithWatchdog([&]() {
        documents = wordApp->querySubObject("Documents");
    });
    if (!documents) {
        // 再尝试一次重启后重试，应对 Documents 集合突然失效的情况
        restartWordApp();
        if (wordApp && !wordApp->isNull()) {
            runWordActionWithWatchdog([&]() {
                documents = wordApp->querySubObject("Documents");
            });
        }
    }
    if (!documents) {
        result.success = false;
        result.error = "Failed to access Microsoft Word Documents collection";
        return result;
    }

    QAxObject* document = nullptr;
    bool timedOut = !runWordActionWithWatchdog([&]() {
        document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
            nativeInputPath, false, true, false);
    });
    delete documents;

    if (timedOut) {
        delete document;
        result.success = false;
        result.error = "Microsoft Word became unresponsive while opening the document";
        return result;
    }

    if (!document) {
        result.success = false;
        result.error = "Microsoft Word failed to open the document";
        return result;
    }

    timedOut = !runWordActionWithWatchdog([&]() {
        document->dynamicCall("ExportAsFixedFormat(const QString&, int)",
            nativePdfPath, 17);
        document->dynamicCall("Close(bool)", false);
    });
    delete document;

    if (timedOut) {
        result.success = false;
        result.error = "Microsoft Word became unresponsive while exporting the document";
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
    QMutexLocker locker(&wordMutex);

    if (!ensureWordAppReady()) {
        qDebug() << "Microsoft Word not available for conversion";
        return QString();
    }

    QDir().mkpath(outputDir);

    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QString baseName = QFileInfo(filePath).completeBaseName();
    QString outputPath = outputDir + "/" + baseName + ".png";
    QString nativeOutputPath = QDir::toNativeSeparators(outputPath);

    QAxObject* documents = nullptr;
    runWordActionWithWatchdog([&]() {
        documents = wordApp->querySubObject("Documents");
    });
    if (!documents) {
        // 再尝试一次重启后重试
        restartWordApp();
        if (wordApp && !wordApp->isNull()) {
            runWordActionWithWatchdog([&]() {
                documents = wordApp->querySubObject("Documents");
            });
        }
    }
    if (!documents) {
        qDebug() << "Failed to access Word Documents";
        return QString();
    }

    QAxObject* document = nullptr;
    bool timedOut = !runWordActionWithWatchdog([&]() {
        document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
            nativeInputPath, false, true, false);
    });
    delete documents;

    if (timedOut) {
        delete document;
        qDebug() << "Microsoft Word became unresponsive while opening the document";
        return QString();
    }

    if (!document) {
        qDebug() << "Failed to open document in Word";
        return QString();
    }

    // 导出高质量 PDF
    QString tempPdfPath = outputDir + "/" + baseName + "_temp.pdf";
    QString nativeTempPdfPath = QDir::toNativeSeparators(tempPdfPath);

    timedOut = !runWordActionWithWatchdog([&]() {
        document->dynamicCall("ExportAsFixedFormat(const QString&, int)",
            nativeTempPdfPath, 17);
        document->dynamicCall("Close(bool)", false);
    });
    delete document;

    if (timedOut) {
        qDebug() << "Microsoft Word became unresponsive while exporting the document";
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
