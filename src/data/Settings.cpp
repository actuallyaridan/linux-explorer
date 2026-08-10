#include "Settings.h"

#include <QSettings>
#include <QStringList>
#include <QUrl>

namespace Settings {

namespace {

// How many folders keep a remembered view mode, so a user who browses a lot does
// not accumulate an ever-growing config file. Windows trims its shell bags too.
constexpr int kMaxRememberedFolders = 400;

// How many typed addresses the address bar's dropdown keeps, which is about
// Win7's own list length.
constexpr int kMaxRecentPaths = 25;

QString key(const char *name)
{
    return QStringLiteral("View/") + QLatin1String(name);
}

// QSettings treats '/' as a group separator, so a raw path would explode into
// one nested group per segment. Percent-encoding keeps each folder to one key.
QString folderKey(const QUrl &url)
{
    return QString::fromLatin1(url.toEncoded().toPercentEncoding());
}

int clampMode(int raw)
{
    if (raw < int(ViewMode::ExtraLargeIcons) || raw > int(ViewMode::Content))
        return int(ViewMode::Details);
    return raw;
}

} // namespace

int iconSizeFor(ViewMode mode)
{
    switch (mode) {
    case ViewMode::ExtraLargeIcons: return 256;
    case ViewMode::LargeIcons:      return 96;
    case ViewMode::MediumIcons:     return 48;
    case ViewMode::SmallIcons:      return 16;
    case ViewMode::Tiles:           return 48;
    case ViewMode::Content:         return 32;
    case ViewMode::List:
    case ViewMode::Details:
    default:                        return 16;
    }
}

QByteArray windowGeometry()
{
    return QSettings().value(key("WindowGeometry")).toByteArray();
}

void setWindowGeometry(const QByteArray &state)
{
    QSettings().setValue(key("WindowGeometry"), state);
}

QByteArray splitterState()
{
    return QSettings().value(key("SplitterState")).toByteArray();
}

void setSplitterState(const QByteArray &state)
{
    QSettings().setValue(key("SplitterState"), state);
}

QByteArray headerState()
{
    return QSettings().value(key("HeaderState")).toByteArray();
}

void setHeaderState(const QByteArray &state)
{
    QSettings().setValue(key("HeaderState"), state);
}

bool showHiddenFiles()
{
    return QSettings().value(key("ShowHiddenFiles"), false).toBool();
}

void setShowHiddenFiles(bool show)
{
    QSettings().setValue(key("ShowHiddenFiles"), show);
}

bool hideKnownExtensions()
{
    return QSettings().value(key("HideKnownExtensions"), false).toBool();
}

void setHideKnownExtensions(bool hide)
{
    QSettings().setValue(key("HideKnownExtensions"), hide);
}

bool useCheckBoxes()
{
    return QSettings().value(key("UseCheckBoxes"), false).toBool();
}

void setUseCheckBoxes(bool use)
{
    QSettings().setValue(key("UseCheckBoxes"), use);
}

bool alwaysShowMenus()
{
    return QSettings().value(key("AlwaysShowMenus"), false).toBool();
}

void setAlwaysShowMenus(bool show)
{
    QSettings().setValue(key("AlwaysShowMenus"), show);
}

bool browseInNewWindow()
{
    return QSettings().value(key("BrowseInNewWindow"), false).toBool();
}

void setBrowseInNewWindow(bool separate)
{
    QSettings().setValue(key("BrowseInNewWindow"), separate);
}

bool singleClickToOpen()
{
    return QSettings().value(key("SingleClickToOpen"), false).toBool();
}

void setSingleClickToOpen(bool single)
{
    QSettings().setValue(key("SingleClickToOpen"), single);
}

bool searchFileContents()
{
    return QSettings().value(key("SearchFileContents"), false).toBool();
}

void setSearchFileContents(bool contents)
{
    QSettings().setValue(key("SearchFileContents"), contents);
}

bool searchSubfolders()
{
    return QSettings().value(key("SearchSubfolders"), true).toBool();
}

void setSearchSubfolders(bool recursive)
{
    QSettings().setValue(key("SearchSubfolders"), recursive);
}

void clearRememberedViewModes()
{
    QSettings s;
    // The whole group at once: removing the keys would leave it behind, and
    // nothing else is stored under it.
    s.remove(QStringLiteral("FolderViews"));
}

QStringList recentPaths()
{
    return QSettings().value(key("RecentPaths")).toStringList();
}

void addRecentPath(const QString &path)
{
    if (path.isEmpty())
        return;

    QStringList paths = recentPaths();
    // Re-typing a path moves it to the top rather than adding a duplicate.
    paths.removeAll(path);
    paths.prepend(path);
    while (paths.size() > kMaxRecentPaths)
        paths.removeLast();
    QSettings().setValue(key("RecentPaths"), paths);
}

ViewMode defaultViewMode()
{
    return ViewMode(clampMode(
        QSettings().value(key("DefaultMode"), int(ViewMode::Details)).toInt()));
}

void setDefaultViewMode(ViewMode mode)
{
    QSettings().setValue(key("DefaultMode"), int(mode));
}

ViewMode viewModeFor(const QUrl &url)
{
    if (!url.isValid())
        return defaultViewMode();

    QSettings s;
    s.beginGroup(QStringLiteral("FolderViews"));
    const QVariant stored = s.value(folderKey(url));
    s.endGroup();

    if (!stored.isValid())
        return defaultViewMode();
    return ViewMode(clampMode(stored.toInt()));
}

bool hasViewModeFor(const QUrl &url)
{
    if (!url.isValid())
        return false;

    QSettings s;
    s.beginGroup(QStringLiteral("FolderViews"));
    const bool stored = s.value(folderKey(url)).isValid();
    s.endGroup();
    return stored;
}

void setViewModeFor(const QUrl &url, ViewMode mode)
{
    if (!url.isValid())
        return;

    QSettings s;
    s.beginGroup(QStringLiteral("FolderViews"));

    // Trimmed before inserting, so the file never exceeds the cap. QSettings has
    // no insertion order and so no true LRU to evict by; dropping an arbitrary
    // quarter keeps this to one sweep per hundred new folders.
    const QStringList existing = s.childKeys();
    const QString entry = folderKey(url);
    if (existing.size() >= kMaxRememberedFolders && !existing.contains(entry)) {
        for (int i = 0; i < existing.size() / 4; ++i)
            s.remove(existing.at(i));
    }

    s.setValue(entry, int(mode));
    s.endGroup();
}

} // namespace Settings
