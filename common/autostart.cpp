#include "autostart.h"

#ifdef Q_OS_WIN
#include <QSettings>
#include <QDir>
#endif

namespace CrossNetShare {

bool AutoStart::setAutoStart(const QString& appName, const QString& appPath, bool enable) {
#ifdef Q_OS_WIN
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      QSettings::NativeFormat);

    if (enable) {
        QString path = QDir::toNativeSeparators(appPath);
        settings.setValue(appName, "\"" + path + "\"");
    } else {
        settings.remove(appName);
    }

    settings.sync();
    return settings.status() == QSettings::NoError;
#else
    // Linux/Mac implementation can be added here
    return false;
#endif
}

bool AutoStart::isAutoStartEnabled(const QString& appName) {
#ifdef Q_OS_WIN
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      QSettings::NativeFormat);
    return settings.contains(appName);
#else
    return false;
#endif
}

}
