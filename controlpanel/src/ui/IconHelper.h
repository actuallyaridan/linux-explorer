#pragma once

#include <QHash>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <initializer_list>

// Theme-icon resolution with graceful fallbacks. The Control Panel borrows many
// KDE/Breeze-specific icon names (e.g. "input_devices_settings", "gadgets") that
// are absent on Adwaita and other themes, so every lookup degrades to a sensible
// alternative and ultimately to "preferences-system".

inline QIcon tryIconName(const QString &name) {
    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) return icon;
    return QIcon::fromTheme(name + QStringLiteral("-symbolic"));
}

// First icon found from an explicit preference list, else "preferences-system".
inline QIcon themeIcon(std::initializer_list<const char *> names) {
    for (const char *n : names) {
        QIcon icon = tryIconName(QString::fromLatin1(n));
        if (!icon.isNull()) return icon;
    }
    QIcon icon = QIcon::fromTheme(QStringLiteral("preferences-system"));
    if (!icon.isNull()) return icon;
    return QIcon::fromTheme(QStringLiteral("preferences-system-symbolic"));
}

// Resolve a single theme name, consulting a fallback chain for the KDE-specific
// names used in the category data when the requested icon is missing.
inline QIcon resolveIcon(const QString &name) {
    using SL = QStringList;
    static const QHash<QString, SL> fallbacks{
        {QStringLiteral("input_devices_settings"),
         SL{"preferences-desktop-peripherals", "input-keyboard", "input-mouse"}},
        {QStringLiteral("system-software-update"),
         SL{"software-update-available", "update-notifier"}},
        {QStringLiteral("drive-harddisk-encrypted"),
         SL{"security-high", "security-medium", "drive-harddisk"}},
        {QStringLiteral("redhat-system_tools"),
         SL{"applications-system", "computer"}},
        {QStringLiteral("gadgets"),
         SL{"applications-utilities", "preferences-desktop"}},
        {QStringLiteral("exec"),
         SL{"application-x-executable", "system-run"}},
        {QStringLiteral("preferences-system-network-share-windows"),
         SL{"preferences-system-network", "network-workgroup"}},
        {QStringLiteral("preferences-desktop-theme-global"),
         SL{"preferences-desktop-theme"}},
        {QStringLiteral("preferences-desktop-activities"),
         SL{"applications-system", "preferences-system"}},
        {QStringLiteral("application-vnd.debian.binary-package"),
         SL{"package-x-generic", "application-x-deb"}},
    };

    if (!name.isEmpty()) {
        QIcon icon = tryIconName(name);
        if (!icon.isNull()) return icon;

        auto it = fallbacks.constFind(name);
        if (it != fallbacks.constEnd()) {
            for (const QString &fb : it.value()) {
                icon = tryIconName(fb);
                if (!icon.isNull()) return icon;
            }
        }
    }

    QIcon icon = QIcon::fromTheme(QStringLiteral("preferences-system"));
    if (!icon.isNull()) return icon;
    return QIcon::fromTheme(QStringLiteral("preferences-system-symbolic"));
}
