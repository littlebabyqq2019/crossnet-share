#include "watermark_service.h"
#include "document_converter.h"
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QXmlStreamReader>
#include <QPainter>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDateTime>
#include <QTemporaryFile>
#include <cmath>

namespace CrossNetShare {

WatermarkService::WatermarkService(QObject* parent)
    : QObject(parent)
{
    // 设置默认配置路径
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    configPath_ = appDataPath + "/watermark_config.json";

    // 加载配置
    loadConfig();
}

WatermarkService::~WatermarkService() {
    saveConfig();
}

bool WatermarkService::loadConfig(const QString& configPath) {
    QString path = configPath.isEmpty() ? configPath_ : configPath;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage("Watermark config not found, using defaults");

        // 设置默认关键词
        keywords_ = {
            KeywordRule("党政办", "党政办", true),
            KeywordRule("医政科", "医政科", true),
            KeywordRule("公卫科", "公卫科", true),
            KeywordRule("中医科", "中医科", true),
            KeywordRule("法监科", "法监科", true),
            KeywordRule("家庭发展科", "家庭发展科", true),
            KeywordRule("健康促进科（爱卫办）", "健康促进科", true),
            KeywordRule("办公室（后勤）", "办公室（后勤）", true),
            KeywordRule("办公室（财务）", "办公室（财务）", true),
            KeywordRule("信息中心", "信息中心", true),
            KeywordRule("计生协", "计生协", true),
            KeywordRule("疾控中心（监督所、健教所）", "疾控中心", true),
            KeywordRule("妇计中心", "妇计中心", true),
            KeywordRule("中心医院", "中心医院", true),
            KeywordRule("大雁塔", "大雁塔", true),
            KeywordRule("小寨路", "小寨路", true),
            KeywordRule("长延堡", "长延堡", true),
            KeywordRule("电子城", "电子城", true),
            KeywordRule("等驾坡", "等驾坡", true),
            KeywordRule("曲江", "曲江", true),
            KeywordRule("杜城", "杜城", true),
            KeywordRule("各科室", "各科室", true),
            KeywordRule("各单位", "各单位", true),
            KeywordRule("各科室、各单位", "全体科室单位", true)
        };

        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit logMessage("Invalid watermark config format");
        return false;
    }

    QJsonObject root = doc.object();

    // 加载水印样式
    if (root.contains("watermarkStyle")) {
        QJsonObject style = root["watermarkStyle"].toObject();
        config_.fontSize = style["fontSize"].toInt(14);
        config_.color = QColor(style["color"].toString("#808080"));
        config_.opacity = style["opacity"].toInt(30);
        config_.rotation = style["rotation"].toInt(45);
        config_.density = style["density"].toString("medium");
        config_.enableUnified = style["enableUnified"].toBool(false);
        config_.unifiedText = style["unifiedText"].toString("");
        config_.enabled = style["enabled"].toBool(false);
    }

    // 加载关键词
    keywords_.clear();
    if (root.contains("keywords")) {
        QJsonArray keywordsArray = root["keywords"].toArray();
        for (const QJsonValue& val : keywordsArray) {
            QJsonObject obj = val.toObject();
            KeywordRule rule;
            rule.detectText = obj["detectText"].toString();
            rule.watermarkText = obj["watermarkText"].toString();
            rule.enabled = obj["enabled"].toBool(true);
            keywords_.append(rule);
        }
    }

    emit logMessage("Watermark config loaded successfully");
    return true;
}

bool WatermarkService::saveConfig(const QString& configPath) {
    QString path = configPath.isEmpty() ? configPath_ : configPath;

    // 确保目录存在
    QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonObject root;

    // 保存水印样式
    QJsonObject style;
    style["fontSize"] = config_.fontSize;
    style["color"] = config_.color.name();
    style["opacity"] = config_.opacity;
    style["rotation"] = config_.rotation;
    style["density"] = config_.density;
    style["enableUnified"] = config_.enableUnified;
    style["unifiedText"] = config_.unifiedText;
    style["enabled"] = config_.enabled;
    root["watermarkStyle"] = style;

    // 保存关键词
    QJsonArray keywordsArray;
    for (const KeywordRule& rule : keywords_) {
        QJsonObject obj;
        obj["detectText"] = rule.detectText;
        obj["watermarkText"] = rule.watermarkText;
        obj["enabled"] = rule.enabled;
        keywordsArray.append(obj);
    }
    root["keywords"] = keywordsArray;

    QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit logMessage("Failed to save watermark config: " + file.errorString());
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    emit logMessage("Watermark config saved successfully");
    return true;
}

