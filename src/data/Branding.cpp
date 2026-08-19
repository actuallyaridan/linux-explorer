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
        return useWindowsNames() ? QStringLiteral("Windows")
                                 : QStringLiteral("Linux");
    }

    return systemFolders().value(cleanPath);
}

// Resolved at first use, since these come from the running user's own config
const QHash<QString, QString> &userFolders()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        const QString home = QDir::homePath();
        const auto add = [&m](const QString &path, const QString &name) {
            if (!path.isEmpty())
                m.insert(QDir::cleanPath(path), name);
        };

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
    return mapped.isEmpty() ? realName : mapped;
}

} // namespace Branding
