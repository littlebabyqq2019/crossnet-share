#include "document_converter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

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
    result.success = true;
    result.mimeType = "text/html; charset=utf-8";
    result.data = "<pre class=\"text-preview\">" + htmlEscape(QString::fromUtf8(content)) + "</pre>";
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
         << "html"
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

    QString htmlPath = tempDir.path() + "/" + QFileInfo(filePath).completeBaseName() + ".html";
    QFile htmlFile(htmlPath);
    if (!htmlFile.open(QIODevice::ReadOnly)) {
        QStringList generatedFiles = QDir(tempDir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
        result.success = false;
        result.error = withDetails("Converted HTML file not found", "Generated files: " + generatedFiles.join(", ") + (details.isEmpty() ? QString() : "\n" + details));
        return result;
    }

    result.success = true;
    result.mimeType = "text/html; charset=utf-8";
    result.data = htmlFile.readAll();
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
