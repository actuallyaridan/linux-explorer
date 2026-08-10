#include "Branding.h"

#include <QDir>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

namespace Branding {

namespace {

QString key(const char *name)
{
    return QStringLiteral("Branding/") + QLatin1String(name);
}

// Absolute path -> the name a Windows user would look for. Only directories
// where the substitution genuinely helps somebody navigate, under two rules:
//
//  - No two siblings may map to the same name. /etc and /var both hold system
//    data, and calling both "ProgramData" would put two identically named
//    folders side by side in the root listing.
//  - Pseudo-filesystems (/proc, /sys, /dev, /run) are left alone: inventing a
//    counterpart would suggest they can be browsed like ordinary folders.
const QHash<QString, QString> &systemFolders()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("/home"), QStringLiteral("Users")},
        {QStringLiteral("/usr"),  QStringLiteral("Program Files")},
        {QStringLiteral("/opt"),  QStringLiteral("Program Files (x86)")},
        {QStringLiteral("/var"),  QStringLiteral("ProgramData")},
        {QStringLiteral("/tmp"),  QStringLiteral("Temp")},
        {QStringLiteral("/boot"), QStringLiteral("Boot")},
        // /etc is absent: its label depends on useWindowsNames() and is
        // resolved in rootFolderName().
    };
    return map;
}

// Mapped, and hidden anyway: C:\Boot exists on Windows but is a hidden system
// folder, so /boot keeps its name for when hidden files are shown. Leaving it
// unmapped would instead reveal it as "boot".
const QSet<QString> &hiddenDespiteName()
{
    static const QSet<QString> set = {QStringLiteral("/boot")};
    return set;
}

// A direct child of "/" and nothing else: "/usr" qualifies, "/usr/bin" does
// not, and neither does "/" itself. The hiding rule below rests on this.
bool isTopLevel(const QString &cleanPath)
{
    return cleanPath.length() > 1
        && cleanPath.startsWith(QLatin1Char('/'))
        && cleanPath.indexOf(QLatin1Char('/'), 1) == -1;
}

// The Windows name for a top-level directory, or empty if it has none.
QString rootFolderName(const QString &cleanPath)
{
    if (!isTopLevel(cleanPath))
        return {};

    // The one label that is not a fixed translation: /etc holds the OS's own
    // configuration, and which OS that claims to be is the user's choice.
    if (cleanPath == QLatin1String("/etc")) {
        return useWindowsNames() ? QStringLiteral("Windows")
                                 : QStringLiteral("Linux");
    }

    return systemFolders().value(cleanPath);
}

// Per-user directories, resolved at first use: they come from the running
// user's XDG configuration rather than being fixed paths.
const QHash<QString, QString> &userFolders()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        const QString home = QDir::homePath();
        const auto add = [&m](const QString &path, const QString &name) {
            if (!path.isEmpty())
                m.insert(QDir::cleanPath(path), name);
        };

        // Win7 keeps per-application state under AppData, and the XDG base
        // directories are the closest equivalent.
        add(home + QStringLiteral("/.config"), QStringLiteral("AppData (Roaming)"));
        add(home + QStringLiteral("/.local/share"), QStringLiteral("AppData (Local)"));
        add(home + QStringLiteral("/.cache"), QStringLiteral("AppData (Cache)"));
        return m;
    }();
    return map;
}

} // namespace

bool windowsFriendlyMode()
{
    QSettings s;
    return s.value(key("windowsFriendlyMode"), false).toBool();
}

void setWindowsFriendlyMode(bool on)
{
    QSettings s;
    s.setValue(key("windowsFriendlyMode"), on);
}

bool useWindowsNames()
{
    QSettings s;
    return s.value(key("useWindowsNames"), false).toBool();
}

void setUseWindowsNames(bool on)
{
    QSettings s;
    s.setValue(key("useWindowsNames"), on);
}

QString folderName(const QString &absolutePath)
{
    if (!windowsFriendlyMode() || absolutePath.isEmpty())
        return {};

    const QString path = QDir::cleanPath(absolutePath);

    const auto user = userFolders().constFind(path);
    if (user != userFolders().constEnd())
        return user.value();

    return rootFolderName(path);
}

bool isSystemFolder(const QString &absolutePath)
{
    if (!windowsFriendlyMode() || absolutePath.isEmpty())
        return false;

    const QString path = QDir::cleanPath(absolutePath);

    // Top level only. A folder deeper in the tree is the user's own and must
    // never be hidden, whatever it is called.
    if (!isTopLevel(path))
        return false;

    // Unmapped means no place in a C:\ listing. Derived rather than listed, so a
    // distribution's own top-level directory (/snap, /nix, /data) needs no
    // entry here. A few mapped folders are hidden too; see hiddenDespiteName().
    return hiddenDespiteName().contains(path) || rootFolderName(path).isEmpty();
}

QString displayName(const QUrl &url, const QString &realName)
{
    if (!url.isLocalFile())
        return realName;

    const QString mapped = folderName(url.toLocalFile());
    return mapped.isEmpty() ? realName : mapped;
}

} // namespace Branding
