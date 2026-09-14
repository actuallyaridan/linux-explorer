#include "Settings.h"

#include <atomic>

#include <QSettings>
#include <QStringList>
#include <QUrl>

namespace Settings {

namespace {

// How many folders keep a remembered view mode
constexpr int kMaxRememberedFolders = 400;

// About Win7's own list length
constexpr int kMaxRecentPaths = 25;

QString key(const char *name)
{
    return QStringLiteral("View/") + QLatin1String(name);
}

// Constructing a QSettings costs about 22us, and these are read from the sort
// comparator and from every painted cell, so the value is held in memory and
// the setter is the only thing that can move it
struct BoolSetting {
    const char *name;
    bool fallback;
    std::atomic<signed char> cached{-1};

    bool get()
    {
        signed char value = cached.load(std::memory_order_relaxed);
        if (value < 0) {
            value = QSettings().value(key(name), fallback).toBool() ? 1 : 0;
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

BoolSetting s_showHiddenFiles{"ShowHiddenFiles", false};
BoolSetting s_hideKnownExtensions{"HideKnownExtensions", false};
BoolSetting s_useCheckBoxes{"UseCheckBoxes", false};
BoolSetting s_alwaysShowMenus{"AlwaysShowMenus", false};
BoolSetting s_browseInNewWindow{"BrowseInNewWindow", false};
BoolSetting s_singleClickToOpen{"SingleClickToOpen", false};
BoolSetting s_searchFileContents{"SearchFileContents", false};
BoolSetting s_searchSubfolders{"SearchSubfolders", true};
BoolSetting s_notifyUnmountedDrives{"NotifyUnmountedDrives", true};

// QSettings treats a slash as a group separator, so paths are percent encoded
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

bool showHiddenFiles()          { return s_showHiddenFiles.get(); }
void setShowHiddenFiles(bool show)      { s_showHiddenFiles.set(show); }

bool hideKnownExtensions()      { return s_hideKnownExtensions.get(); }
void setHideKnownExtensions(bool hide)  { s_hideKnownExtensions.set(hide); }

bool useCheckBoxes()            { return s_useCheckBoxes.get(); }
void setUseCheckBoxes(bool use)         { s_useCheckBoxes.set(use); }

bool alwaysShowMenus()          { return s_alwaysShowMenus.get(); }
void setAlwaysShowMenus(bool show)      { s_alwaysShowMenus.set(show); }

bool browseInNewWindow()        { return s_browseInNewWindow.get(); }
void setBrowseInNewWindow(bool separate) { s_browseInNewWindow.set(separate); }

bool singleClickToOpen()        { return s_singleClickToOpen.get(); }
void setSingleClickToOpen(bool single)  { s_singleClickToOpen.set(single); }

bool searchFileContents()       { return s_searchFileContents.get(); }
void setSearchFileContents(bool contents) { s_searchFileContents.set(contents); }

bool searchSubfolders()         { return s_searchSubfolders.get(); }
void setSearchSubfolders(bool recursive) { s_searchSubfolders.set(recursive); }

bool notifyUnmountedDrives()    { return s_notifyUnmountedDrives.get(); }
void setNotifyUnmountedDrives(bool notify) { s_notifyUnmountedDrives.set(notify); }

void clearRememberedViewModes()
{
    QSettings s;
    // The whole group, since removing the keys alone would leave it behind
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
    // Retyping a path moves it to the top rather than adding a duplicate
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

    // QSettings has no insertion order and so nothing to evict by age, and
    // dropping an arbitrary quarter keeps this to one sweep per hundred folders
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
