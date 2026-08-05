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
#include <QMutex>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#ifdef Q_OS_WIN
#include <QAxObject>
#include <objbase.h>
#endif

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
// Word.Application 的全部生命周期都被限定在一个专用的工作线程内部：
// 该线程创建自己的 Word 实例，逐个执行提交给它的任务，任务闭包只在工作
// 线程实际执行时才拿到 QAxObject* 指针（作为参数传入），调用方（网络线程）
// 提交任务时完全不知道、也不需要碰这个指针本身。
//
// 如果 Word 在某次任务中假死，工作线程会永久卡在那次 COM 调用里——但这只会
// 拖死这一个线程和它对应的 WINWORD.EXE 进程，不会影响任何其他功能（网页请求、
// 文件下载、设置界面等全部继续正常工作）。调用方等待超时后会立刻收到错误，
// 并将这个工作线程标记为"已作废"；下一次转换请求会启动一个全新的工作线程和
// 全新的 Word 实例。旧的工作线程和它卡住的 WINWORD.EXE 进程被直接放弃
// （不再有任何代码引用或操作它们），从根本上避免了跨线程强杀进程可能引发的崇溃。
class WordWorker {
public:
    using Job = std::function<void(QAxObject*)>;

    WordWorker() {
        thread_ = std::thread([this]() { threadMain(); });
    }

    // 工作线程一旦启动就永不主动停止（即使外部不再使用它），
    // 因为如果它当前正卡在一次 COM 调用里，任何尝试 join 的操作本身也会被
    // 阻塞。直接 detach，随进程退出而结束即可，代价是可能残留一个僵死的
    // WINWORD.EXE 进程，需要靠重启服务端来清理。
    ~WordWorker() {
        thread_.detach();
    }

    // 提交一个任务并等待最多 timeoutMs 毫秒。同一个 WordWorker 实例上的
    // 多次调用会被互相串行化，不会并发提交多个任务。
    // 返回 true 表示任务在超时前已经执行完成；返回 false 表示超时——
    // 此时任务可能仍在工作线程里挂着，这个 WordWorker 实例应被视为已作废。
    bool run(Job job, int timeoutMs) {
        std::lock_guard<std::mutex> submitLock(submitMutex_);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingJob_ = std::move(job);
            jobDone_ = false;
        }
        jobReady_.notify_one();

        std::unique_lock<std::mutex> lock(mutex_);
        return doneCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() { return jobDone_; });
    }

private:
    void threadMain() {
        // 这是一个全新的、Qt 未曾管理过的原生线程，必须先手动初始化 COM
        // 套间，否则下面创建 QAxObject 会直接失败。使用 STA（单线程套间）
        // 与 Word 这种进程外 COM 服务器的推荐使用方式保持一致。
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        QAxObject* wordApp = new QAxObject("Word.Application");
        if (!wordApp->isNull()) {
            wordApp->setProperty("Visible", false);
            wordApp->setProperty("DisplayAlerts", 0);
        }

        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                jobReady_.wait(lock, [this]() { return static_cast<bool>(pendingJob_); });
                job = std::move(pendingJob_);
                pendingJob_ = nullptr;
            }

            // job() 在这里可能永久阻塞（Word 假死）。如果发生，这个线程就
            // 永远停在这一行，再也不会回到循环顶部——这正是预期行为：
            // 外部的 run() 调用早已因超时返回，本线程和它持有的 wordApp
            // 已被上层逻辑放弃，不会再有任何代码等待或依赖这次调用的结果。
            job(wordApp);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                jobDone_ = true;
            }
            doneCv_.notify_one();
        }
    }

    std::thread thread_;
    std::mutex submitMutex_;
    std::mutex mutex_;
    std::condition_variable jobReady_;
    std::condition_variable doneCv_;
    Job pendingJob_;
    bool jobDone_ = false;
};

// 当前有效的工作线程，用裸指针管理，而非 shared_ptr/unique_ptr。
//
// 这是有意为之：一旦某次调用超时，我们只是把这个指针替换为一个全新实例，
// 旧对象绝不会被 delete——因为它的线程可能仍卡在一次 COM 调用里，
// 一旦最终从卡住状态返回（比如 Word 自己崇溃退出），仍会访问自己的
// mutex_/doneCv_ 等成员。销毁一个其线程仍可能运行的对象是释放后使用的
// 未定义行为；而放着不管，最坏后果只是泄漏一小块内存和一个线程句柄，
// 随进程退出一起被系统回收，代价远小于一次崇溃。
WordWorker* g_wordWorker = nullptr;
QMutex g_wordWorkerMutex;

WordWorker* getOrCreateWordWorker() {
    QMutexLocker locker(&g_wordWorkerMutex);
    if (!g_wordWorker) {
        g_wordWorker = new WordWorker();
    }
    return g_wordWorker;
}

void discardWordWorker(WordWorker* worker) {
    QMutexLocker locker(&g_wordWorkerMutex);
    if (g_wordWorker == worker) {
        // 只是放弃这个指针，绝不 delete——参见上面的说明。
        g_wordWorker = nullptr;
    }
}

// 在专用 Word 工作线程上执行 job，最多等待 timeoutMs。
// 返回 true 表示成功在超时前完成；返回 false 表示超时，此时已自动
// 丢弃这个作废的工作线程，下一次调用会透明地创建一个全新的实例。
bool runOnWordWorker(const WordWorker::Job& job, int timeoutMs = 30000) {
    WordWorker* worker = getOrCreateWordWorker();
    bool completed = worker->run(job, timeoutMs);
    if (!completed) {
        qDebug() << "[DocumentConverter] Word worker did not respond within" << timeoutMs
                  << "ms, discarding it - a fresh Word instance will be used for future requests";
        discardWordWorker(worker);
    }
    return completed;
}
#endif

}

