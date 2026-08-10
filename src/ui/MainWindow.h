#pragma once

#include "Settings.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QSoundEffect>
#include <QUrl>

#include <KFileItem>

class ComputerModel;
class ComputerView;
class DetailsPane;
class DirectoryModel;
class FileView;
class KFileItemActions;
class KNewFileMenu;
class NavigationPane;
class PreviewPane;
class QAction;
class QActionGroup;
class QDropEvent;
class QMenu;
class QSplitter;
class QStackedWidget;
class QTimer;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPropertyAnimation;
class QModelIndex;
class QPoint;
class QPushButton;
class QWidget;

// The Explorer window: Aero glass navigation bar, command bar, navigation pane
// and file list, details pane along the bottom.
//
// The crumb trail is built by hand out of QLabels rather than with
// KUrlNavigator, whose breadcrumb rendering is not reachable from a
// stylesheet. Dropping it means dropping its history too, so the
// back/forward stack below mirrors the Control Panel's.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QUrl &startUrl, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens `folder`, selecting `selection` in it once it has listed, and
    // raises the window it used. A window already showing that folder is
    // reused rather than duplicated. `startupId` is the caller's activation
    // token, without which the window would be raised but not focused under
    // focus-stealing prevention.
    static MainWindow *openWindow(const QUrl &folder,
                                  const QList<QUrl> &selection = {},
                                  const QString &startupId = QString());

    // How many Explorer windows this process has open.
    static int openWindowCount();

    // Selects `urls` as soon as they turn up in the listing: a reveal request
    // usually arrives while the folder is still being read.
    void selectOnArrival(const QList<QUrl> &urls);