void WatermarkService::setConfig(const WatermarkConfig& config) {
    config_ = config;
}

void WatermarkService::setKeywords(const QList<KeywordRule>& keywords) {
    keywords_ = keywords;
}

WatermarkService::WatermarkResult WatermarkService::generateWatermarkedImages(
    const QString& wordFilePath,
    const QString& outputDir,
    const QString& originalFileName)
{
    WatermarkResult result;

    // 检查功能是否启用
    if (!config_.enabled) {
        result.error = "Watermark feature is disabled";
        return result;
    }

    emit progress("开始处理文档...", 1, 6);
    emit logMessage("Processing: " + wordFilePath);

    // 1. 转换 Word 为 PDF（使用 DocumentConverter，处理 WordML 格式）
    emit progress("转换文档为 PDF...", 2, 6);
    QString pdfPath = convertWordToPdf(wordFilePath, outputDir);
    if (pdfPath.isEmpty()) {
        result.error = "Failed to convert Word to PDF";
        return result;
    }

    // 2. 转换 PDF 为 HTML（提取表格内容）
    emit progress("解析文档表格...", 3, 6);
    QString htmlPath = convertWordToHtml(pdfPath, outputDir);
    if (htmlPath.isEmpty()) {
        emit logMessage("Warning: Failed to convert PDF to HTML for table extraction");
        emit logMessage("Continuing without keyword detection...");
    }

    // 3. 提取拟办意见文本
    QString opinionText;
    if (!htmlPath.isEmpty()) {
        opinionText = extractOpinionText(htmlPath);
        if (opinionText.isEmpty()) {
            emit logMessage("Warning: No opinion text found in row 7, column 2");
        }
    }

    // 4. 匹配关键词
    QStringList matchedKeywords;
    if (config_.enableUnified) {
        // 统一水印模式：使用配置的统一文字
        if (!config_.unifiedText.isEmpty()) {
            matchedKeywords.append(config_.unifiedText);
        }
    } else {
        // 关键词模式：匹配检测到的关键词
        if (!opinionText.isEmpty()) {
            matchedKeywords = matchKeywords(opinionText);
        }
    }

    if (matchedKeywords.isEmpty()) {
        // 没有匹配到关键词，生成无水印原图
        emit logMessage("No keywords matched, generating original image");
        matchedKeywords.append("");  // 空字符串表示无水印
    }

    // 5. 转换 PDF 为 JPG
    emit progress("转换文档为图片...", 4, 6);
    QString baseImagePath = convertPdfToJpg(pdfPath, outputDir);
    if (baseImagePath.isEmpty()) {
        result.error = "Failed to convert PDF to JPG";
        return result;
    }

    emit logMessage("Loading image from: " + baseImagePath);
    emit logMessage("File exists: " + QString(QFile::exists(baseImagePath) ? "Yes" : "No"));
    if (QFile::exists(baseImagePath)) {
        QFileInfo fileInfo(baseImagePath);
        emit logMessage("File size: " + QString::number(fileInfo.size()) + " bytes");
    }

    QImage baseImage(baseImagePath);
    if (baseImage.isNull()) {
        emit logMessage("Error: QImage failed to load the file");
        emit logMessage("Checking for multi-page output...");

        // LibreOffice 可能生成了多页，尝试查找第一页
        QFileInfo info(baseImagePath);
        QString baseName = info.completeBaseName();
        QString firstPagePath = info.dir().absolutePath() + "/" + baseName + "_1.jpg";

        if (QFile::exists(firstPagePath)) {
            emit logMessage("Found multi-page output, using first page: " + firstPagePath);
            baseImage.load(firstPagePath);
            baseImagePath = firstPagePath;
        }

        if (baseImage.isNull()) {
            result.error = "Failed to load generated image";
            return result;
        }
    }

    // 6. 为每个关键词生成带水印的图片
    emit progress("生成水印图片...", 5, 6);
    QStringList generatedImages;
    QString baseName = QFileInfo(originalFileName).completeBaseName();

    int current = 0;
    for (const QString& keyword : matchedKeywords) {
        current++;

        QImage watermarkedImage;
        QString outputFileName;

        if (keyword.isEmpty()) {
            // 无水印原图
            watermarkedImage = baseImage;
            outputFileName = baseName + ".jpg";
        } else {
            // 添加水印
            watermarkedImage = addWatermark(baseImage, keyword);
            outputFileName = keyword + "-" + baseName + ".jpg";
        }

        QString outputPath = outputDir + "/" + outputFileName;
        if (!watermarkedImage.save(outputPath, "JPG", 95)) {
            emit logMessage("Warning: Failed to save " + outputFileName);
            continue;
        }

        generatedImages.append(outputPath);
        result.generatedFiles.append(outputFileName);

        emit logMessage("Generated: " + outputFileName);
    }

    if (generatedImages.isEmpty()) {
        result.error = "No images generated";
        return result;
    }

    // 7. 打包为 ZIP
    emit progress("打包图片...", 6, 6);
    result.zipFilePath = createZipFile(generatedImages, outputDir, baseName + "_水印图片");

    // 8. 清理临时文件
    if (!htmlPath.isEmpty()) {
        QFile::remove(htmlPath);
    }
    QFile::remove(pdfPath);
    QFile::remove(baseImagePath);
    for (const QString& imagePath : generatedImages) {
        // ZIP 创建后删除临时图片文件
        // QFile::remove(imagePath);  // 暂时保留，方便调试
    }

    result.success = true;
    emit progress("完成！", 6, 6);
    return result;
}

