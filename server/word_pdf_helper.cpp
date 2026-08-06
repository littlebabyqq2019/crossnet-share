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
#include <QList>
#include <QTimer>
#include <QVariant>
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <shellapi.h>

namespace {

// logStep() 和崩溃处理路径都要写同一个物理文件，用一份共享的、程序启动时
// 一次性计算好的路径，避免两处各自拼路径时出现不一致。
QString g_logFilePath;
QByteArray g_logFilePathUtf8; // CreateFileA 需要的窄字符（UTF-8）版本

// 这是一个 WIN32 子系统的 GUI 程序（无控制台），qDebug() 默认只写到调试器，
// 用户和服务端都看不到。之前的修复都是在完全看不到本进程内部实际执行到
// 哪一步的情况下做的推断——为了不再猜测，把每一步都追加写入一个固定的日志
// 文件，下次失败时直接读这个文件就能看到确切死在哪一行。
QFile* g_logFile = nullptr;

void logStep(const QString& message) {
    if (!g_logFile) {
        g_logFile = new QFile(g_logFilePath);
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
// 不到。
//
// 极其重要：一旦这里真的捕获到异常，就意味着底层 COM/RPC 通道大概率已经
// 处于损坏状态。已用查证过的 Qt 源码证实——QAxObject 的析构函数
// （QAxBase::clear()）会调用 IDispatch::Release()/IUnknown::Release()，
// 而 querySubObject() 创建的每个子对象都以调用者为 QObject parent，所以
// 哪怕调用方完全不写 delete，父对象（wordApp/document）析构时 Qt 也会
// 级联 delete 所有子对象、从而级联触发这些 Release() 调用。对一个已经在
// 硬件异常中损坏的 COM 接口再发出任何调用（包括这个隐式 Release），都有
// 很高概率再次触发同样的硬件级异常——而这一次没有 __try 保护，会被系统
// 当作真正的未处理异常直接杀掉整个进程。实测现象完全吻合：日志显示第
// 一次异常被正确捕获并记录，但进程仍然以 QProcess::CrashExit 报告退出，
// 说明是在捕获之后的清理代码（delete document/wordApp）里发生了第二次、
// 未受保护的崩溃。
//
// 因此这个函数不会返回、不会走"记录日志→delete→app.exit()"的常规清理
// 流程——一旦捕获到异常，直接在 __except 块内部把日志写完，然后调用
// TerminateProcess(GetCurrentProcess(), ...) 立即终止自身进程。这是
// Windows 上唯一不经过任何 C++/Qt 析构链、不触碰任何 COM 对象、直接由
// 内核强制回收资源的退出方式——绝不能换成 exit()/_exit()/app.exit()，
// 那些都会继续执行当前函数栈上剩余对象的析构。
//
// 注意：__try 块所在的函数不能包含需要栈展开的 C++ 局部对象（局部 QString
// 等），否则会触发 MSVC C2712 编译错误——这里全程只用 char 数组和整型，
// 符合这个限制。
template <typename Func>
void runGuardedOrDie(Func&& func, const char* stepDescription) {
    __try {
        func();
    } __except (sehExceptionFilter(GetExceptionCode())) {
        unsigned long code = static_cast<unsigned long>(GetExceptionCode());
        // 直接用 Win32 API 打开、追加、关闭日志文件，完全不经过 QFile/
        // QString——同样是为了不在这个异常处理路径上创建任何需要栈展开/
        // 析构的 C++ 对象。用带缓冲区大小的 sprintf_s（标准 CRT 安全扩展，
        // 无需链接 user32.lib），不用 wsprintfA——后者是文档标注为不推荐
        // 使用的旧 API，没有缓冲区大小检查。
        char buf[256];
        sprintf_s(buf, sizeof(buf),
                  "CRASH: structured exception 0x%08lX during %s - terminating process immediately (no further COM calls, no destructors)\r\n",
                  code, stepDescription);
        HANDLE logHandle = CreateFileA(g_logFilePathUtf8.constData(), FILE_APPEND_DATA,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logHandle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(logHandle, buf, static_cast<DWORD>(strlen(buf)), &written, nullptr);
            CloseHandle(logHandle);
        }
        TerminateProcess(GetCurrentProcess(), 9);
    }
}

}

int main(int argc, char* argv[]) {
    g_logFilePath = QDir::temp().filePath("crossnet_word_helper.log");
    g_logFilePathUtf8 = g_logFilePath.toUtf8();

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
        QAxObject* wordApp = nullptr;
        runGuardedOrDie([&wordApp]() {
            wordApp = new QAxObject("Word.Application");
        }, "creating Word.Application");
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
        runGuardedOrDie([&]() {
            documents = wordApp->querySubObject("Documents");
        }, "querying Documents");
        logStep(QString("Documents collection query returned %1").arg(documents ? "non-null" : "null"));
        if (!documents) {
            wordApp->dynamicCall("Quit()");
            delete wordApp;
            app.exit(3); // 无法访问 Documents 集合
            return;
        }

        logStep("calling Documents.Open(\"" + nativeInputPath + "\")...");
        QAxObject* document = nullptr;
        runGuardedOrDie([&]() {
            document = documents->querySubObject(
                "Open(const QString&, bool, bool, bool)",
                nativeInputPath, false, true, false);
        }, "opening the document");
        delete documents;
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
        // QAxBase::dynamicCall 的定长重载最多只接受 8 个变体参数（加方法名
        // 共 9 个），这个方法需要 14 个参数，必须使用 QList<QVariant> 形式
        // 的重载。
        QList<QVariant> exportArgs = {
            nativeOutputPath, 17, false, 0, 0, 1, 1, 0, true, true, 0, true, true, false
        };
        runGuardedOrDie([&]() {
            document->dynamicCall(
                "ExportAsFixedFormat(const QString&, int, bool, int, int, int, int, int, bool, bool, int, bool, bool, bool)",
                exportArgs);
        }, "ExportAsFixedFormat");
        logStep("ExportAsFixedFormat returned, calling Close...");

        runGuardedOrDie([&]() {
            document->dynamicCall("Close(bool)", false);
        }, "closing the document");
        logStep("Close returned");
        delete document;

        logStep("calling Word Quit...");
        runGuardedOrDie([&]() {
            wordApp->dynamicCall("Quit()");
        }, "Word Quit");
        logStep("Quit returned");
        delete wordApp;

        bool pdfExists = QFile::exists(outputPdfPath);
        logStep(QString("PDF exists=%1, exiting with code %2").arg(pdfExists).arg(pdfExists ? 0 : 5));
        app.exit(pdfExists ? 0 : 5); // 5 = 导出后未生成 PDF 文件
    });

    int rc = app.exec();
    logStep(QString("app.exec() returned %1, process exiting").arg(rc));
    return rc;
}
