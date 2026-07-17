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
#ifdef Q_OS_WIN
#include <QAxObject>
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
    PreviewResult wordResult = convertWordWithMicrosoftWord(filePath);
    if (wordResult.success) {
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
    if (!libreOfficeResult.success && !wordResult.error.isEmpty()) {
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
    QString nativeInputPath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QString nativePdfPath = QDir::toNativeSeparators(pdfPath);

    QAxObject word("Word.Application");
    if (word.isNull()) {
        result.success = false;
        result.error = "Microsoft Word is not installed or COM automation is unavailable";
        return result;
    }

    word.setProperty("Visible", false);
    word.setProperty("DisplayAlerts", 0);

    QAxObject* documents = word.querySubObject("Documents");
    if (!documents) {
        word.dynamicCall("Quit()");
        result.success = false;
        result.error = "Failed to access Microsoft Word Documents collection";
        return result;
    }

    QVariant filename(nativeInputPath);
    QVariant confirmConversions(false);
    QVariant readOnly(true);
    QVariant addToRecentFiles(false);
    QVariant passwordDocument("");
    QVariant passwordTemplate("");
    QVariant revert(false);
    QVariant writePasswordDocument("");
    QVariant writePasswordTemplate("");
    QVariant format(QVariant());
    QVariant encoding(QVariant());
    QVariant visible(false);
    QVariant openAndRepair(true);
    QVariant documentDirection(0);
    QVariant noEncodingDialog(true);

    QAxObject* document = documents->querySubObject(
        "Open(const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&)",
        filename,
        confirmConversions,
        readOnly,
        addToRecentFiles,
        passwordDocument,
        passwordTemplate,
        revert,
        writePasswordDocument,
        writePasswordTemplate,
        format,
        encoding,
        visible,
        openAndRepair,
        documentDirection,
        noEncodingDialog);

    if (!document) {
        word.dynamicCall("Quit()");
        result.success = false;
        result.error = "Microsoft Word failed to open the document";
        return result;
    }

    QVariant outputFileName(nativePdfPath);
    QVariant exportFormat(17);
    QVariant openAfterExport(false);
    QVariant optimizeFor(0);
    QVariant range(0);
    QVariant from(1);
    QVariant to(1);
    QVariant item(0);
    QVariant includeDocProps(true);
    QVariant keepIRM(true);
    QVariant createBookmarks(0);
    QVariant docStructureTags(true);
    QVariant bitmapMissingFonts(true);
    QVariant useISO19005_1(false);

    document->dynamicCall(
        "ExportAsFixedFormat(const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&, const QVariant&)",
        outputFileName,
        exportFormat,
        openAfterExport,
        optimizeFor,
        range,
        from,
        to,
        item,
        includeDocProps,
        keepIRM,
        createBookmarks,
        docStructureTags,
        bitmapMissingFonts,
        useISO19005_1);

    document->dynamicCall("Close(const QVariant&)", QVariant(false));
    word.dynamicCall("Quit()");

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

}