QString WatermarkService::convertWordToPdf(const QString& wordFilePath, const QString& outputDir) {
    emit logMessage("Converting Word to PDF using DocumentConverter...");

    // 使用 DocumentConverter 转换（会自动使用缓存）
    DocumentConverter::PreviewResult pdfResult = DocumentConverter::previewFile(wordFilePath);

    if (!pdfResult.success) {
        emit logMessage("Error: Failed to convert Word to PDF: " + pdfResult.error);
        return QString();
    }

    if (pdfResult.mimeType != "application/pdf") {
        emit logMessage("Error: Expected PDF output but got: " + pdfResult.mimeType);
        return QString();
    }

    // 保存 PDF 到临时目录
    QString pdfPath = outputDir + "/" + QFileInfo(wordFilePath).completeBaseName() + ".pdf";
    QFile pdfFile(pdfPath);
    if (!pdfFile.open(QIODevice::WriteOnly)) {
        emit logMessage("Error: Failed to write PDF file: " + pdfPath);
        return QString();
    }

    pdfFile.write(pdfResult.data);
    pdfFile.close();

    emit logMessage("Successfully converted to PDF: " + pdfPath);
    return pdfPath;
}

QString WatermarkService::convertPdfToJpg(const QString& pdfFilePath, const QString& outputDir) {
    // 尝试多个可能的 LibreOffice 可执行文件名
    QStringList possibleCommands = {"soffice", "soffice.exe", "libreoffice", "libreoffice.exe"};
    QString sofficePath;

    // 检查哪个命令可用
    for (const QString& cmd : possibleCommands) {
        QProcess testProcess;
        testProcess.start(cmd, QStringList() << "--version");
        if (testProcess.waitForStarted(1000)) {
            testProcess.waitForFinished(3000);
            sofficePath = cmd;
            break;
        }
    }

    if (sofficePath.isEmpty()) {
        emit logMessage("Error: LibreOffice not found for PDF to JPG conversion");
        return QString();
    }

    QStringList args;
    args << "--headless"
         << "--convert-to" << "jpg"
         << "--outdir" << outputDir
         << pdfFilePath;

    emit logMessage("Converting PDF to JPG using: " + sofficePath);
    emit logMessage("Input PDF: " + pdfFilePath);
    emit logMessage("Output dir: " + outputDir);
    emit logMessage("Command: " + sofficePath + " " + args.join(" "));

    // 检查输入文件是否存在
    if (!QFile::exists(pdfFilePath)) {
        emit logMessage("Error: Input PDF file does not exist: " + pdfFilePath);
        return QString();
    }

    // 确保输出目录存在
    QDir dir(outputDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            emit logMessage("Error: Failed to create output directory");
            return QString();
        }
    }

    QProcess process;
    process.setWorkingDirectory(outputDir);
    process.start(sofficePath, args);

    if (!process.waitForStarted(5000)) {
        emit logMessage("Error: Failed to start LibreOffice for PDF to JPG conversion");
        return QString();
    }

    if (!process.waitForFinished(30000)) {
        emit logMessage("Error: LibreOffice PDF to JPG conversion timeout");
        process.kill();
        return QString();
    }

    if (process.exitCode() != 0) {
        QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError());
        QString stdout_output = QString::fromLocal8Bit(process.readAllStandardOutput());
        emit logMessage("Error: LibreOffice PDF to JPG conversion failed (exit code: " + QString::number(process.exitCode()) + ")");
        if (!stderr_output.isEmpty()) emit logMessage("stderr: " + stderr_output);
        if (!stdout_output.isEmpty()) emit logMessage("stdout: " + stdout_output);
        return QString();
    }

    // 计算输出文件名
    QString baseName = QFileInfo(pdfFilePath).completeBaseName();
    QString jpgPath = outputDir + "/" + baseName + ".jpg";

    // LibreOffice 可能生成多页，检查第一页
    if (!QFile::exists(jpgPath)) {
        QString firstPagePath = outputDir + "/" + baseName + "_1.jpg";
        if (QFile::exists(firstPagePath)) {
            jpgPath = firstPagePath;
            emit logMessage("Multi-page PDF detected, using first page");
        } else {
            emit logMessage("Error: JPG file not generated. Expected: " + jpgPath);
            return QString();
        }
    }

    emit logMessage("Successfully converted PDF to JPG: " + jpgPath);
    return jpgPath;
}

