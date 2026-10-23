#pragma once

#include <QHash>
#include <QIcon>
#include <QString>
#include <QStringList>

#include "aero/icons.h"

namespace ControlPanel {

// The Control Panel's category data names many KDE and Breeze specific icons
// (e.g. "input_devices_settings", "gadgets") that are absent on Adwaita and
// other themes, so this looks each one up with a fallback chain of its own,
// ending at "preferences-system". Plain lists of names go to Aero::themeIcon

// Resolve a single theme name, consulting a fallback chain for the KDE-specific
// names used in the category data when the requested icon is missing
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
        QIcon icon = Aero::tryIconName(name);
        if (!icon.isNull())
            return icon;

        auto it = fallbacks.constFind(name);
        if (it != fallbacks.constEnd()) {
            for (const QString &fb : it.value()) {
                icon = Aero::tryIconName(fb);
                if (!icon.isNull())
                    return icon;
            }
        }
    }

    QIcon icon = QIcon::fromTheme(QStringLiteral("preferences-system"));
    if (!icon.isNull())
        return icon;
    return QIcon::fromTheme(QStringLiteral("preferences-system-symbolic"));
}

} // namespace ControlPanel
