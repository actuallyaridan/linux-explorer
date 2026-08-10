#pragma once

#include <QIcon>
#include <QString>
#include <initializer_list>

// Theme-icon resolution with graceful fallbacks. Per-file icons come from
// KFileItem::iconName(), which KIO resolves from the MIME database, so this
// covers the chrome icons the window draws itself, which still have to degrade
// on themes lacking the KDE-specific names.

inline QIcon tryIconName(const QString &name)
{
    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) return icon;
    return QIcon::fromTheme(name + QStringLiteral("-symbolic"));
}

// First icon found from an explicit preference list, else "system-file-manager".
inline QIcon themeIcon(std::initializer_list<const char *> names)
{
    for (const char *n : names) {
        QIcon icon = tryIconName(QString::fromLatin1(n));
        if (!icon.isNull()) return icon;
    }
    QIcon icon = QIcon::fromTheme(QStringLiteral("system-file-manager"));
    if (!icon.isNull()) return icon;
    return QIcon::fromTheme(QStringLiteral("folder"));
}
