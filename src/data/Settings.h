#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QUrl;

// Everything the window remembers between runs, in the same QSettings file
// Branding uses so the app has one config file rather than one per subsystem.
//
// Written on close rather than on every change: column widths and splitter
// positions move continuously during a drag, and syncing each step would mean
// a file write per mouse move.
namespace Settings {

// Windows 7's eight view modes, in View-menu order, which is also the order
// Ctrl+Shift+1..8 selects them in.
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

// The icon edge length each mode draws at. Details and List are row-based and
// use the small 16px icon.
int iconSizeFor(ViewMode mode);

QByteArray windowGeometry();
void setWindowGeometry(const QByteArray &state);

QByteArray splitterState();
void setSplitterState(const QByteArray &state);

// Section order, widths, hidden sections and the sort indicator, which
// QHeaderView packs into one blob. Saved as a unit: restoring them separately
// would apply a width to whichever section sat at that index first.
QByteArray headerState();
void setHeaderState(const QByteArray &state);

bool showHiddenFiles();
void setShowHiddenFiles(bool show);

// Win7's "Hide extensions for known file types", on by default there and off
// here: a Linux filesystem leans on extensions less.
bool hideKnownExtensions();
void setHideKnownExtensions(bool hide);

// The mode used for folders with no remembered preference of their own.
ViewMode defaultViewMode();
void setDefaultViewMode(ViewMode mode);

// Win7 remembers the view mode per folder. Returns defaultViewMode() for a
// folder that has never been given one.
ViewMode viewModeFor(const QUrl &url);
void setViewModeFor(const QUrl &url, ViewMode mode);

// Locations typed into the address bar, most recent first, which is what F4
// drops under it. Typed only, so clicking through folders does not fill it up.
QStringList recentPaths();
void addRecentPath(const QString &path);

// Win7's "Use check boxes to select items": a tick box on each row, so a
// multiple selection needs no keyboard.
bool useCheckBoxes();
void setUseCheckBoxes(bool use);

// Folder Options, View: "Always show menus". Win7 keeps the classic menu bar
// hidden until Alt is pressed; with this on it stays on screen.
bool alwaysShowMenus();
void setAlwaysShowMenus(bool show);

// Folder Options, General: "Open each folder in its own window". Off by
// default, as in Win7.
bool browseInNewWindow();
void setBrowseInNewWindow(bool separate);

// Folder Options, General: "Single-click to open an item", off by default as in
// Win7. Applied by the file view directly rather than through the style's
// activate-on-single-click hint, so the desktop's own setting cannot override
// it.
bool singleClickToOpen();
void setSingleClickToOpen(bool single);

// Folder Options, Search: whether Enter looks inside files as well as at their
// names. Off by default: without an index this reads every file it walks past.
bool searchFileContents();
void setSearchFileContents(bool contents);

// Folder Options, Search: "Include subfolders in search results". With it off,
// Enter in the search box leaves the listing filtered to this folder rather
// than starting a recursive search.
bool searchSubfolders();
void setSearchSubfolders(bool recursive);

// Folder Options' "Reset Folders"; folders fall back to defaultViewMode().
void clearRememberedViewModes();

// Whether this folder has a mode of its own rather than inheriting the default.
// Computer needs the difference: its fallback is Tiles, not defaultViewMode(),
// and viewModeFor() alone cannot say which one applied.
bool hasViewModeFor(const QUrl &url);

} // namespace Settings