QString WatermarkService::convertWordToHtml(const QString& wordFilePath, const QString& outputDir) {
    // 尝试多个可能的 LibreOffice 可执行文件名
    QStringList possibleCommands = {"soffice", "soffice.exe", "libreoffice", "libreoffice.exe"};
    QString sofficePath;

    // 检查哪个命令可用
    for (const QString& cmd : possibleCommands) {
        QProcess testProcess;
        testProcess.start(cmd, QStringList() << "--version");
        if (testProcess.waitForStarted(1000)) {
            testProcess.waitForFinished(3000);
            sofficePath = cmd;
            break;
        }
    }

    if (sofficePath.isEmpty()) {
        emit logMessage("Error: LibreOffice not found. Please install LibreOffice and ensure it's in PATH.");
        emit logMessage("Tried commands: " + possibleCommands.join(", "));
        return QString();
    }

    QStringList args;
    args << "--headless"
         << "--convert-to" << "html:HTML:EmbedImages"
         << "--outdir" << outputDir
         << wordFilePath;

    emit logMessage("Converting Word to HTML using: " + sofficePath);
    emit logMessage("Input file: " + wordFilePath);
    emit logMessage("Output dir: " + outputDir);
    emit logMessage("Command: " + sofficePath + " " + args.join(" "));

    // 检查输入文件是否存在
    if (!QFile::exists(wordFilePath)) {
        emit logMessage("Error: Input Word file does not exist: " + wordFilePath);
        return QString();
    }

    // 确保输出目录存在
    QDir dir(outputDir);
    if (!dir.exists()) {
        emit logMessage("Creating output directory: " + outputDir);
        if (!dir.mkpath(".")) {
            emit logMessage("Error: Failed to create output directory");
            return QString();
        }
    }

    QProcess process;
    process.setWorkingDirectory(outputDir);
    process.start(sofficePath, args);

    if (!process.waitForStarted(5000)) {
        emit logMessage("Error: Failed to start LibreOffice (" + sofficePath + ")");
        return QString();
    }

    if (!process.waitForFinished(30000)) {
        emit logMessage("Error: LibreOffice conversion timeout");
        process.kill();
        return QString();
    }

    if (process.exitCode() != 0) {
        QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError());
        QString stdout_output = QString::fromLocal8Bit(process.readAllStandardOutput());
        emit logMessage("Error: LibreOffice conversion failed (exit code: " + QString::number(process.exitCode()) + ")");
        if (!stderr_output.isEmpty()) emit logMessage("stderr: " + stderr_output);
        if (!stdout_output.isEmpty()) emit logMessage("stdout: " + stdout_output);
        return QString();
    }

    // 计算输出文件名
    QString baseName = QFileInfo(wordFilePath).completeBaseName();
    QString htmlPath = outputDir + "/" + baseName + ".html";

    if (!QFile::exists(htmlPath)) {
        emit logMessage("Error: HTML file not generated: " + htmlPath);
        return QString();
    }

    emit logMessage("Successfully converted to HTML: " + htmlPath);
    return htmlPath;
}

