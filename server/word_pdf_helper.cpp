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

#include <QApplication>
#include <QAxObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <windows.h>
#include <shellapi.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

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
        return 1; // 参数不足
    }

    // 关键：Word 自动化调用必须在事件循环已经运行起来之后才发起，不能像
    // 之前那样在 app.exec() 之前于 main() 里同步调用。Word 是进程外 COM
    // 服务器，在执行较慢操作（比如 ExportAsFixedFormat 导出较大文档）时，
    // 可能通过 COM 消息过滤器/重入回调与发起调用的 STA 线程通信，这要求该
    // 线程此刻有一个正在运行的消息循环来处理这些往返消息。如果没有消息
    // 循环在跑，这类回调可能找不到出路，导致底层 OLE/RPC 层抛出一个无法
    // 被捕获的结构化异常，表现为本进程直接崩溃退出——这正是实测观察到的
    // "process crashed" 现象的根源。用 QTimer::singleShot(0, ...) 把实际
    // 工作推迟到 exec() 内部执行，就能保证调用发生时消息循环确实在运行。
    QTimer::singleShot(0, &app, [&app, inputPath, outputPdfPath]() {
        QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(inputPath).absoluteFilePath());
        QString nativeOutputPath = QDir::toNativeSeparators(QFileInfo(outputPdfPath).absoluteFilePath());

        QAxObject wordApp("Word.Application");
        if (wordApp.isNull()) {
            app.exit(2); // 无法启动/连接 Microsoft Word
            return;
        }
        wordApp.setProperty("Visible", false);
        wordApp.setProperty("DisplayAlerts", 0);

        QAxObject* documents = wordApp.querySubObject("Documents");
        if (!documents) {
            wordApp.dynamicCall("Quit()");
            app.exit(3); // 无法访问 Documents 集合
            return;
        }

        QAxObject* document = documents->querySubObject(
            "Open(const QString&, bool, bool, bool)",
            nativeInputPath, false, true, false);
        delete documents;

        if (!document) {
            wordApp.dynamicCall("Quit()");
            app.exit(4); // 打开文档失败
            return;
        }

        // 17 = wdExportFormatPDF
        document->dynamicCall("ExportAsFixedFormat(const QString&, int)", nativeOutputPath, 17);
        document->dynamicCall("Close(bool)", false);
        delete document;

        wordApp.dynamicCall("Quit()");

        app.exit(QFile::exists(outputPdfPath) ? 0 : 5); // 5 = 导出后未生成 PDF 文件
    });

    return app.exec();
}
