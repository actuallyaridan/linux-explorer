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
namespace Aero { class NotificationStrip; }
class QLineEdit;
class QProgressBar;
class QPropertyAnimation;
class QModelIndex;
class QPoint;
class QPushButton;
class QWidget;

// The Explorer window
//
// The crumb trail is built by hand out of labels rather than with KDE's own
// navigator, whose rendering is not reachable from a stylesheet, and dropping
// that means dropping its history too, hence the stack below
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QUrl &startUrl, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Reuses a window already showing the folder rather than duplicating it,
    // and without the caller's startup token it is raised but not focused
    static MainWindow *openWindow(const QUrl &folder,
                                  const QList<QUrl> &selection = {},
                                  const QString &startupId = QString());

    static int openWindowCount();

    // A reveal request usually arrives while the folder is still being read
    void selectOnArrival(const QList<QUrl> &urls);

protected:
    // Watches the whole application, so the mouse side buttons work wherever
    // the pointer is
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

    // The administrator warning outranks the drives notice
    enum class Notice { None, Administrator, UnmountedDrives };
    Notice pendingNotice() const;

    static QString unmountedDrivesNotice(int hidden);

    void updateNotification();
    void setupShortcuts();

    // The segments from Computer down to the folder, each with its target
    QList<QPair<QString, QUrl>> crumbsFor(const QUrl &url) const;

    // The label and mount point, resolved from the places model so the trail
    // names drives as Computer and the sidebar do
    QPair<QString, QUrl> driveFor(const QString &path) const;

    void setCrumbTrail(const QUrl &url);
    void clearPathLayout();

    // Win7 turns the address bar into an editable path field when clicked
    // anywhere that is not a crumb
    void beginPathEdit();
    void endPathEdit();

    // On all three of Win7's keys, one of which also drops the typed addresses
    void focusPathEdit(bool dropHistory);
    void showPathHistoryMenu();

    // On all three of Win7's search keys
    void focusSearchBox();

    // Around the four stops, the pane, the address bar, the list and the search
    void cyclePanes(bool forward);

    // The subfolders of the crumb to the arrow's left, and the leading icon's
    // arrow lists the places instead
    void showCrumbMenu(QLabel *arrow);

    // Prefers the folder's places entry, so the glyphs match the pane's
    QIcon locationIcon(const QUrl &url) const;

    // Placed by hand, the bar sitting behind the crumbs rather than beside them
    void layoutPathProgress();

    void stopPathProgress();

    // The same for a folder that finished listing, running out to full first
    void finishPathProgress();

    // Without touching history, where navigating also records it
    void showLocation(const QUrl &url);
    void navigateTo(const QUrl &url);
    void goBack();
    void goForward();
    void goUp();
    void refresh();

    void pushHistory(const QUrl &url);
    void updateNavButtons();
    void updateWindowTitle();

    // Repaints every folder name, and nothing is read again from disk
    void refreshBranding();
    void updateDetailsPane();

    // Asynchronous, the query blocking for seconds on a dead network mount
    void updateFreeSpace();

    void updateListMessage();

    // In one place, so the command bar, the menus and the context menu cannot
    // disagree about what is enabled
    void updateActionStates();

    // Cached, the caller asking on every selection change
    bool isFolderWritable(const QUrl &folder) const;

    // The command bar's selection dependent buttons
    void rebuildContextualCommands();

    void activateIndex(const QModelIndex &index);
    void openItems(const QList<KFileItem> &items);
    // The flag decides which of the two menus below is shown
    void showContextMenu(const QPoint &globalPos, bool onItem);
    void showComputerContextMenu(const QUrl &url, const QPoint &globalPos);
    void handleDrop(QDropEvent *event, const QUrl &destination);

    // Win7 keeps these apart, neither being the other with entries greyed out,
    // so they are built separately rather than filtered from one list
    void buildItemContextMenu(QMenu &menu, const QList<KFileItem> &selection);
    void buildFolderContextMenu(QMenu &menu);

    // A desktop shortcut, the user's folders, and any removable drive
    void buildSendToMenu(QMenu *menu);

    // Also called when the listing ends, so a request naming something absent
    // does not leave the window waiting forever
    void applyPendingSelection(bool listingFinished);

    void renameSelection();
    void applyRename(const QUrl &url, const QString &newName);
    void createNewFolder();
    void openNewWindow(const QUrl &url);
    void showMountDialog();
    void showMapDriveDialog();

    void showOptionsDialog();

    void showAboutDialog();

    // Called for every open window, the settings belonging to the application
    void applyOptions();

    // Win7's extract all, defaulting to a folder beside the archive
    void extractSelection();

    // Reopens the current folder through the admin worker, which puts up the
    // desktop's authentication dialog first
    void openAsAdministrator();

    // The same for the folder that refused rather than the one on screen,
    // returning what stopped it or nothing at all on success
    QString openFolderAsAdministrator(const QUrl &folder);

    // The offer to retry as administrator where that could help, and a flat
    // refusal where it could not, anything else going to the details pane
    void reportListingFailure(int error, const QUrl &url);

    // For an elevated view, the plain path rather than the worker's location
    static QString displayPath(const QUrl &url);

    // From the search again strip under the results
    void searchAgain(bool contents, bool wholeMachine);

    void updateSearchAgainBar();

    // A navigation, the results being a real location with their own history
    // entry, so Back returns to the folder
    void startSearch();

    void setViewMode(Settings::ViewMode mode);

    // Scrolling up steps towards the larger icons
    void zoomViewMode(int angleDelta);

    QList<KFileItem> selectedItems() const;
    QList<QUrl> selectedUrls() const;
    QUrl currentUrl() const;

    // The current location, or the folder a search was started from
    QUrl operationFolder() const;

    // While the list shows drives its rows are devices rather than files and
    // the location is not a real one, so most file behaviour stands down
    bool isComputerView() const;
    bool isTrashView() const;

    DirectoryModel *m_model = nullptr;
    ComputerModel  *m_computerModel = nullptr;
    ComputerView   *m_computerView = nullptr;

    // Swapping whole widgets rather than models keeps the tree bound to the
    // directory model, with no column state bleeding across and nothing to
    // rebind afterwards
    QStackedWidget *m_stack = nullptr;

    // The margin cannot go on the view itself, a stylesheet on a scroll area
    // pulling the whole view into the stylesheet engine
    QWidget *m_listPage = nullptr;

    // In the window rather than the Computer page, so it spans the navigation
    // pane too, and what it carries is decided in one place
    Aero::NotificationStrip *m_notification = nullptr;

    // Dismissing hides it for the session, the drive list being rebuilt on
    // every device event and a bar that kept coming back being worse than none
    bool m_notificationDismissed = false;

    Notice m_notice = Notice::None;

    // Shown while a slow directory is read, so the user sees one settled state
    // instead of rows trickling in
    QWidget *m_loadingPage = nullptr;

    QLabel *m_loadingLabel = nullptr;

    // Delayed, since most local folders list in a few milliseconds and flashing
    // a placeholder for that long looks like a glitch
    QTimer *m_loadingTimer = nullptr;
    bool    m_loading = false;

    // An unreadable directory also produces no rows, but is not empty
    bool m_lastLoadFailed = false;

    NavigationPane *m_places = nullptr;
    QSplitter      *m_splitter = nullptr;
    FileView       *m_fileView = nullptr;
    DetailsPane    *m_details = nullptr;
    PreviewPane    *m_previewPane = nullptr;
    QLineEdit      *m_searchBox = nullptr;

    // Shown under the command bar only while search results are up
    QWidget *m_searchAgainBar = nullptr;

    QWidget     *m_navBar = nullptr;
    QWidget     *m_pathBox = nullptr;
    QHBoxLayout *m_pathLayout = nullptr;

    // Fills the address box while a folder is listed, on the same delay as the
    // placeholder page
    QProgressBar *m_pathProgress = nullptr;
    QTimer       *m_pathProgressTimer = nullptr;
    QPropertyAnimation *m_pathProgressFill = nullptr;

    // Present only in edit mode, since rebuilding the trail destroys the editor
    QLineEdit *m_pathEdit = nullptr;

    QPushButton *m_backBtn = nullptr;
    QPushButton *m_forwardBtn = nullptr;

    // The crumb menu is given the arrow and has to look its folder up, where
    // the crumb links carry their own destination in the handler
    QHash<QObject *, QUrl> m_crumbArrows;

    // The command bar, and the buttons rebuilt on selection change
    QHBoxLayout *m_commandLayout = nullptr;
    QList<QWidget *> m_contextualCommands;

    // Created once and shared by every menu and the command bar, so they cannot
    // disagree about a command's name or enabled state
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

    // Kept for the window's lifetime, its creation jobs being asynchronous and
    // reporting back through it
    KNewFileMenu *m_newFileMenu = nullptr;

    // Supplies the open with list and the desktop's service menus
    KFileItemActions *m_itemActions = nullptr;

    // Items to select once the lister reports them, not being in the model yet
    // when they are asked for
    QList<QUrl> m_pendingSelection;
    bool m_pendingRename = false;

    // A rename edits one item and commits its text as the base name for all of
    // them, so the rest are remembered from the moment the editor opened
    QList<KFileItem> m_batchRename;

    // Touchpads send fractions of a wheel notch, and rounding each away would
    // make zooming do nothing at all on that hardware
    int m_zoomRemainder = 0;

    // Alt on its own shows the classic menu bar, so this tracks whether it was
    // pressed with no other key in between and the combinations still work
    bool m_altAlone = false;

    QString m_freeSpace;
    QUrl    m_freeSpaceUrl;

    // Mutable so the check can stay a const query
    mutable QUrl m_writableUrl;
    mutable bool m_writable = true;

    QList<QUrl> m_history;
    int         m_historyIndex = -1;

    QSoundEffect m_navSound;
};