QString WatermarkService::convertWordToJpg(const QString& wordFilePath, const QString& outputDir) {
    // 尝试多个可能的 LibreOffice 可执行文件名
    QStringList possibleCommands = {"soffice", "soffice.exe", "libreoffice", "libreoffice.exe"};
    QString sofficePath;

    // 检查哪个命令可用
    for (const QString& cmd : possibleCommands) {
        QProcess testProcess;
        testProcess.start(cmd, QStringList() << "--version");
        if (testProcess.waitForStarted(1000)) {
            testProcess.waitForFinished(3000);
            sofficePath = cmd;
            break;
        }
    }

    if (sofficePath.isEmpty()) {
        emit logMessage("Error: LibreOffice not found for JPG conversion");
        return QString();
    }

    QStringList args;
    args << "--headless"
         << "--convert-to" << "jpg"
         << "--outdir" << outputDir
         << wordFilePath;

    emit logMessage("Converting Word to JPG using: " + sofficePath);
    emit logMessage("Input file: " + wordFilePath);
    emit logMessage("Output dir: " + outputDir);
    emit logMessage("Command: " + sofficePath + " " + args.join(" "));

    // 检查输入文件是否存在
    if (!QFile::exists(wordFilePath)) {
        emit logMessage("Error: Input Word file does not exist: " + wordFilePath);
        return QString();
    }

    // 确保输出目录存在
    QDir dir(outputDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            emit logMessage("Error: Failed to create output directory");
            return QString();
        }
    }

    QProcess process;
    process.setWorkingDirectory(outputDir);
    process.start(sofficePath, args);

    if (!process.waitForStarted(5000)) {
        emit logMessage("Error: Failed to start LibreOffice for JPG conversion");
        return QString();
    }

    if (!process.waitForFinished(30000)) {
        emit logMessage("Error: LibreOffice JPG conversion timeout");
        process.kill();
        return QString();
    }

    if (process.exitCode() != 0) {
        QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError());
        QString stdout_output = QString::fromLocal8Bit(process.readAllStandardOutput());
        emit logMessage("Error: LibreOffice JPG conversion failed (exit code: " + QString::number(process.exitCode()) + ")");
        if (!stderr_output.isEmpty()) emit logMessage("stderr: " + stderr_output);
        if (!stdout_output.isEmpty()) emit logMessage("stdout: " + stdout_output);
        return QString();
    }

    // 计算输出文件名
    QString baseName = QFileInfo(wordFilePath).completeBaseName();
    QString jpgPath = outputDir + "/" + baseName + ".jpg";

    if (!QFile::exists(jpgPath)) {
        emit logMessage("Error: JPG file not generated: " + jpgPath);
        return QString();
    }

    emit logMessage("Successfully converted to JPG: " + jpgPath);
    return jpgPath;
}

