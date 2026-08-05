// CrossNetShareWordHelper —— 独立的 Word→PDF 转换辅助程序。
//
// 为什么需要单独一个可执行文件，而不是在服务端进程内直接调用 Word COM：
// Word 的自动化接口本质上是一个进程外 COM 服务器（WINWORD.EXE）。如果它在
// 某次调用中假死或崩溃，之前两种"进程内隔离"方案（看门狗强杀 WINWORD.EXE、
// 专用工作线程）都观察到会让服务端主进程本身毫无提示地整体消失——无论隔离
// 得多干净，只要 Word COM 调用发生在服务端自己的进程地址空间内，一次跨进程
// RPC 失联就有可能以未处理的结构化异常的形式波及整个进程。
//
// 真正安全的做法是让 Word COM 调用完全发生在另一个独立进程里：本程序单次
// 运行只做一件事——把一个 Word 文档转换为 PDF，然后退出。服务端通过 QProcess
// 启动它、传入输入/输出路径、限时等待。如果它卡死，服务端只需要终止这一个
// 外部子进程（这是操作系统级别的正常操作，与服务端自身的线程/COM 状态毫无
// 关系），服务端主进程永远不受影响。
//
// 用法：CrossNetShareWordHelper.exe <输入文档路径> <输出PDF路径>
// 退出码：0 = 成功；非 0 = 失败（具体含义见下方各返回点的注释）。

#include <QAxObject>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <windows.h>
#include <shellapi.h>

namespace {

// 这是一个 WIN32 子系统的 GUI 程序（无控制台），qDebug() 默认只写到调试器，
// 用户和服务端都看不到。之前两次修复都是在完全看不到本进程内部实际执行到
// 哪一步的情况下做的猜测——为了不再猜测，把每一步都追加写入一个固定的日志
// 文件，下次失败时直接读这个文件就能看到确切死在哪一行。
QFile* g_logFile = nullptr;

void logStep(const QString& message) {
    if (!g_logFile) {
        g_logFile = new QFile(QDir::temp().filePath("crossnet_word_helper.log"));
        g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    if (g_logFile->isOpen()) {
        QTextStream stream(g_logFile);
        stream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") << " " << message << "\n";
        stream.flush();
        g_logFile->flush();
    }
}

}

int main(int argc, char* argv[]) {
    logStep(QString("=== helper starting, pid=%1 ===").arg(GetCurrentProcessId()));

    QApplication app(argc, argv);
    logStep("QApplication constructed");

    // 不使用 CRT 自带的 narrow argv：它是把宽字符命令行按系统 ANSI 代码页
    // 转换而来的，对不在该代码页范围内的文件名会造成乱码/截断。这里直接从
    // 原始宽字符命令行重新解析参数，确保任意 Unicode 路径都能被正确还原。
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    QString inputPath;
    QString outputPdfPath;
    bool argsOk = wargv && wargc >= 3;
    if (argsOk) {
        inputPath = QString::fromWCharArray(wargv[1]);
        outputPdfPath = QString::fromWCharArray(wargv[2]);
    }
    if (wargv) {
        LocalFree(wargv);
    }
    if (!argsOk) {
        logStep("FAILED: invalid arguments");
        return 1; // 参数不足
    }
    logStep("args parsed: input=" + inputPath + " output=" + outputPdfPath);

    // Word 自动化调用推迟到事件循环已经运行起来之后才发起（用
    // QTimer::singleShot(0, ...)），而不是在 app.exec() 之前于 main() 里
    // 同步调用。Word 是进程外 COM 服务器，在执行较慢操作时可能通过 COM
    // 消息过滤器/重入回调与发起调用的 STA 线程通信，这要求该线程此刻有一个
    // 正在运行的消息循环来处理这些往返消息。
    QTimer::singleShot(0, &app, [&app, inputPath, outputPdfPath]() {
        QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(inputPath).absoluteFilePath());
        QString nativeOutputPath = QDir::toNativeSeparators(QFileInfo(outputPdfPath).absoluteFilePath());

        logStep("creating Word.Application COM object...");
        QAxObject wordApp("Word.Application");
        logStep(QString("Word.Application created, isNull=%1").arg(wordApp.isNull()));
        if (wordApp.isNull()) {
            app.exit(2); // 无法启动/连接 Microsoft Word
            return;
        }
        wordApp.setProperty("Visible", false);
        wordApp.setProperty("DisplayAlerts", 0);
        logStep("Word.Application configured (Visible=false, DisplayAlerts=0)");

        logStep("querying Documents collection...");
        QAxObject* documents = wordApp.querySubObject("Documents");
        logStep(QString("Documents collection query returned %1").arg(documents ? "non-null" : "null"));
        if (!documents) {
            wordApp.dynamicCall("Quit()");
            app.exit(3); // 无法访问 Documents 集合
            return;
        }

        logStep("calling Documents.Open(\"" + nativeInputPath + "\")...");
        QAxObject* document = documents->querySubObject(
            "Open(const QString&, bool, bool, bool)",
            nativeInputPath, false, true, false);
        logStep(QString("Documents.Open returned %1").arg(document ? "non-null" : "null"));
        delete documents;

        if (!document) {
            wordApp.dynamicCall("Quit()");
            app.exit(4); // 打开文档失败
            return;
        }

        logStep("calling ExportAsFixedFormat to \"" + nativeOutputPath + "\"...");
        // 17 = wdExportFormatPDF
        document->dynamicCall("ExportAsFixedFormat(const QString&, int)", nativeOutputPath, 17);
        logStep("ExportAsFixedFormat returned, calling Close...");
        document->dynamicCall("Close(bool)", false);
        logStep("Close returned");
        delete document;

        logStep("calling Word Quit...");
        wordApp.dynamicCall("Quit()");
        logStep("Quit returned");

        bool pdfExists = QFile::exists(outputPdfPath);
        logStep(QString("PDF exists=%1, exiting with code %2").arg(pdfExists).arg(pdfExists ? 0 : 5));
        app.exit(pdfExists ? 0 : 5); // 5 = 导出后未生成 PDF 文件
    });

    int rc = app.exec();
    logStep(QString("app.exec() returned %1, process exiting").arg(rc));
    return rc;
}
