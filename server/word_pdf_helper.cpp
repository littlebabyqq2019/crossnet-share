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
#include <QTimer>
#include <windows.h>
#include <shellapi.h>

namespace {

// 这是一个 WIN32 子系统的 GUI 程序（无控制台），qDebug() 默认只写到调试器，
// 用户和服务端都看不到。之前的修复都是在完全看不到本进程内部实际执行到
// 哪一步的情况下做的推断——为了不再猜测，把每一步都追加写入一个固定的日志
// 文件，下次失败时直接读这个文件就能看到确切死在哪一行。
QFile* g_logFile = nullptr;

void logStep(const QString& message) {
    if (!g_logFile) {
        g_logFile = new QFile(QDir::temp().filePath("crossnet_word_helper.log"));
        g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    if (g_logFile->isOpen()) {
        // 显式按 UTF-8 写入字节，不依赖 QTextStream 的默认本地区域码页
        // 编码（在中文 Windows 上通常是 GBK）——服务端用 QString::fromUtf8()
        // 读取这个文件，两端编码必须一致，否则中文文件名会显示为乱码。
        QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") + " " + message + "\n";
        g_logFile->write(line.toUtf8());
        g_logFile->flush();
    }
}

// MSVC 内部用这个"魔术"异常码实现 C++ 的 throw/catch。如果放行到这里的是
// 这个码，说明是一次真正的 C++ 异常（比如 std::bad_alloc），应该让它继续
// 按正常的 C++ 异常机制传播，而不是被我们当作硬件异常吞掉、隐藏真实问题。
int sehExceptionFilter(unsigned int code) {
    const unsigned int kMsvcCppExceptionCode = 0xE06D7363;
    if (code == kMsvcCppExceptionCode) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// 用结构化异常处理（SEH）包裹一次可能触发硬件级异常（例如访问越界）的调用。
// COM 自动化对象内部的崩溃常以这种方式传播，普通的 C++ try/catch 完全捕获
// 不到。这样可以把它转化为一次"看得见"的失败：记录异常码并走正常的错误
// 处理分支退出，而不是被操作系统直接判定为进程崩溃、悄无声息地消失。
//
// 返回 true 表示 func() 正常执行完成；返回 false 表示捕获到了一次结构化
// 异常（exceptionCodeOut 会被设为具体的异常码）。
//
// 注意：这个函数本身刻意不包含任何需要栈展开的 C++ 对象（局部 QString 等）
// ——__try 块所在的函数如果有这类局部对象会触发 MSVC C2712 编译错误。
// func 是调用方传入的、只捕获简单值/指针的 lambda，它的调用是一次普通的
// 嵌套函数调用，其内部创建的 C++ 对象位于 func 自己的栈帧里，与此处无关。
template <typename Func>
bool runGuarded(Func&& func, unsigned long& exceptionCodeOut) {
    __try {
        func();
        return true;
    } __except (sehExceptionFilter(GetExceptionCode())) {
        exceptionCodeOut = static_cast<unsigned long>(GetExceptionCode());
        return false;
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

        unsigned long sehCode = 0;

        logStep("creating Word.Application COM object...");
        QAxObject* wordApp = nullptr;
        bool ok = runGuarded([&wordApp]() {
            wordApp = new QAxObject("Word.Application");
        }, sehCode);
        if (!ok) {
            logStep(QString("CRASH: structured exception 0x%1 while creating Word.Application").arg(sehCode, 0, 16));
            app.exit(9);
            return;
        }
        logStep(QString("Word.Application created, isNull=%1").arg(wordApp->isNull()));
        if (wordApp->isNull()) {
            delete wordApp;
            app.exit(2); // 无法启动/连接 Microsoft Word
            return;
        }
        wordApp->setProperty("Visible", false);
        wordApp->setProperty("DisplayAlerts", 0);
        logStep("Word.Application configured (Visible=false, DisplayAlerts=0)");

        logStep("querying Documents collection...");
        QAxObject* documents = nullptr;
        ok = runGuarded([&]() {
            documents = wordApp->querySubObject("Documents");
        }, sehCode);
        if (!ok) {
            logStep(QString("CRASH: structured exception 0x%1 while querying Documents").arg(sehCode, 0, 16));
            delete wordApp;
            app.exit(9);
            return;
        }
        logStep(QString("Documents collection query returned %1").arg(documents ? "non-null" : "null"));
        if (!documents) {
            wordApp->dynamicCall("Quit()");
            delete wordApp;
            app.exit(3); // 无法访问 Documents 集合
            return;
        }

        logStep("calling Documents.Open(\"" + nativeInputPath + "\")...");
        QAxObject* document = nullptr;
        ok = runGuarded([&]() {
            document = documents->querySubObject(
                "Open(const QString&, bool, bool, bool)",
                nativeInputPath, false, true, false);
        }, sehCode);
        delete documents;
        if (!ok) {
            logStep(QString("CRASH: structured exception 0x%1 while opening the document").arg(sehCode, 0, 16));
            wordApp->dynamicCall("Quit()");
            delete wordApp;
            app.exit(9);
            return;
        }
        logStep(QString("Documents.Open returned %1").arg(document ? "non-null" : "null"));

        if (!document) {
            wordApp->dynamicCall("Quit()");
            delete wordApp;
            app.exit(4); // 打开文档失败
            return;
        }

        // Document.ExportAsFixedFormat 的完整参数列表（除末尾 Object 类型的
        // FixedFormatExtClassPtr 外）。之前只传了前 2 个参数、其余全部依赖
        // Word 的可选参数默认值——这是通过 IDispatch::Invoke 晚绑定调用该
        // 方法时一个有据可查的已知隐患：Word 对该方法可选参数的晚绑定处理
        // 不够健壮，省略参数在某些环境下会导致 Word 自动化层内部崩溃
        // （与本次实测的崩溃位置完全一致）。显式传入全部参数是文档化的
        // 规避方式：
        //   ExportFormat=17 (wdExportFormatPDF)
        //   OpenAfterExport=false
        //   OptimizeFor=0   (wdExportOptimizeForPrint)
        //   Range=0         (wdExportAllDocument)
        //   From=1, To=1    (Range 为整份文档时被忽略，但仍须提供取值)
        //   Item=0          (wdExportDocumentContent)
        //   IncludeDocProps=true
        //   KeepIRM=true
        //   CreateBookmarks=0 (wdExportCreateNoBookmarks)
        //   DocStructureTags=true
        //   BitmapMissingFonts=true
        //   UseISO19005_1=false（不强制 PDF/A，保持导出效果与之前一致）
        logStep("calling ExportAsFixedFormat to \"" + nativeOutputPath + "\"...");
        ok = runGuarded([&]() {
            document->dynamicCall(
                "ExportAsFixedFormat(const QString&, int, bool, int, int, int, int, int, bool, bool, int, bool, bool, bool)",
                nativeOutputPath, 17, false, 0, 0, 1, 1, 0, true, true, 0, true, true, false);
        }, sehCode);
        if (!ok) {
            logStep(QString("CRASH: structured exception 0x%1 during ExportAsFixedFormat").arg(sehCode, 0, 16));
            delete document;
            delete wordApp;
            app.exit(9);
            return;
        }
        logStep("ExportAsFixedFormat returned, calling Close...");

        ok = runGuarded([&]() {
            document->dynamicCall("Close(bool)", false);
        }, sehCode);
        if (!ok) {
            logStep(QString("CRASH: structured exception 0x%1 while closing the document").arg(sehCode, 0, 16));
            delete document;
            delete wordApp;
            app.exit(9);
            return;
        }
        logStep("Close returned");
        delete document;

        logStep("calling Word Quit...");
        ok = runGuarded([&]() {
            wordApp->dynamicCall("Quit()");
        }, sehCode);
        if (!ok) {
            logStep(QString("CRASH: structured exception 0x%1 during Quit").arg(sehCode, 0, 16));
            // 导出很可能已经在 Quit 之前完成，仍按文件是否存在来判定结果，
            // 不因为退出这一步崩溃就把已经成功的转换判定为失败。
        } else {
            logStep("Quit returned");
        }
        delete wordApp;

        bool pdfExists = QFile::exists(outputPdfPath);
        logStep(QString("PDF exists=%1, exiting with code %2").arg(pdfExists).arg(pdfExists ? 0 : 5));
        app.exit(pdfExists ? 0 : 5); // 5 = 导出后未生成 PDF 文件
    });

    int rc = app.exec();
    logStep(QString("app.exec() returned %1, process exiting").arg(rc));
    return rc;
}