void DocumentConverter::initialize() {
    // Word.Application 现在按需在专用工作线程内延迟创建（见 runOnWordWorker），
    // 这里不再需要预热，只确保预览缓存目录存在。

    QDir cacheDir(QDir::temp().filePath("crossnet_preview_cache"));
    if (!cacheDir.exists()) {
        cacheDir.mkpath(".");
    }
}

void DocumentConverter::cleanup() {
#ifdef Q_OS_WIN
    // 只在工作线程存在、且此刻处于空闲（未卡死）状态时才礼貌地调用 Quit()。
    // 如果它正卡在上一次任务里，短暂等待后直接放弃——反正进程马上就要退出了，
    // 残留的 WINWORD.EXE 会成为孤儿进程，用户可在任务管理器里清理。
    runOnWordWorker([](QAxObject* wordApp) {
        if (wordApp && !wordApp->isNull()) {
            wordApp->dynamicCall("Quit()");
        }
    }, 5000);
#endif
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
    auto tempDir = std::make_shared<QTemporaryDir>(QDir::temp().filePath("crossnet_word_preview_XXXXXX"));
    if (!tempDir->isValid()) {
        result.success = false;
        result.error = "Failed to create temporary Word conversion directory";
        return result;
    }

    auto pdfPath = std::make_shared<QString>(tempDir->path() + "/" + QFileInfo(filePath).completeBaseName() + ".pdf");
    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QString nativePdfPath = QDir::toNativeSeparators(*pdfPath);

    // 整个"打开-导出-关闭"序列作为单个任务提交给工作线程。
    // 所有输入参数按值捕获，任务内部只使用本地拷贝，绝不引用调用方栈上的
    // 任何对象——这样即使调用方因超时提前返回、栈帧被销毁，工作线程里仍在
    // 运行的任务也不会访问到悬空引用。tempDir 用 shared_ptr 按值捕获以延长
    // 其生命周期：只要任务闭包还持有它，临时目录就不会被提前删除。
    auto jobError = std::make_shared<QString>();
    auto jobSuccess = std::make_shared<bool>(false);

    bool completed = runOnWordWorker([tempDir, nativeInputPath, nativePdfPath, jobError, jobSuccess](QAxObject* wordApp) {
        if (!wordApp || wordApp->isNull()) {
            *jobError = "Microsoft Word failed to start";
            return;
        }

        QAxObject* documents = wordApp->querySubObject("Documents");
        if (!documents) {
            *jobError = "Failed to access Microsoft Word Documents collection";
            return;
        }

        QAxObject* document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
            nativeInputPath, false, true, false);
        delete documents;

        if (!document) {
            *jobError = "Microsoft Word failed to open the document";
            return;
        }

        document->dynamicCall("ExportAsFixedFormat(const QString&, int)", nativePdfPath, 17);
        document->dynamicCall("Close(bool)", false);
        delete document;

        *jobSuccess = true;
    }, 30000);

    if (!completed) {
        result.success = false;
        result.error = "Microsoft Word became unresponsive while converting the document";
        return result;
    }

    if (!*jobSuccess) {
        result.success = false;
        result.error = jobError->isEmpty() ? "Microsoft Word conversion failed" : *jobError;
        return result;
    }

    QFile pdfFile(*pdfPath);
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

    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QString baseName = QFileInfo(filePath).completeBaseName();
    auto tempPdfPath = std::make_shared<QString>(outputDir + "/" + baseName + "_temp.pdf");
    QString nativeTempPdfPath = QDir::toNativeSeparators(*tempPdfPath);

    // 同样把"打开-导出-关闭"打包为单个任务，所有参数按值捕获，
    // 详见 convertWordWithMicrosoftWord() 中的说明。
    auto jobError = std::make_shared<QString>();
    auto jobSuccess = std::make_shared<bool>(false);

    bool completed = runOnWordWorker([nativeInputPath, nativeTempPdfPath, jobError, jobSuccess](QAxObject* wordApp) {
        if (!wordApp || wordApp->isNull()) {
            *jobError = "Microsoft Word failed to start";
            return;
        }

        QAxObject* documents = wordApp->querySubObject("Documents");
        if (!documents) {
            *jobError = "Failed to access Microsoft Word Documents collection";
            return;
        }

        QAxObject* document = documents->querySubObject("Open(const QString&, bool, bool, bool)",
            nativeInputPath, false, true, false);
        delete documents;

        if (!document) {
            *jobError = "Microsoft Word failed to open the document";
            return;
        }

        document->dynamicCall("ExportAsFixedFormat(const QString&, int)", nativeTempPdfPath, 17);
        document->dynamicCall("Close(bool)", false);
        delete document;

        *jobSuccess = true;
    }, 30000);

    if (!completed) {
        qDebug() << "Microsoft Word became unresponsive while converting the document";
        return QString();
    }

    if (!*jobSuccess) {
        qDebug() << "Word conversion failed:" << *jobError;
        return QString();
    }

    if (!QFile::exists(*tempPdfPath)) {
        qDebug() << "Failed to generate PDF";
        return QString();
    }

    return *tempPdfPath;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(outputDir);
    return QString();
#endif
}

}
