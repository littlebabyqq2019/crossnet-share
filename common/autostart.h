#pragma once

#include <QString>

namespace CrossNetShare {

class AutoStart {
public:
    // 设置开机自启动
    static bool setAutoStart(const QString& appName, const QString& appPath, bool enable);

    // 检查是否已设置自启动
    static bool isAutoStartEnabled(const QString& appName);
};

}
