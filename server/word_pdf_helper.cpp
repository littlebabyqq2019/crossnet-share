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
#include <windows.h>
#include <shellapi.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 不使用 CRT 自带的 narrow argv：它是把宽字符命令行按系统 ANSI 代码页
    // 转换而来的，对不在该代码页范围内的文件名会造成乱码/截断。这里直接从
    // 原始宽字符命令行重新解析参数，确保任意 Unicode 路径都能被正确还原。
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv || wargc < 3) {
        if (wargv) LocalFree(wargv);
        return 1; // 参数不足
    }

    QString inputPath = QString::fromWCharArray(wargv[1]);
    QString outputPdfPath = QString::fromWCharArray(wargv[2]);
    LocalFree(wargv);

    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(inputPath).absoluteFilePath());
    QString nativeOutputPath = QDir::toNativeSeparators(QFileInfo(outputPdfPath).absoluteFilePath());

    QAxObject wordApp("Word.Application");
    if (wordApp.isNull()) {
        return 2; // 无法启动/连接 Microsoft Word
    }
    wordApp.setProperty("Visible", false);
    wordApp.setProperty("DisplayAlerts", 0);

    QAxObject* documents = wordApp.querySubObject("Documents");
    if (!documents) {
        wordApp.dynamicCall("Quit()");
        return 3; // 无法访问 Documents 集合
    }

    QAxObject* document = documents->querySubObject(
        "Open(const QString&, bool, bool, bool)",
        nativeInputPath, false, true, false);
    delete documents;

    if (!document) {
        wordApp.dynamicCall("Quit()");
        return 4; // 打开文档失败
    }

    // 17 = wdExportFormatPDF
    document->dynamicCall("ExportAsFixedFormat(const QString&, int)", nativeOutputPath, 17);
    document->dynamicCall("Close(bool)", false);
    delete document;

    wordApp.dynamicCall("Quit()");

    return QFile::exists(outputPdfPath) ? 0 : 5; // 5 = 导出后未生成 PDF 文件
}
