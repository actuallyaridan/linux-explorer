#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QUrl;

// Everything the window remembers between runs, written on close rather than
// on every change
namespace Settings {

// In View menu order
enum class ViewMode {
    ExtraLargeIcons = 0,
    LargeIcons,
    MediumIcons,
    SmallIcons,
    List,
    Details,
    Tiles,
    Content,
};

int iconSizeFor(ViewMode mode);

QByteArray windowGeometry();
void setWindowGeometry(const QByteArray &state);

QByteArray splitterState();
void setSplitterState(const QByteArray &state);

// Saved as one blob, since restoring order and widths separately would apply a
// width to whichever section sat at that index first
QByteArray headerState();
void setHeaderState(const QByteArray &state);

bool showHiddenFiles();
void setShowHiddenFiles(bool show);

// Win7's hide extensions for known file types, off here by default
bool hideKnownExtensions();
void setHideKnownExtensions(bool hide);

ViewMode defaultViewMode();
void setDefaultViewMode(ViewMode mode);

// Per folder, falling back to the default mode
ViewMode viewModeFor(const QUrl &url);
void setViewModeFor(const QUrl &url, ViewMode mode);

// Typed into the address bar, most recent first, and what F4 drops under it
QStringList recentPaths();
void addRecentPath(const QString &path);

// Win7's use check boxes to select items
bool useCheckBoxes();
void setUseCheckBoxes(bool use);

// Folder Options, View, always show menus
bool alwaysShowMenus();
void setAlwaysShowMenus(bool show);

// Folder Options, General, open each folder in its own window
bool browseInNewWindow();
void setBrowseInNewWindow(bool separate);

// Folder Options, General, single click to open an item, applied by the file
// view directly so the desktop's own setting cannot override it
bool singleClickToOpen();
void setSingleClickToOpen(bool single);

// Folder Options, Search, whether Enter looks inside files as well as at their
// names, off by default since without an index it reads every file it passes
bool searchFileContents();
void setSearchFileContents(bool contents);

// Folder Options, Search, include subfolders in search results, and with it
// off Enter filters this folder rather than starting a recursive search
bool searchSubfolders();
void setSearchSubfolders(bool recursive);

// Folder Options, reset folders
void clearRememberedViewModes();

// Computer needs the difference, its own fallback being Tiles, and the mode
// alone cannot say whether the default applied
bool hasViewModeFor(const QUrl &url);

} // namespace Settings