protected:
    // Watches the whole application so the mouse side buttons work wherever
    // the pointer is; see the installEventFilter call in the constructor.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildActions();
    void buildMenuBar();
    void buildNavigationBar();
    QWidget *buildCommandBar();
    QWidget *buildSearchAgainBar();
    QWidget *buildNotificationBar();
    QWidget *buildBody();

    // What the yellow strip has to say. The drives notice belongs to the
    // Computer page; the administrator warning belongs to any elevated
    // location and outranks it.
    enum class Notice { None, Administrator, UnmountedDrives };
    Notice pendingNotice() const;

    // Puts the strip in step with pendingNotice().
    void updateNotification();
    void setupShortcuts();

    // The crumb segments from Computer down to `url`, as (label, target) pairs.
    QList<QPair<QString, QUrl>> crumbsFor(const QUrl &url) const;

    // The drive `path` lives on, as (label, mount point). Resolved from the
    // places model so the trail names drives as Computer and the sidebar do.
    QPair<QString, QUrl> driveFor(const QString &path) const;

    // Rebuilds the address bar for `url`: leading icon, then the clickable
    // trail from the root down to the current folder.
    void setCrumbTrail(const QUrl &url);
    void clearPathLayout();

    // Win7 turns the address bar into an editable path field when it is
    // clicked anywhere that is not a crumb. Enter navigates, Escape and
    // focus loss put the trail back.
    void beginPathEdit();
    void endPathEdit();

    // Ctrl+L, Alt+D and F4. All three enter edit mode with the path selected;
    // F4 also drops the list of previously typed addresses.
    void focusPathEdit(bool dropHistory);
    void showPathHistoryMenu();

    // Ctrl+E, Ctrl+F and F3, all of which Win7 sends to the search box.
    void focusSearchBox();

    // F6 and Shift+F6: navigation pane, address bar, file list, search box.
    void cyclePanes(bool forward);

    // The menu behind a crumb's ▸ arrow: the subfolders of the crumb to its
    // left. The leading icon's arrow lists the places instead.
    void showCrumbMenu(QLabel *arrow);

    // The icon at the head of the address bar, preferring the folder's places
    // entry so Downloads and Music match the navigation pane's glyphs.
    QIcon locationIcon(const QUrl &url) const;

    // Stretches the loading bar across the inside of the address box. Placed
    // by hand, since it sits behind the crumbs rather than beside them.
    void layoutPathProgress();

    // Ends the address bar's fill and resets it for the next folder.
    void stopPathProgress();

    // The same for a folder that finished listing: the bar runs out to full
    // first.
    void finishPathProgress();

    // Renders a location without touching history; navigateTo() is the entry
    // point that also records it.
    void showLocation(const QUrl &url);
    void navigateTo(const QUrl &url);
    void goBack();
    void goForward();
    void goUp();
    void refresh();

    void pushHistory(const QUrl &url);
    void updateNavButtons();
    void updateWindowTitle();

    // Repaints everything showing a folder name after the Windows-friendly
    // naming setting is toggled. Nothing is re-read from disk.
    void refreshBranding();
    void updateDetailsPane();

    // Free space on the current folder's volume, for the details pane's second
    // line. Asynchronous: the query can block for seconds on a dead network
    // mount.
    void updateFreeSpace();

    void updateListMessage();

    // Enables and disables everything that depends on the selection, the
    // location and the clipboard. Kept in one place so the command bar, the
    // menus and the context menu cannot disagree.
    void updateActionStates();

    // Whether the user can write to `folder`, which decides whether "Open as
    // Administrator" is worth offering. Cached; the caller asks on every
    // selection change.
    bool isFolderWritable(const QUrl &folder) const;

    // The command bar's selection-dependent buttons.
    void rebuildContextualCommands();

    void activateIndex(const QModelIndex &index);
    void openItems(const QList<KFileItem> &items);
    // `onItem` says whether the click landed on a row or on the empty space
    // around it, which is what decides which of the two menus below is shown.
    void showContextMenu(const QPoint &globalPos, bool onItem);
    void showComputerContextMenu(const QUrl &url, const QPoint &globalPos);
    void handleDrop(QDropEvent *event, const QUrl &destination);

    // The two file-list context menus. Win7 keeps them apart: the item menu
    // never offers New, Paste or the arrangement commands, the folder menu
    // never offers Open, Cut, Rename or Delete. Neither is the other greyed
    // out, so they are built separately rather than filtered from one list.
    void buildItemContextMenu(QMenu &menu, const QList<KFileItem> &selection);
    void buildFolderContextMenu(QMenu &menu);

    // Win7's Send To: a Desktop shortcut, the user's own folders, and any
    // removable drive currently plugged in.
    void buildSendToMenu(QMenu *menu);

    // Applies whatever m_pendingSelection is waiting for, and drops it once it
    // lands. Also called when the listing ends, so a request naming something
    // absent does not leave the window waiting forever.
    void applyPendingSelection(bool listingFinished);

    void renameSelection();
    void applyRename(const QUrl &url, const QString &newName);
    void createNewFolder();
    void openNewWindow(const QUrl &url);
    void showMountDialog();
    void showMapDriveDialog();

    // Win7's Folder Options, named "Options" here.
    void showOptionsDialog();

    // Behind both the command bar's Help button and the menu bar's About entry.
    void showAboutDialog();

    // Re-reads everything the Options dialog can change. Called for every open
    // window, not just the one the dialog was opened from: the settings belong
    // to the application.
    void applyOptions();

    // Win7's "Extract All...", on the selected archive or the one being
    // browsed. Defaults to a folder beside the archive, named after it.
    void extractSelection();

    // Reopens the current folder through the admin:// worker: kio-admin puts
    // up polkit's authentication dialog, then serves the folder with the
    // privileges it granted.
    void openAsAdministrator();

    // The same for a named folder, for the elevation prompt: it answers for the
    // folder that refused rather than the one on screen. Returns what stopped
    // it, or an empty string on success, leaving the caller to decide how
    // loudly to report it.
    QString openFolderAsAdministrator(const QUrl &folder);

    // Windows' two answers to a folder that would not open: the offer to retry
    // as administrator where that could help, and a flat "Access is denied"
    // where it could not. Non-refusals are left to the details pane.
    void reportListingFailure(int error, const QUrl &url);

    // A location as named to the user: for an elevated view, the plain path
    // rather than the admin:// URL behind it.
    static QString displayPath(const QUrl &url);

    // Runs the search term again with a different scope, from Win7's
    // "Search again in:" strip under the results.
    void searchAgain(bool contents, bool wholeMachine);

    // Shows the "Search again in:" strip only for search results.
    void updateSearchAgainBar();

    // Starts a recursive search of the current folder. This is a navigation:
    // the results are a real KIO location with their own history entry, so
    // Back returns to the folder.
    void startSearch();

    void setViewMode(Settings::ViewMode mode);

    // Ctrl+wheel steps through the eight view modes in View-menu order,
    // scrolling up towards the larger icons.
    void zoomViewMode(int angleDelta);

    QList<KFileItem> selectedItems() const;
    QList<QUrl> selectedUrls() const;
    QUrl currentUrl() const;

    // The folder operations act on: the current location, or the folder a
    // search was started from.
    QUrl operationFolder() const;

    // True while the file list is showing drives rather than a directory. Most
    // file-oriented behaviour has to stand down there: the rows are devices,
    // not KFileItems, and the location is not a real KIO URL.
    bool isComputerView() const;
    bool isTrashView() const;

    DirectoryModel *m_model = nullptr;
    ComputerModel  *m_computerModel = nullptr;
    ComputerView   *m_computerView = nullptr;

    // Holds the directory tree and the Computer page. Swapping whole widgets
    // rather than models keeps the tree bound to the directory model: no
    // hidden-column state bleeding across, no selection model to reconnect.
    QStackedWidget *m_stack = nullptr;

    // The file list wrapped in a margined container. The margin cannot go on
    // the view itself: a stylesheet on a QAbstractScrollArea pulls the whole
    // view into the stylesheet engine, which main.cpp goes to lengths to avoid.
    QWidget *m_listPage = nullptr;

    // Win7's pale yellow strip. In the window rather than the Computer page so
    // it spans the navigation pane too, as Windows puts it.
    QWidget *m_notification = nullptr;
    QLabel  *m_notificationText = nullptr;
    QLabel  *m_notificationDismiss = nullptr;

    // Dismissing hides it for the rest of the session: the drive list is
    // rebuilt on every device event, and a bar that kept coming back would be
    // worse than one that never appeared. Drives notice only; the
    // administrator warning carries no close box.
    bool m_notificationDismissed = false;

    // Which notice the strip is carrying, so its click knows what it offered.
    Notice m_notice = Notice::None;

    // Windows waits a beat after the page settles, then slides the strip down.
    // The timer is the pause, the animation the slide (through maximumHeight,
    // so content below is pushed down rather than covered).
    QTimer *m_notificationTimer = nullptr;
    QPropertyAnimation *m_notificationSlide = nullptr;

    // Shown in place of the list while a slow directory is read, so the user
    // sees one settled state instead of rows trickling in.
    QWidget *m_loadingPage = nullptr;

    // The page is shown the instant a listing starts; only this label waits.
    QLabel *m_loadingLabel = nullptr;

    // Deliberately delayed: most local folders list in a few milliseconds, and
    // flashing a placeholder for that long looks like a glitch. m_loading says
    // whether the listing is still running when the timer fires.
    QTimer *m_loadingTimer = nullptr;
    bool    m_loading = false;

    // An unreadable directory also produces zero rows, but calling it empty
    // would be a lie. The details pane reports the error instead.
    bool m_lastLoadFailed = false;

    NavigationPane *m_places = nullptr;
    QSplitter      *m_splitter = nullptr;
    FileView       *m_fileView = nullptr;
    DetailsPane    *m_details = nullptr;
    PreviewPane    *m_previewPane = nullptr;
    QLineEdit      *m_searchBox = nullptr;

    // Win7's "Search again in:" strip, shown under the command bar only while
    // search results are up, and the scope each of its links stands for. The
    // links are QLabels driven through the event filter, like the crumb trail,
    // so they can carry Win7's blue task-link look.
    QWidget *m_searchAgainBar = nullptr;
    struct SearchScope { bool contents = false; bool wholeMachine = false; };
    QHash<QObject *, SearchScope> m_searchAgainLinks;

    QWidget     *m_navBar = nullptr;
    QWidget     *m_pathBox = nullptr;
    QHBoxLayout *m_pathLayout = nullptr;

    // Fills the address box while a folder is being listed, on the same delay
    // as the "Working on it..." page.
    QProgressBar *m_pathProgress = nullptr;
    QTimer       *m_pathProgressTimer = nullptr;
    QPropertyAnimation *m_pathProgressFill = nullptr;

    // Non-null only in edit mode; setCrumbTrail clears it, since rebuilding
    // the trail destroys the editor.
    QLineEdit *m_pathEdit = nullptr;

    QPushButton *m_backBtn = nullptr;
    QPushButton *m_forwardBtn = nullptr;

    // Each clickable crumb label to the location it navigates to, and each ▸
    // arrow to the folder whose children it drops down. Doubles as the set of
    // labels the event filter may underline on hover; being app-wide, it would
    // otherwise underline every QLabel in the process.
    QHash<QObject *, QUrl> m_crumbLinks;
    QHash<QObject *, QUrl> m_crumbArrows;

    // The command bar, and the contextual buttons rebuilt on selection change.
    QHBoxLayout *m_commandLayout = nullptr;
    QList<QWidget *> m_contextualCommands;

    // Every command the window offers, created once and shared by the menu
    // bar, the Organize menu, the context menu and the command bar, so they
    // cannot disagree about a command's name or enabled state.
    QAction *m_actOpen = nullptr;
    QAction *m_actOpenWith = nullptr;
    QAction *m_actCut = nullptr;
    QAction *m_actCopy = nullptr;
    QAction *m_actPaste = nullptr;
    QAction *m_actCopyPath = nullptr;
    QAction *m_actCreateLink = nullptr;
    QAction *m_actDelete = nullptr;
    QAction *m_actDeleteForever = nullptr;
    QAction *m_actRename = nullptr;
    QAction *m_actProperties = nullptr;
    QAction *m_actUndo = nullptr;
    QAction *m_actSelectAll = nullptr;
    QAction *m_actInvertSelection = nullptr;
    QAction *m_actRefresh = nullptr;
    QAction *m_actRestore = nullptr;
    QAction *m_actEmptyTrash = nullptr;
    QAction *m_actOpenTerminal = nullptr;
    QAction *m_actShowHidden = nullptr;
    QAction *m_actHideExtensions = nullptr;
    QAction *m_actNewWindow = nullptr;
    QAction *m_actClose = nullptr;
    QAction *m_actUp = nullptr;
    QAction *m_actConnectDrives = nullptr;
    QAction *m_actMapDrive = nullptr;
    QAction *m_actPreviewPane = nullptr;
    QAction *m_actExtract = nullptr;
    QAction *m_actOptions = nullptr;
    QAction *m_actOpenAsAdmin = nullptr;
    QAction *m_actUseCheckBoxes = nullptr;

    QActionGroup *m_viewModeGroup = nullptr;
    QActionGroup *m_sortGroup = nullptr;
    QActionGroup *m_groupGroup = nullptr;
    QAction *m_sortAscending = nullptr;
    QAction *m_sortDescending = nullptr;

    // Win7's "New" submenu, which also owns New Folder. Kept alive for the
    // window's lifetime: its creation jobs are asynchronous and report back
    // through it.
    KNewFileMenu *m_newFileMenu = nullptr;

    // Supplies the "Open With" list and the desktop's service menus.
    KFileItemActions *m_itemActions = nullptr;

    // Items to select once the lister reports them: a folder just created
    // through the New menu, or files a "show in folder" request wants
    // revealed. In both cases they are not in the model yet when asked for.
    QList<QUrl> m_pendingSelection;
    bool m_pendingRename = false;

    // The rest of a multiple selection being renamed. F2 edits one item and
    // commits its text as the base name for all of them, so the others are
    // remembered from the moment the editor opened.
    QList<KFileItem> m_batchRename;

    // Leftover wheel movement below one notch. Touchpads and high-resolution
    // wheels send fractions of the 120-unit notch, and rounding each away
    // would make Ctrl+scroll do nothing on that hardware.
    int m_zoomRemainder = 0;

    // Alt on its own shows the classic menu bar, as in Win7. Tracks whether
    // Alt was pressed with no other key in between, so Alt+Tab and Alt+Left do
    // not toggle it.
    bool m_altAlone = false;

    // The details pane's free-space line, and the folder it was measured for.
    QString m_freeSpace;
    QUrl    m_freeSpaceUrl;

    // Whether the current folder is writable, and which folder that was
    // measured for. Mutable so the check can stay a const query.
    mutable QUrl m_writableUrl;
    mutable bool m_writable = true;

    QList<QUrl> m_history;
    int         m_historyIndex = -1;

    QSoundEffect m_navSound;
};
