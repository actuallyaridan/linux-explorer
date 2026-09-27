#include "Branding.h"

#include <atomic>

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

// Read from the sort comparator and from every painted cell, where constructing
// a QSettings each time costs about 22us, so the value is held in memory
//
// Atomic because the navigation pane's subdirectory scan asks about system
// folders from a worker thread
struct BoolSetting {
    const char *name;
    std::atomic<signed char> cached{-1};

    bool get()
    {
        signed char value = cached.load(std::memory_order_relaxed);
        if (value < 0) {
            value = QSettings().value(key(name), false).toBool() ? 1 : 0;
            cached.store(value, std::memory_order_relaxed);
        }
        return value == 1;
    }

    void set(bool on)
    {
        QSettings().setValue(key(name), on);
        cached.store(on ? 1 : 0, std::memory_order_relaxed);
    }
};

BoolSetting s_friendlyMode{"windowsFriendlyMode"};
BoolSetting s_useWindowsNames{"useWindowsNames"};

// Absolute path to the name a Windows user would look for, where no two
// siblings may map to the same name and pseudo filesystems are left alone
const QHash<QString, QString> &systemFolders()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("/home"), QStringLiteral("Users")},
        {QStringLiteral("/usr"),  QStringLiteral("Program Files")},
        {QStringLiteral("/opt"),  QStringLiteral("Program Files (x86)")},
        {QStringLiteral("/var"),  QStringLiteral("ProgramData")},
        {QStringLiteral("/tmp"),  QStringLiteral("Temp")},
        {QStringLiteral("/boot"), QStringLiteral("Boot")},
        // The system config folder is absent, its label depending on a setting
    };
    return map;
}

// Mapped but hidden anyway, as the boot folder is on Windows
const QSet<QString> &hiddenDespiteName()
{
    static const QSet<QString> set = {QStringLiteral("/boot")};
    return set;
}

// A direct child of the root and nothing else, which the hiding rule rests on
bool isTopLevel(const QString &cleanPath)
{
    return cleanPath.length() > 1
        && cleanPath.startsWith(QLatin1Char('/'))
        && cleanPath.indexOf(QLatin1Char('/'), 1) == -1;
}

QString rootFolderName(const QString &cleanPath)
{
    if (!isTopLevel(cleanPath))
        return {};

    // The one label that is not a fixed translation
    if (cleanPath == QLatin1String("/etc")) {
        if (useWindowsNames())
            return QStringLiteral("Windows");
        return QStringLiteral("Linux");
    }

    return systemFolders().value(cleanPath);
}

// Resolved at first use, since these come from the running user's own config
const QHash<QString, QString> &userFolders()
{
    static const QHash<QString, QString> map = [] {
        const QString home = QDir::homePath();
        QHash<QString, QString> folders;
        folders.insert(QDir::cleanPath(home + QStringLiteral("/.config")),
                       QStringLiteral("AppData (Roaming)"));
        folders.insert(QDir::cleanPath(home + QStringLiteral("/.local/share")),
                       QStringLiteral("AppData (Local)"));
        folders.insert(QDir::cleanPath(home + QStringLiteral("/.cache")),
                       QStringLiteral("AppData (Cache)"));
        return folders;
    }();
    return map;
}

} // namespace

bool windowsFriendlyMode()
{
    return s_friendlyMode.get();
}

void setWindowsFriendlyMode(bool on)
{
    s_friendlyMode.set(on);
}

bool useWindowsNames()
{
    return s_useWindowsNames.get();
}

void setUseWindowsNames(bool on)
{
    s_useWindowsNames.set(on);
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

    // Top level only, a folder deeper in the tree being the user's own
    if (!isTopLevel(path))
        return false;

    // Unmapped means no place in a Windows root listing, so a distribution's
    // own top level directory needs no entry here
    return hiddenDespiteName().contains(path) || rootFolderName(path).isEmpty();
}

QString displayName(const QUrl &url, const QString &realName)
{
    if (!url.isLocalFile())
        return realName;

    const QString mapped = folderName(url.toLocalFile());
    if (mapped.isEmpty())
        return realName;
    return mapped;
}

} // namespace Branding