QString WatermarkService::extractOpinionText(const QString& htmlFilePath) {
    QFile file(htmlFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage("Error: Failed to open HTML file: " + htmlFilePath);
        return QString();
    }

    QXmlStreamReader xml(&file);
    int rowIndex = 0;
    int colIndex = 0;
    bool inTable = false;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            QString name = xml.name().toString().toLower();

            if (name == "table") {
                inTable = true;
                rowIndex = 0;
            } else if (inTable && name == "tr") {
                rowIndex++;
                colIndex = 0;
            } else if (inTable && (name == "td" || name == "th")) {
                colIndex++;

                if (rowIndex == 7 && colIndex == 2) {
                    QString text = xml.readElementText().trimmed();
                    file.close();
                    return text;
                }
            }
        } else if (xml.isEndElement()) {
            QString name = xml.name().toString().toLower();
            if (name == "table") {
                inTable = false;
            }
        }
    }

    file.close();

    if (xml.hasError()) {
        emit logMessage("Warning: XML parsing error: " + xml.errorString());
    }

    return QString();
}

QStringList WatermarkService::matchKeywords(const QString& text) {
    QStringList matched;

    for (const KeywordRule& rule : keywords_) {
        if (!rule.enabled) {
            continue;
        }

        // 精确匹配整个短语
        if (text.contains(rule.detectText)) {
            matched.append(rule.watermarkText);
            emit logMessage("Matched keyword: " + rule.detectText + " -> " + rule.watermarkText);
        }
    }

    return matched;
}

QImage WatermarkService::addWatermark(const QImage& source, const QString& text) {
    QImage result = source.copy();
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // 设置字体
    QFont font("SimSun", config_.fontSize);
    painter.setFont(font);

    // 设置颜色和透明度
    QColor color = config_.color;
    color.setAlpha(255 * config_.opacity / 100);
    painter.setPen(color);

    // 计算文字尺寸
    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(text);

    // 根据密度计算间距
    int baseSpacing = qMax(textRect.width(), textRect.height()) * 2;
    int spacing = baseSpacing;
    if (config_.density == "sparse") {
        spacing = baseSpacing * 2;
    } else if (config_.density == "dense") {
        spacing = baseSpacing;
    } else {  // medium
        spacing = baseSpacing * 1.5;
    }

    // 保存当前状态
    painter.save();

    // 计算旋转后需要绘制的范围
    int diagonal = static_cast<int>(std::sqrt(result.width() * result.width() + result.height() * result.height()));

    // 平移到中心，旋转
    painter.translate(result.width() / 2, result.height() / 2);
    painter.rotate(config_.rotation);
    painter.translate(-result.width() / 2, -result.height() / 2);

    // 满铺绘制水印
    for (int y = -diagonal; y < result.height() + diagonal; y += spacing) {
        for (int x = -diagonal; x < result.width() + diagonal; x += spacing) {
            painter.drawText(x, y, text);
        }
    }

    painter.restore();
    painter.end();

    return result;
}

QString WatermarkService::createZipFile(const QStringList& imagePaths, const QString& outputDir, const QString& baseName) {
    // 简化版：直接返回第一个文件（如果只有一个）
    // 完整版需要使用 QuaZip 或调用系统压缩命令

    if (imagePaths.size() == 1) {
        return imagePaths.first();
    }

    // TODO: 实现 ZIP 打包
    // 临时方案：返回第一个文件
    QString zipPath = outputDir + "/" + baseName + ".zip";

    // 使用系统 tar 命令打包（Windows 10+ 支持）
    QStringList fileNames;
    for (const QString& path : imagePaths) {
        fileNames.append(QFileInfo(path).fileName());
    }

    QProcess process;
    process.setWorkingDirectory(outputDir);

    #ifdef Q_OS_WIN
    // Windows: 使用 PowerShell Compress-Archive
    QString filesArg = fileNames.join("','");
    QStringList args;
    args << "-Command"
         << QString("Compress-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(filesArg, QFileInfo(zipPath).fileName());
    process.start("powershell", args);
    #else
    // Linux/Mac: 使用 zip
    QStringList args;
    args << zipPath;
    args.append(fileNames);
    process.start("zip", args);
    #endif

    if (!process.waitForFinished(10000)) {
        emit logMessage("Warning: ZIP creation timeout");
        process.kill();
        return imagePaths.first();
    }

    if (QFile::exists(zipPath)) {
        return zipPath;
    }

    emit logMessage("Warning: ZIP creation failed, returning first image");
    return imagePaths.first();
}

}
