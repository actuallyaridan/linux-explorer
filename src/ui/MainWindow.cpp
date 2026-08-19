#include "MainWindow.h"
#include "Assets.h"
#include "AccessDialogs.h"
#include "Branding.h"
#include "ComputerModel.h"
#include "ComputerView.h"
#include "DetailsPane.h"
#include "DirectoryModel.h"
#include "DriveLabel.h"
#include "AboutDialog.h"
#include "Archives.h"
#include "FileOps.h"
#include "FileView.h"
#include "aero/icons.h"
#include "Locations.h"
#include "MapDriveDialog.h"
#include "MountDialog.h"
#include "OptionsDialog.h"
#include "NavigationPane.h"
#include "PreviewPane.h"
#include "Settings.h"
#include "aero/arrows.h"
#include "aero/buttons.h"
#include "aero/strips.h"
#include "aero/text.h"
#include "aero/artwork.h"

#include <KDirModel>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KFilePlacesModel>
#include <KIO/FileSystemFreeSpaceJob>
#include <KIO/FileUndoManager>
#include <KIO/Global>
#include <KNewFileMenu>
#include <KProtocolInfo>
#include <KWindowSystem>

#include <AeroQt/insetwindow.h>
#include <AeroQt/navbuttons.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCompleter>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPair>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

namespace {

// The gap Win7 leaves between the navigation pane and the file list
constexpr int kListLeftMargin = 8;

// Long enough that ordinary local folders never show the placeholder at all
constexpr int kLoadingPlaceholderDelay = 400;

// The address bar's fill runs on a timer rather than on the listing, most
// directories reporting no progress at all and the rest reporting it late, so
// it creeps to a ceiling, waits there, then runs out once the folder lands
constexpr int kProgressCeiling = 80;
constexpr int kProgressTickInterval = 200;
constexpr int kProgressTickStep = 1;          // so the ceiling is sixteen seconds off

// Two pixels is the glass texture's own frame
constexpr int kPathProgressInset = 2;

// Faded rather than solid, the crumbs having to stay readable across it
constexpr qreal kPathProgressOpacity = 0.5;

constexpr int kProgressFinishDuration = 180;

constexpr int kNotificationDelay = 1000;
constexpr int kNotificationSlideDuration = 180;

// Or a directory with thousands of subfolders builds a menu off the screen
constexpr int kMaxCrumbMenuEntries = 60;

constexpr int kMaxRecentEntries = 15;

// In creation order, and owning nothing, windows deleting themselves on close
QList<MainWindow *> g_windows;

} // namespace

int MainWindow::openWindowCount()
{
    return g_windows.size();
}

MainWindow *MainWindow::openWindow(const QUrl &folder, const QList<QUrl> &selection,
                                   const QString &startupId)
{
    const QUrl target = folder.isValid() ? folder : Locations::computer();

    // Two reveal requests for the same folder should end up as one window with
    // both files showing
    MainWindow *window = nullptr;
    for (MainWindow *candidate : std::as_const(g_windows)) {
        if (candidate->currentUrl().matches(target, QUrl::StripTrailingSlash)) {
            window = candidate;
            break;
        }
    }
    if (!window) {
        window = new MainWindow(target);
        window->show();
    }

    if (!selection.isEmpty())
        window->selectOnArrival(selection);

    // Without the caller's token the window is stacked on top but left
    // unfocused, covering what the user was doing and still refusing their keys
    if (!startupId.isEmpty())
        KWindowSystem::setCurrentXdgActivationToken(startupId);
    window->show();
    window->raise();
    if (QWindow *handle = window->windowHandle())
        KWindowSystem::activateWindow(handle);

    return window;
}

QPair<QString, QUrl> MainWindow::driveFor(const QString &path) const
{
    KFilePlacesModel *places = m_places->placesModel();
    QString label;
    QUrl mountUrl;
    int bestLength = -1;

    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        const KFilePlacesModel::GroupType group = places->groupType(index);
        if (group != KFilePlacesModel::DevicesType
            && group != KFilePlacesModel::RemovableDevicesType) {
            continue;
        }

        const QUrl deviceUrl = places->url(index);
        if (!deviceUrl.isLocalFile())
            continue;

        const QString mount = QDir::cleanPath(deviceUrl.toLocalFile());
        const bool isRoot = (mount == QLatin1String("/"));
        if (path != mount && !path.startsWith(isRoot ? mount : mount + QLatin1Char('/')))
            continue;

        // The longest match wins, the nearer mount being the one it is on
        if (mount.length() > bestLength) {
            bestLength = mount.length();
            // Never substituted, friendly mode renaming directories and not
            // drives
            label = DriveLabel::forPlace(places, index);
            mountUrl = deviceUrl;
        }
    }

    if (bestLength < 0) {
        // Nothing in the places model claims this path, which happens before
        // the model has finished populating
        return {tr("Local Disk"), QUrl::fromLocalFile(QStringLiteral("/"))};
    }
    return {label, mountUrl};
}

QList<QPair<QString, QUrl>> MainWindow::crumbsFor(const QUrl &url) const
{
    QList<QPair<QString, QUrl>> crumbs;
    crumbs.append({tr("Computer"), Locations::computer()});

    if (Locations::isComputer(url))
        return crumbs;

    // Search results hang off the folder that was searched
    if (Locations::isSearch(url)) {
        const QUrl folder = Locations::searchFolder(url);
        crumbs = crumbsFor(folder);
        const QString where = crumbs.isEmpty() ? QString() : crumbs.last().first;
        crumbs.append({tr("Search Results in %1").arg(where), url});
        return crumbs;
    }

    // An archive is browsed at the path of the archive file itself, so the
    // trail runs through the real folders and carries on into it
    if (Archives::isInsideArchive(url)) {
        const QUrl archiveFile = Archives::archiveFileFor(url);
        if (archiveFile.isValid()) {
            crumbs = crumbsFor(KIO::upUrl(archiveFile));
            QUrl walk = archiveFile;
            walk.setScheme(url.scheme());
            crumbs.append({archiveFile.fileName(), walk});

            // Whatever is left is the path inside the archive
            const QString inside = url.path().mid(archiveFile.path().length());
            const QStringList segments =
                inside.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            for (const QString &segment : segments) {
                walk.setPath(walk.path() + QLatin1Char('/') + segment);
                crumbs.append({segment, walk});
            }
            return crumbs;
        }
    }

    // An elevated view is an ordinary path served by a different worker, so it
    // gets the ordinary trail, and the targets stay elevated since a crumb
    // dropping back would take the window's privileges away halfway along
    if (url.scheme() == QLatin1String("admin")) {
        QUrl local = url;
        local.setScheme(QStringLiteral("file"));
        crumbs = crumbsFor(local);
        for (QPair<QString, QUrl> &crumb : crumbs) {
            if (crumb.second.isLocalFile())
                crumb.second.setScheme(QStringLiteral("admin"));
        }
        return crumbs;
    }

    if (!url.isLocalFile()) {
        // Remote and virtual locations hang straight off Computer
        crumbs.append({url.scheme() == QLatin1String("trash")
                           ? tr("Recycle Bin") : url.scheme(), url});
        return crumbs;
    }

    const QString path = QDir::cleanPath(url.toLocalFile());

    // Computer, then a drive, then folders, and home is walked through like
    // any other path rather than collapsed to a root of its own
    const QPair<QString, QUrl> drive = driveFor(path);
    crumbs.append(drive);

    const QString mount = QDir::cleanPath(drive.second.toLocalFile());
    const bool mountIsRoot = (mount == QLatin1String("/"));
    QString walk = mountIsRoot ? QString() : mount;
    const QStringList segments = path.mid(mountIsRoot ? 0 : mount.length())
                                     .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : segments) {
        walk += QLatin1Char('/') + segment;
        // The label may be rewritten for friendly naming, the target never
        const QString mapped = Branding::folderName(walk);
        crumbs.append({mapped.isEmpty() ? segment : mapped,
                       QUrl::fromLocalFile(walk)});
    }
    return crumbs;
}

MainWindow::MainWindow(const QUrl &startUrl, QWidget *parent)
    : QMainWindow(parent)
    , m_model(new DirectoryModel(this))
    , m_places(new NavigationPane(this))
{
    // From the navigation pane's places model, so the two agree about which
    // drives exist
    m_computerModel = new ComputerModel(m_places->placesModel(), this);

    g_windows.append(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(700, 460);
    resize(940, 620);

    m_navSound.setSource(QUrl::fromLocalFile(
        "/usr/share/sounds/Windows 7/og/Windows Navigation Start.wav"));
    m_navSound.setVolume(1.0f);

    buildNavigationBar();

    auto *central = new QWidget;
    // ID scoped, a declaration only sheet matching everything and dragging
    // every descendant, scroll bars included, into the stylesheet engine
    central->setObjectName("explorerCentral");
    central->setStyleSheet(Aero::panelSheet(central->objectName(), Aero::Palette::Surface));

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Before the actions and the command bar, which both refer to the file view
    QWidget *body = buildBody();
    buildActions();

    root->addWidget(buildCommandBar());
    root->addWidget(buildSearchAgainBar());
    root->addWidget(buildNotificationBar());
    root->addWidget(body, 1);

    m_details = new DetailsPane;
    root->addWidget(m_details);

    setCentralWidget(central);
    statusBar()->hide();

    // Hidden until Alt is pressed, unless the setting says otherwise
    buildMenuBar();
    menuBar()->setVisible(Settings::alwaysShowMenus());

    // After the central widget is set, and the bar must not also be in that
    // layout, the inset call placing it in the frame's glass area itself
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    Aero::makeInsetWindow(this, central, m_navBar, nullptr);

    setupShortcuts();

    // The whole application, so the side buttons navigate wherever the pointer
    // is, the file list and navigation pane consuming presses themselves
    qApp->installEventFilter(this);

    connect(m_places, &NavigationPane::urlActivated, this, &MainWindow::navigateTo);
    connect(m_places, &NavigationPane::emptyTrashRequested, this,
            [this] { FileOps::emptyTrash(this); });
    connect(m_places, &NavigationPane::connectDrivesRequested,
            this, &MainWindow::showMountDialog);
    connect(m_places, &NavigationPane::mapDriveRequested,
            this, &MainWindow::showMapDriveDialog);
    connect(m_places, &NavigationPane::dropped, this, &MainWindow::handleDrop);

    m_loadingTimer = new QTimer(this);
    m_loadingTimer->setSingleShot(true);
    m_loadingTimer->setInterval(kLoadingPlaceholderDelay);
    connect(m_loadingTimer, &QTimer::timeout, this, [this] {
        // The listing may have finished while the timer was running
        if (!m_loading)
            return;
        if (m_stack->currentWidget() == m_loadingPage)
            m_loadingLabel->show();
        // Same delay as the placeholder, and for the same reason
        layoutPathProgress();
        m_pathProgress->show();
    });

    connect(m_model, &DirectoryModel::loadingStarted, this, [this] {
        m_loading = true;
        m_lastLoadFailed = false;

        // The page swaps in at once and only the label waits, since leaving the
        // list up means watching the batches trickle in and only then being
        // replaced by a placeholder
        m_loadingLabel->hide();
        if (!isComputerView())
            m_stack->setCurrentWidget(m_loadingPage);
        m_loadingTimer->start();

        // Already creeping before the bar is shown, so it arrives part filled
        m_pathProgress->hide();
        m_pathProgress->setValue(0);
        m_pathProgressTimer->start();
        updateListMessage();
    });
    connect(m_model, &DirectoryModel::loadingProgress, this, [this](int percent) {
        // Only forwards, the timer possibly having carried the bar past what
        // the lister reports
        m_pathProgress->setValue(qMax(m_pathProgress->value(), percent));
    });
    connect(m_model, &DirectoryModel::loadingFinished, this, [this] {
        m_loading = false;
        m_loadingTimer->stop();
        // Runs out to full before hiding rather than cutting off partway
        finishPathProgress();
        if (m_stack->currentWidget() == m_loadingPage)
            m_stack->setCurrentWidget(m_listPage);
        // Last chance for a reveal naming something not in this folder
        applyPendingSelection(true);
        updateDetailsPane();
        updateListMessage();
    });

    // A folder created through the new menu goes into rename mode once listed,
    // creating one returning before the lister has seen it
    connect(m_model, &DirectoryModel::itemsAdded, this,
            [this] { applyPendingSelection(false); });

    // The places model populates asynchronously, so on a cold start the drives
    // land after Computer was summarised and the pane would report none
    connect(m_computerModel, &QAbstractItemModel::modelReset, this, [this] {
        if (isComputerView())
            updateDetailsPane();
        // Connecting a drive removes it from the hidden tally
        updateNotification();
    });

    // The same race, the first trail falling back to a generic drive label
    const auto refreshTrail = [this] { setCrumbTrail(currentUrl()); };
    connect(m_places->placesModel(), &QAbstractItemModel::modelReset, this, refreshTrail);
    connect(m_places->placesModel(), &QAbstractItemModel::rowsInserted, this, refreshTrail);
    connect(m_places->placesModel(), &QAbstractItemModel::rowsRemoved, this, refreshTrail);
    connect(m_model, &DirectoryModel::errorOccurred, this,
            [this](int error, const QString &message, const QUrl &url) {
        // An unreadable directory never completes, so the placeholder has to
        // be torn down here too
        m_loading = false;
        m_lastLoadFailed = true;
        m_loadingTimer->stop();
        stopPathProgress();
        if (m_stack->currentWidget() == m_loadingPage)
            m_stack->setCurrentWidget(m_listPage);
        m_details->showMessage(message);
        updateListMessage();
        reportListingFailure(error, url);
    });

    // Named after whatever it will reverse
    auto *undoManager = KIO::FileUndoManager::self();
    connect(undoManager, &KIO::FileUndoManager::undoAvailable, this,
            [this](bool available) { m_actUndo->setEnabled(available); });
    connect(undoManager, &KIO::FileUndoManager::undoTextChanged, this,
            [this](const QString &text) {
        m_actUndo->setText(text.isEmpty() ? tr("Undo") : text);
    });

    const QByteArray geometry = Settings::windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const QByteArray splitter = Settings::splitterState();
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);
    m_fileView->setHeaderState(Settings::headerState());

    // Options the views hold rather than read, so a fresh window agrees with
    // the dialog
    m_fileView->setSingleClickToOpen(Settings::singleClickToOpen());

    // Not a navigation, opening a window not counting as one
    pushHistory(startUrl);
    showLocation(startUrl);
}

MainWindow::~MainWindow()
{
    g_windows.removeOne(this);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Settings::setWindowGeometry(saveGeometry());
    Settings::setSplitterState(m_splitter->saveState());
    Settings::setHeaderState(m_fileView->headerState());
    QMainWindow::closeEvent(event);
}

void MainWindow::buildActions()
{
    // Scoped to the file view rather than the window, a window wide Delete
    // firing with the navigation pane focused and trashing the list's selection
    const auto fileAction = [this](const QString &text, auto slot,
                                   const QKeySequence &key = {}) {
        auto *action = new QAction(text, m_fileView);
        if (!key.isEmpty()) {
            action->setShortcut(key);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            // Parenting only gives ownership, a shortcut being live only once
            // the action is in some widget's own list
            m_fileView->addAction(action);
        }
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    // About the window rather than the selection, so they work whatever has
    // the focus
    const auto windowAction = [this](const QString &text, auto slot,
                                     const QKeySequence &key = {}) {
        auto *action = new QAction(text, this);
        if (!key.isEmpty()) {
            action->setShortcut(key);
            addAction(action);
        }
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    m_actOpen = fileAction(tr("Open"), [this] { openItems(selectedItems()); });
    m_actOpenWith = fileAction(tr("Open with..."), [this] {
        FileOps::openWith(selectedItems(), this);
    });
    m_actCut = fileAction(tr("Cut"), [this] {
        FileOps::cutToClipboard(selectedItems());
    }, QKeySequence::Cut);
    m_actCopy = fileAction(tr("Copy"), [this] {
        FileOps::copyToClipboard(selectedItems());
    }, QKeySequence::Copy);
    m_actPaste = fileAction(tr("Paste"), [this] {
        if (!isComputerView())
            FileOps::pasteFromClipboard(operationFolder(), this);
    }, QKeySequence::Paste);
    m_actCopyPath = fileAction(tr("Copy as path"), [this] {
        FileOps::copyPathToClipboard(selectedItems());
    }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    m_actCreateLink = fileAction(tr("Create shortcut"), [this] {
        FileOps::createLink(selectedUrls(), operationFolder(), this);
    });
    m_actDelete = fileAction(tr("Delete"), [this] {
        FileOps::moveToTrash(selectedUrls(), this);
    }, QKeySequence(Qt::Key_Delete));
    m_actDeleteForever = fileAction(tr("Delete permanently"), [this] {
        FileOps::deletePermanently(selectedUrls(), this);
    }, QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    m_actRename = fileAction(tr("Rename"), &MainWindow::renameSelection,
                             QKeySequence(Qt::Key_F2));
    m_actProperties = fileAction(tr("Properties"), [this] {
        const QList<KFileItem> selection = selectedItems();
        // With nothing selected Win7 reports on the current folder
        if (selection.isEmpty()) {
            if (!isComputerView())
                FileOps::showProperties({KFileItem(operationFolder())}, this);
        } else {
            FileOps::showProperties(selection, this);
        }
    }, QKeySequence(Qt::ALT | Qt::Key_Return));
    m_actRestore = fileAction(tr("Restore"), [this] {
        FileOps::restoreFromTrash(selectedUrls(), this);
    });
    m_actSelectAll = fileAction(tr("Select all"), [this] {
        m_fileView->selectAll();
    }, QKeySequence::SelectAll);
    m_actInvertSelection = fileAction(tr("Invert selection"), [this] {
        m_fileView->invertSelection();
    });

    m_actUndo = windowAction(tr("Undo"), [] { FileOps::undo(); }, QKeySequence::Undo);
    m_actUndo->setEnabled(FileOps::isUndoAvailable());

    m_actRefresh = windowAction(tr("Refresh"), &MainWindow::refresh,
                                QKeySequence(Qt::Key_F5));
    m_actEmptyTrash = windowAction(tr("Empty Recycle Bin"), [this] {
        FileOps::emptyTrash(this);
    });
    m_actOpenTerminal = windowAction(tr("Open in Terminal"), [this] {
        FileOps::openTerminalAt(operationFolder(), this);
    }, QKeySequence(Qt::SHIFT | Qt::Key_F4));
    m_actNewWindow = windowAction(tr("New window"), [this] {
        openNewWindow(currentUrl());
    }, QKeySequence::New);
    m_actClose = windowAction(tr("Close"), [this] { close(); }, QKeySequence::Close);
    m_actUp = windowAction(tr("Up one level"), &MainWindow::goUp,
                           QKeySequence(Qt::ALT | Qt::Key_Up));
    m_actConnectDrives = windowAction(tr("Connect drives..."),
                                      &MainWindow::showMountDialog);
    m_actMapDrive = windowAction(tr("Map network drive..."),
                                 &MainWindow::showMapDriveDialog);
    m_actExtract = windowAction(tr("Extract All..."), &MainWindow::extractSelection);
    m_actOptions = windowAction(tr("Folder and search options"),
                                &MainWindow::showOptionsDialog);
    m_actOpenAsAdmin = windowAction(tr("Run as Administrator"),
                                    &MainWindow::openAsAdministrator);

    m_actPreviewPane = windowAction(tr("Preview pane"), [this](bool on) {
        m_previewPane->setVisible(on);
        if (on)
            m_previewPane->setItems(selectedItems());
    }, QKeySequence(Qt::ALT | Qt::Key_P));
    m_actPreviewPane->setCheckable(true);

    m_actShowHidden = windowAction(tr("Show hidden files"), [this](bool on) {
        m_model->setShowHiddenFiles(on);
    }, QKeySequence(Qt::CTRL | Qt::Key_H));
    m_actShowHidden->setCheckable(true);
    m_actShowHidden->setChecked(Settings::showHiddenFiles());

    m_actHideExtensions = windowAction(tr("Hide extensions for known file types"),
                                       [this](bool on) {
        Settings::setHideKnownExtensions(on);
        m_model->refreshDisplayNames();
    });
    m_actHideExtensions->setCheckable(true);
    m_actHideExtensions->setChecked(Settings::hideKnownExtensions());

    m_actUseCheckBoxes = windowAction(tr("Use check boxes to select items"),
                                      [this](bool on) {
        Settings::setUseCheckBoxes(on);
        m_fileView->setCheckBoxesVisible(on);
    });
    m_actUseCheckBoxes->setCheckable(true);
    m_actUseCheckBoxes->setChecked(Settings::useCheckBoxes());

    // In Win7's own order, on Win7's own shortcuts
    m_viewModeGroup = new QActionGroup(this);
    const std::pair<const char *, Settings::ViewMode> modes[] = {
        {QT_TR_NOOP("Extra large icons"), Settings::ViewMode::ExtraLargeIcons},
        {QT_TR_NOOP("Large icons"),       Settings::ViewMode::LargeIcons},
        {QT_TR_NOOP("Medium icons"),      Settings::ViewMode::MediumIcons},
        {QT_TR_NOOP("Small icons"),       Settings::ViewMode::SmallIcons},
        {QT_TR_NOOP("List"),              Settings::ViewMode::List},
        {QT_TR_NOOP("Details"),           Settings::ViewMode::Details},
        {QT_TR_NOOP("Tiles"),             Settings::ViewMode::Tiles},
        {QT_TR_NOOP("Content"),           Settings::ViewMode::Content},
    };
    int index = 0;
    for (const auto &[label, mode] : modes) {
        auto *action = new QAction(tr(label), this);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT
                                         | Qt::Key(Qt::Key_1 + index)));
        action->setData(int(mode));
        connect(action, &QAction::triggered, this, [this, mode] { setViewMode(mode); });
        m_viewModeGroup->addAction(action);
        addAction(action);
        ++index;
    }

    m_sortGroup = new QActionGroup(this);
    const std::pair<const char *, int> sorts[] = {
        {QT_TR_NOOP("Name"),          DirectoryModel::Name},
        {QT_TR_NOOP("Date modified"), DirectoryModel::ModifiedTime},
        {QT_TR_NOOP("Type"),          DirectoryModel::Type},
        {QT_TR_NOOP("Size"),          DirectoryModel::Size},
    };
    for (const auto &[label, column] : sorts) {
        auto *action = new QAction(tr(label), this);
        action->setCheckable(true);
        action->setData(column);
        connect(action, &QAction::triggered, this, [this, column] {
            m_fileView->sortBy(column, m_model->sortOrder());
        });
        m_sortGroup->addAction(action);
    }

    // Separate from the sort actions, a listing being able to group by type
    // and sort by date at once
    m_groupGroup = new QActionGroup(this);
    const std::pair<const char *, int> groups[] = {
        {QT_TR_NOOP("(None)"),        -1},
        {QT_TR_NOOP("Name"),          DirectoryModel::Name},
        {QT_TR_NOOP("Date modified"), DirectoryModel::ModifiedTime},
        {QT_TR_NOOP("Type"),          DirectoryModel::Type},
        {QT_TR_NOOP("Size"),          DirectoryModel::Size},
    };
    for (const auto &[label, column] : groups) {
        auto *action = new QAction(tr(label), this);
        action->setCheckable(true);
        action->setChecked(column == -1);
        action->setData(column);
        connect(action, &QAction::triggered, this, [this, column] {
            m_fileView->setGroupColumn(column);
        });
        m_groupGroup->addAction(action);
    }

    auto *orderGroup = new QActionGroup(this);
    m_sortAscending = new QAction(tr("Ascending"), this);
    m_sortDescending = new QAction(tr("Descending"), this);
    for (QAction *action : {m_sortAscending, m_sortDescending}) {
        action->setCheckable(true);
        orderGroup->addAction(action);
    }
    connect(m_sortAscending, &QAction::triggered, this, [this] {
        m_fileView->sortBy(m_model->sortColumn(), Qt::AscendingOrder);
    });
    connect(m_sortDescending, &QAction::triggered, this, [this] {
        m_fileView->sortBy(m_model->sortColumn(), Qt::DescendingOrder);
    });

    // The desktop's own menu supplies the folder command, the templates and
    // the naming dialog that avoids collisions
    m_newFileMenu = new KNewFileMenu(this);
    m_newFileMenu->setParentWidget(this);
    m_newFileMenu->setText(tr("New"));
    const auto rememberCreated = [this](const QUrl &url) {
        // Selected and renamed once the lister sees it
        m_pendingSelection = {url};
        m_pendingRename = true;
    };
    connect(m_newFileMenu, &KNewFileMenu::directoryCreated, this, rememberCreated);
    connect(m_newFileMenu, &KNewFileMenu::fileCreated, this, rememberCreated);

    m_itemActions = new KFileItemActions(this);
    m_itemActions->setParentWidget(this);
}

void MainWindow::buildMenuBar()
{
    QMenuBar *bar = menuBar();

    QMenu *file = bar->addMenu(tr("&File"));
    file->addAction(m_newFileMenu);
    file->addSeparator();
    file->addAction(m_actOpen);
    file->addAction(m_actOpenWith);
    file->addSeparator();
    file->addAction(m_actCreateLink);
    file->addAction(m_actRename);
    file->addAction(m_actDelete);
    file->addSeparator();
    file->addAction(m_actProperties);
    file->addSeparator();
    file->addAction(m_actNewWindow);
    file->addAction(m_actClose);

    QMenu *edit = bar->addMenu(tr("&Edit"));
    edit->addAction(m_actUndo);
    edit->addSeparator();
    edit->addAction(m_actCut);
    edit->addAction(m_actCopy);
    edit->addAction(m_actPaste);
    edit->addAction(m_actCopyPath);
    edit->addSeparator();
    edit->addAction(m_actSelectAll);
    edit->addAction(m_actInvertSelection);

    QMenu *view = bar->addMenu(tr("&View"));
    for (QAction *action : m_viewModeGroup->actions())
        view->addAction(action);
    view->addSeparator();
    QMenu *sort = view->addMenu(tr("Sort by"));
    for (QAction *action : m_sortGroup->actions())
        sort->addAction(action);
    sort->addSeparator();
    sort->addAction(m_sortAscending);
    sort->addAction(m_sortDescending);

    QMenu *group = view->addMenu(tr("Group by"));
    for (QAction *action : m_groupGroup->actions())
        group->addAction(action);
    view->addSeparator();
    view->addAction(m_actShowHidden);
    view->addAction(m_actHideExtensions);
    view->addAction(m_actUseCheckBoxes);
    view->addSeparator();
    view->addAction(m_actRefresh);

    QMenu *tools = bar->addMenu(tr("&Tools"));
    tools->addAction(m_actConnectDrives);
    tools->addAction(m_actMapDrive);
    tools->addSeparator();
    tools->addAction(m_actOpenTerminal);
    tools->addAction(m_actOpenAsAdmin);
    tools->addSeparator();
    tools->addAction(m_actEmptyTrash);

    QMenu *go = bar->addMenu(tr("&Go"));
    go->addAction(m_actUp);
    go->addSeparator();
    go->addAction(tr("Computer"), this,
                  [this] { navigateTo(Locations::computer()); });
    go->addAction(tr("Home"), this, [this] {
        navigateTo(QUrl::fromLocalFile(QDir::homePath()));
    });
    go->addAction(tr("Recycle Bin"), this, [this] {
        navigateTo(QUrl(QStringLiteral("trash:/")));
    });

    QMenu *help = bar->addMenu(tr("&Help"));
    help->addAction(tr("About Explorer"), this,
                    &MainWindow::showAboutDialog);
}

void MainWindow::setupShortcuts()
{
    // Shortcuts with no menu entry of their own, all of them window wide since
    // they are about where the keyboard goes rather than the selection
    const auto add = [this](const QList<QKeySequence> &keys, auto slot) {
        auto *action = new QAction(this);
        action->setShortcuts(keys);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        return action;
    };

    // Backspace for back is added because Qt's standard sequence omits it here,
    // and a focused text field claims it first through an override, so binding
    // it window wide is safe
    add({QKeySequence(QKeySequence::Back), QKeySequence(Qt::Key_Backspace)},
        [this] { goBack(); });
    add({QKeySequence(QKeySequence::Forward)}, [this] { goForward(); });

    add({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)},
        &MainWindow::createNewFolder);

    // One of these also drops the history
    add({QKeySequence(Qt::CTRL | Qt::Key_L), QKeySequence(Qt::ALT | Qt::Key_D)},
        [this] { focusPathEdit(false); });
    add({QKeySequence(Qt::Key_F4)}, [this] { focusPathEdit(true); });

    add({QKeySequence(Qt::CTRL | Qt::Key_E), QKeySequence(Qt::CTRL | Qt::Key_F),
         QKeySequence(Qt::Key_F3)},
        &MainWindow::focusSearchBox);

    add({QKeySequence(Qt::Key_F6)}, [this] { cyclePanes(true); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_F6)}, [this] { cyclePanes(false); });

    add({QKeySequence(Qt::Key_F11)}, [this] {
        // Going full screen takes the frame and its glass area with it, and Qt
        // reparents the inset content back so nothing has to be undone here
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });
}

void MainWindow::focusPathEdit(bool dropHistory)
{
    // Already editing, where Win7 selects again rather than doing nothing
    if (m_pathEdit) {
        m_pathEdit->setFocus();
        m_pathEdit->selectAll();
    } else {
        beginPathEdit();
    }

    if (dropHistory)
        showPathHistoryMenu();
}

void MainWindow::showPathHistoryMenu()
{
    const QStringList paths = Settings::recentPaths();
    if (paths.isEmpty() || !m_pathBox)
        return;

    QMenu menu(this);
    for (const QString &path : paths) {
        QAction *action = menu.addAction(path);
        connect(action, &QAction::triggered, this, [this, path] {
            const QUrl target = QUrl::fromUserInput(path, QDir::homePath(),
                                                    QUrl::AssumeLocalFile);
            if (target.isValid())
                navigateTo(target);
        });
    }

    // Under the address box and the same width, the way Win7 drops it
    menu.setMinimumWidth(m_pathBox->width());
    // The editor keeps the keyboard, a menu taking focus for a reason the
    // event filter does not treat as clicking away
    menu.exec(m_pathBox->mapToGlobal(QPoint(0, m_pathBox->height())));
}

void MainWindow::focusSearchBox()
{
    if (!m_searchBox->isEnabled())
        return;   // Computer has no folder to search
    m_searchBox->setFocus();
    m_searchBox->selectAll();
}

void MainWindow::cyclePanes(bool forward)
{
    // In Win7's order, and the address bar is entered by starting an edit, the
    // only way it can hold the keyboard
    enum Pane { NavigationPaneStop, AddressStop, ListStop, SearchStop, StopCount };

    QWidget *focus = qApp->focusWidget();
    int current = ListStop;
    if (m_pathEdit && (focus == m_pathEdit || m_pathEdit->isAncestorOf(focus)))
        current = AddressStop;
    else if (focus == m_searchBox)
        current = SearchStop;
    else if (focus && m_places->isAncestorOf(focus))
        current = NavigationPaneStop;

    // Skips stops that cannot take the keyboard, so on the Computer page this
    // steps past the search box rather than appearing to do nothing
    for (int step = 1; step < StopCount; ++step) {
        const int next = (current + (forward ? step : StopCount - step)) % StopCount;
        switch (next) {
        case NavigationPaneStop:
            m_places->setFocus(Qt::TabFocusReason);
            return;
        case AddressStop:
            focusPathEdit(false);
            return;
        case ListStop:
            if (m_pathEdit)
                endPathEdit();
            if (isComputerView())
                m_computerView->focusView();
            else
                m_fileView->focusView();
            return;
        case SearchStop:
            if (!m_searchBox->isEnabled())
                continue;
            if (m_pathEdit)
                endPathEdit();
            focusSearchBox();
            return;
        }
    }
}

void MainWindow::buildNavigationBar()
{
    m_navBar = new QWidget;
    m_navBar->setFixedHeight(34);
    auto *layout = new QHBoxLayout(m_navBar);
    layout->setContentsMargins(4, 3, 6, 3);
    layout->setSpacing(2);

    // No background at all, the inset call marking this bar transparent so the
    // theme paints its children against the window's glass
    auto *navBtns = new Aero::NavButtons(m_navBar);
    m_backBtn = navBtns->back();
    m_forwardBtn = navBtns->forward();
    m_backBtn->setEnabled(false);
    m_forwardBtn->setEnabled(false);
    connect(m_backBtn, &QPushButton::clicked, this, &MainWindow::goBack);
    connect(m_forwardBtn, &QPushButton::clicked, this, &MainWindow::goForward);
    layout->addWidget(navBtns);

    // The chrome reserves the slot and paints the arrow, and giving it a menu
    // is what makes the button appear, it being hidden until then
    auto *recentMenu = new QMenu(navBtns);
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu] {
        recentMenu->clear();
        int shown = 0;
        for (int i = m_history.size() - 1; i >= 0 && shown < kMaxRecentEntries; --i, ++shown) {
            const QUrl url = m_history.at(i);
            const QList<QPair<QString, QUrl>> crumbs = crumbsFor(url);
            QAction *action = recentMenu->addAction(
                locationIcon(url), crumbs.isEmpty() ? url.toString() : crumbs.last().first);
            action->setCheckable(true);
            action->setChecked(i == m_historyIndex);
            connect(action, &QAction::triggered, this, [this, i] {
                // Moves within the history rather than pushing an entry, so
                // forward can still return
                if (i < 0 || i >= m_history.size())
                    return;
                m_historyIndex = i;
                m_navSound.play();
                showLocation(m_history.at(i));
                updateNavButtons();
            });
        }
    });
    // Taken by reference, so the menu has to be a named variable
    navBtns->setMenu(recentMenu);
    navBtns->menuButton()->setToolTip(tr("Recent locations"));
    layout->addSpacing(4);

    // On the theme's glass texture so the box matches the search field, and
    // scoped by name so the child labels are unaffected
    m_pathBox = new QWidget;
    m_pathBox->setObjectName("addressBox");
    m_pathBox->setStyleSheet(
        "#addressBox { border-image: url(:/AeroQt/transpbg/entry/normal.png) 4 4 4 4 repeat; border-width: 4px; }"
        "#addressBox:hover { border-image: url(:/AeroQt/transpbg/entry/hover.png) 4 4 4 4 repeat; }"
    );
    m_pathBox->setFixedHeight(24);
    // Clicking the bar's empty space starts editing the path
    m_pathBox->setCursor(Qt::IBeamCursor);
    m_pathLayout = new QHBoxLayout(m_pathBox);
    m_pathLayout->setContentsMargins(4, 0, 4, 0);
    m_pathLayout->setSpacing(4);

    // Inside the box rather than in the layout, positioned by hand and lowered
    // behind the crumbs, which have no background and read straight over it
    m_pathProgress = new QProgressBar(m_pathBox);
    m_pathProgress->setRange(0, 100);
    m_pathProgress->setTextVisible(false);
    // A child taking the click would leave a dead strip across the bar
    m_pathProgress->setAttribute(Qt::WA_TransparentForMouseEvents);
    // An effect rather than colours of its own, so the bar stays the real
    // style's rather than an imitation of it
    auto *fade = new QGraphicsOpacityEffect(m_pathProgress);
    fade->setOpacity(kPathProgressOpacity);
    m_pathProgress->setGraphicsEffect(fade);
    m_pathProgress->hide();
    m_pathProgress->lower();

    m_pathProgressTimer = new QTimer(this);
    m_pathProgressTimer->setInterval(kProgressTickInterval);
    connect(m_pathProgressTimer, &QTimer::timeout, this, [this] {
        const int value = m_pathProgress->value();
        if (value < kProgressCeiling)
            m_pathProgress->setValue(qMin(value + kProgressTickStep, kProgressCeiling));
    });

    // One animation rather than one per finish, so a folder abandoned partway
    // can stop the previous run instead of it writing into the next bar
    m_pathProgressFill = new QPropertyAnimation(m_pathProgress, "value", this);
    m_pathProgressFill->setDuration(kProgressFinishDuration);
    m_pathProgressFill->setEndValue(100);
    connect(m_pathProgressFill, &QPropertyAnimation::finished,
            this, &MainWindow::stopPathProgress);

    layout->addWidget(m_pathBox, 1);
    layout->addSpacing(6);

    // The crumb bar is marked transparent by the inset call, so the theme gives
    // a plain text field a glass background that a border of our own would
    // override, and focus swaps to solid white since there is no glass for it
    m_searchBox = new QLineEdit;
    m_searchBox->setPlaceholderText(tr("Search"));
    m_searchBox->setFixedWidth(190);
    m_searchBox->setFixedHeight(24);
    m_searchBox->setClearButtonEnabled(false);
    // The Win7 glyph comes first, with the standard name as the fallback
    m_searchBox->addAction(Aero::themeIcon({"gtk-search", "system-search"}),
                           QLineEdit::TrailingPosition);
    m_searchBox->setToolTip(tr("Filters this folder as you type. Press Enter to "
                               "search subfolders as well."));

    // Italic only while the placeholder shows
    connect(m_searchBox, &QLineEdit::textChanged, m_searchBox,
            [this](const QString &text) {
        QFont f = m_searchBox->font();
        f.setItalic(text.isEmpty());
        m_searchBox->setFont(f);
    });
    QFont searchFont = m_searchBox->font();
    searchFont.setItalic(true);
    m_searchBox->setFont(searchFont);

    connect(qApp, &QApplication::focusChanged, m_searchBox,
            [this](QWidget *, QWidget *now) {
        if (now == m_searchBox) {
            m_searchBox->setStyleSheet(
                QStringLiteral("QLineEdit { border: 1px solid %1; border-radius: 2px;"
                               " background: %2; color: %3; }")
                    .arg(QLatin1String(Aero::Palette::FocusBorder),
                         QLatin1String(Aero::Palette::Surface),
                         QLatin1String(Aero::Palette::Text)));
        } else {
            m_searchBox->setStyleSheet(QString());   // back to AeroQt's glass
        }
    });

    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model->setNameFilter(text);
        updateDetailsPane();
        updateListMessage();
    });
    connect(m_searchBox, &QLineEdit::returnPressed, this, &MainWindow::startSearch);

    layout->addWidget(m_searchBox);
}

void MainWindow::finishPathProgress()
{
    // Never shown, the folder having listed inside the delay
    if (!m_pathProgress->isVisible()) {
        stopPathProgress();
        return;
    }
    m_pathProgressTimer->stop();
    m_pathProgressFill->setStartValue(m_pathProgress->value());
    m_pathProgressFill->start();
}

void MainWindow::stopPathProgress()
{
    m_pathProgressFill->stop();
    m_pathProgressTimer->stop();
    m_pathProgress->hide();
    // Emptied on the way out, or the next folder shows the last one's fill
    m_pathProgress->setValue(0);
}

void MainWindow::layoutPathProgress()
{
    if (!m_pathProgress)
        return;
    m_pathProgress->setGeometry(
        m_pathBox->rect().adjusted(kPathProgressInset, kPathProgressInset,
                                   -kPathProgressInset, -kPathProgressInset));
    m_pathProgress->lower();
}

QIcon MainWindow::locationIcon(const QUrl &url) const
{
    if (Locations::isComputer(url))
        return Aero::themeIcon({"computer", "computer-laptop"});
    if (Locations::isSearch(url))
        return Aero::themeIcon({"system-search", "folder-saved-search"});

    // Prefer the places entry so the glyphs match the pane's, and the lookup
    // matches ancestors too, hence checking the location itself
    KFilePlacesModel *places = m_places->placesModel();
    const QModelIndex index = places->closestItem(url);
    if (index.isValid() && places->url(index).matches(url, QUrl::StripTrailingSlash))
        return places->icon(index);

    return QIcon::fromTheme(KIO::iconNameForUrl(url), Aero::themeIcon({"folder"}));
}

void MainWindow::clearPathLayout()
{
    while (QLayoutItem *item = m_pathLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            m_crumbArrows.remove(w);
            w->deleteLater();
        }
        delete item;
    }
    // Cleared before the deferred deletes run, so anything reacting to the
    // editor losing focus sees edit mode as already finished
    m_pathEdit = nullptr;
}

void MainWindow::beginPathEdit()
{
    if (m_pathEdit)
        return;

    const QUrl url = currentUrl();
    clearPathLayout();

    // An elevated location edits as the plain path it is, and what the user
    // types goes back through the same worker below
    const bool elevated = url.scheme() == QLatin1String("admin");
    m_pathEdit = new QLineEdit(url.isLocalFile() || elevated
                                   ? displayPath(url)
                                   : url.toString());
    m_pathEdit->setFrame(false);
    Aero::setPointSize(m_pathEdit, 9);
    // Transparent, so the editor looks like the bar rather than sits inside it
    m_pathEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none;"
                       " color: %1; }").arg(QLatin1String(Aero::Palette::Text)));
    m_pathLayout->addWidget(m_pathEdit);

    // A filesystem model rather than a completion object, the completer needing
    // a model to pop up from and this one reading a directory only when the
    // typed text reaches it
    auto *fsModel = new QFileSystemModel(m_pathEdit);
    fsModel->setRootPath(QStringLiteral("/"));
    fsModel->setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot);
    auto *completer = new QCompleter(fsModel, m_pathEdit);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_pathEdit->setCompleter(completer);

    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this] {
        QString text = m_pathEdit->text().trimmed();
        if (text.isEmpty()) {
            endPathEdit();
            return;
        }
        if (text.startsWith(QLatin1Char('~')))
            text.replace(0, 1, QDir::homePath());

        QUrl target = QUrl::fromUserInput(text, QDir::homePath(),
                                          QUrl::AssumeLocalFile);

        // Or a nonexistent path pushes a history entry and shows nothing at all
        if (target.isLocalFile()) {
            const QFileInfo info(target.toLocalFile());
            if (info.isFile()) {
                // A file rather than a folder, which Win7 opens and stays put
                endPathEdit();
                FileOps::openItem(KFileItem(target), this);
                return;
            }
            if (!info.isDir()) {
                m_details->showMessage(tr("Windows can't find '%1'. Check the "
                                          "spelling and try again.").arg(text));
                endPathEdit();
                return;
            }
        }

        // Only paths that survived the checks, so the dropdown lists places
        // that exist, and recorded as typed since elevation is the window's
        if (target.isValid())
            Settings::addRecentPath(text);

        // A path typed into an elevated window stays elevated, as its crumbs do
        if (target.isLocalFile() && currentUrl().scheme() == QLatin1String("admin"))
            target.setScheme(QStringLiteral("admin"));

        // Navigating rebuilds the trail and destroys the editor, so when it
        // declines to move the trail has to be put back by hand
        if (target.isValid() && !target.matches(currentUrl(), QUrl::StripTrailingSlash))
            navigateTo(target);
        else
            endPathEdit();
    });

    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}

void MainWindow::endPathEdit()
{
    if (!m_pathEdit)
        return;
    setCrumbTrail(currentUrl());
}

void MainWindow::setCrumbTrail(const QUrl &url)
{
    clearPathLayout();

    // Each arrow drops the children of the crumb to its left, so it is
    // registered against that crumb, and the first one lists the places instead
    const auto addArrow = [this](const QUrl &parent) {
        auto *arrow = new Aero::LinkLabel;
        arrow->setPixmap(Aero::arrowPixmap(Qt::RightArrow,
                                           Aero::Palette::rgb(Aero::Palette::CrumbArrow), 6));
        m_crumbArrows.insert(arrow, parent);
        connect(arrow, &Aero::LinkLabel::clicked, this,
                [this, arrow] { showCrumbMenu(arrow); });
        m_pathLayout->addWidget(arrow);
    };

    auto *icon = new QLabel;
    icon->setPixmap(locationIcon(url).pixmap(16, 16));
    m_pathLayout->addWidget(icon);
    addArrow(QUrl());

    // Intermediate segments are links and the last is plain text
    const QList<QPair<QString, QUrl>> crumbs = crumbsFor(url);
    for (int i = 0; i < crumbs.size(); ++i) {
        if (i > 0)
            addArrow(crumbs.at(i - 1).second);

        if (i == crumbs.size() - 1) {
            m_pathLayout->addWidget(Aero::label(crumbs.at(i).first));
            continue;
        }

        auto *link = new Aero::LinkLabel(crumbs.at(i).first);
        link->setColors(Aero::Palette::Text, Aero::Palette::CrumbHover);
        link->setUnderlineOnHover(true);
        const QUrl target = crumbs.at(i).second;
        connect(link, &Aero::LinkLabel::clicked, this,
                [this, target] { navigateTo(target); });
        m_pathLayout->addWidget(link);
    }

    m_pathLayout->addStretch();
}

void MainWindow::showCrumbMenu(QLabel *arrow)
{
    const QUrl parent = m_crumbArrows.value(arrow);
    QMenu menu(this);

    if (!parent.isValid()) {
        // The leading arrow lists the places
        menu.addAction(Aero::themeIcon({"computer", "computer-laptop"}), tr("Computer"),
                       this, [this] { navigateTo(Locations::computer()); });
        menu.addSeparator();
        KFilePlacesModel *places = m_places->placesModel();
        for (int row = 0; row < places->rowCount(); ++row) {
            const QModelIndex index = places->index(row, 0);
            if (places->isHidden(index) || places->setupNeeded(index))
                continue;
            const QUrl url = places->url(index);
            menu.addAction(places->icon(index), DriveLabel::forPlace(places, index),
                           this, [this, url] { navigateTo(url); });
        }
    } else if (Locations::isComputer(parent)) {
        for (int row = 0; row < m_computerModel->rowCount(); ++row) {
            const QModelIndex index = m_computerModel->index(row, ComputerModel::Name);
            const QUrl url = index.data(ComputerModel::UrlRole).toUrl();
            if (!url.isValid())
                continue;
            menu.addAction(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)),
                           index.data(Qt::DisplayRole).toString(), this,
                           [this, url] { navigateTo(url); });
        }
    } else if (parent.isLocalFile()) {
        QDir dir(parent.toLocalFile());
        dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot
                      | (m_model->showHiddenFiles() ? QDir::Hidden : QDir::Filter(0)));
        dir.setSorting(QDir::Name | QDir::LocaleAware | QDir::IgnoreCase);

        const QIcon folderIcon = Aero::themeIcon({"folder"});
        const QFileInfoList entries = dir.entryInfoList();
        const QString here = QDir::cleanPath(currentUrl().toLocalFile());
        int shown = 0;
        for (const QFileInfo &entry : entries) {
            if (shown++ >= kMaxCrumbMenuEntries)
                break;
            if (Branding::isSystemFolder(entry.absoluteFilePath())
                && !m_model->showHiddenFiles()) {
                continue;
            }
            const QUrl url = QUrl::fromLocalFile(entry.absoluteFilePath());
            QAction *action = menu.addAction(
                folderIcon, Branding::displayName(url, entry.fileName()), this,
                [this, url] { navigateTo(url); });
            // A whole segment at a time, since a bare prefix would tick a
            // folder whenever you were anywhere under a longer name
            const QString candidate = QDir::cleanPath(entry.absoluteFilePath());
            action->setCheckable(true);
            action->setChecked(here == candidate
                               || here.startsWith(candidate + QLatin1Char('/')));
        }
    }

    if (menu.isEmpty())
        return;
    menu.exec(arrow->mapToGlobal(QPoint(0, arrow->height())));
}

QWidget *MainWindow::buildCommandBar()
{
    Aero::Strip *bar = Aero::commandBar(Explorer::Art::CommandBar);
    m_commandLayout = bar->row();

    auto *organize = new Aero::MenuButton(tr("Organize"));
    auto *menu = new QMenu(organize);
    menu->addAction(m_actCut);
    menu->addAction(m_actCopy);
    menu->addAction(m_actPaste);
    menu->addSeparator();
    menu->addAction(m_actUndo);
    menu->addSeparator();
    menu->addAction(m_actRename);
    menu->addAction(m_actDelete);
    menu->addAction(m_actEmptyTrash);
    menu->addSeparator();
    menu->addAction(m_actSelectAll);
    menu->addAction(m_actInvertSelection);
    menu->addSeparator();

    QMenu *layout = menu->addMenu(tr("Layout"));
    auto *menuBarAction = layout->addAction(tr("Menu bar"));
    menuBarAction->setCheckable(true);
    connect(menuBarAction, &QAction::toggled, this,
            [this](bool on) { menuBar()->setVisible(on); });
    layout->addAction(m_actPreviewPane);
    auto *navPaneAction = layout->addAction(tr("Navigation pane"));
    navPaneAction->setCheckable(true);
    navPaneAction->setChecked(true);
    connect(navPaneAction, &QAction::toggled, this,
            [this](bool on) { m_places->setVisible(on); });
    auto *detailsPaneAction = layout->addAction(tr("Details pane"));
    detailsPaneAction->setCheckable(true);
    detailsPaneAction->setChecked(true);
    connect(detailsPaneAction, &QAction::toggled, this,
            [this](bool on) { m_details->setVisible(on); });
    connect(layout, &QMenu::aboutToShow, this,
            [this, menuBarAction, navPaneAction, detailsPaneAction] {
        menuBarAction->setChecked(menuBar()->isVisible());
        navPaneAction->setChecked(m_places->isVisible());
        detailsPaneAction->setChecked(m_details->isVisible());
    });

    // A command rather than a submenu, some settings being radio choices, two
    // depending on each other, and none taking effect until the user says so
    menu->addAction(m_actOptions);

    menu->addSeparator();
    menu->addAction(m_actProperties);
    menu->addAction(m_actClose);
    organize->setMenu(menu);
    m_commandLayout->addWidget(organize);

    auto *newButton = new Aero::MenuButton(tr("New"));
    auto *newMenu = new QMenu(newButton);
    newMenu->addAction(m_newFileMenu);
    connect(newMenu, &QMenu::aboutToShow, this, [this] {
        m_newFileMenu->setWorkingDirectory(operationFolder());
        m_newFileMenu->checkUpToDate();
    });
    newButton->setMenu(newMenu);
    m_commandLayout->addWidget(newButton);

    m_commandLayout->addStretch(1);

    // Shares the view menu's actions, so the two cannot drift apart
    auto *viewButton = new QToolButton;
    viewButton->setPopupMode(QToolButton::InstantPopup);
    viewButton->setCursor(Qt::PointingHandCursor);
    viewButton->setStyleSheet("QToolButton { border: none; background: transparent; }"
                              "QToolButton::menu-indicator { image: none; }");
    viewButton->setIcon(Aero::themeIcon({"view-list-details", "view-list-text", "view-choose"}));
    viewButton->setToolTip(tr("Change your view"));
    auto *viewMenu = new QMenu(viewButton);
    for (QAction *action : m_viewModeGroup->actions())
        viewMenu->addAction(action);
    viewButton->setMenu(viewMenu);
    m_commandLayout->addWidget(viewButton);

    auto *helpButton = new QToolButton;
    helpButton->setCursor(Qt::PointingHandCursor);
    helpButton->setStyleSheet("QToolButton { border: none; background: transparent; }");
    helpButton->setIcon(Aero::themeIcon({"help-contents", "help-browser", "system-help"}));
    helpButton->setToolTip(tr("Help"));
    helpButton->setShortcut(QKeySequence(Qt::Key_F1));
    connect(helpButton, &QToolButton::clicked, this, &MainWindow::showAboutDialog);
    m_commandLayout->addWidget(helpButton);

    return bar;
}

void MainWindow::rebuildContextualCommands()
{
    // Rebuilt rather than shown and hidden, which buttons belong here depending
    // on what is selected
    for (QWidget *widget : std::as_const(m_contextualCommands)) {
        // Removed at once, a deferred delete leaving the widget in the layout
        // so the insert position below would count buttons on their way out
        m_commandLayout->removeWidget(widget);
        widget->hide();
        widget->deleteLater();
    }
    m_contextualCommands.clear();

    if (!m_commandLayout)
        return;

    // Before the stretch, always third from last
    const int insertAt = m_commandLayout->count() - 3;
    const auto addButton = [this, insertAt](const QString &text, QMenu *menu,
                                            auto slot) {
        auto *button = new Aero::MenuButton(text);
        button->setShowArrow(menu != nullptr);
        if (menu) {
            button->setMenu(menu);
        } else {
            button->setPopupMode(QToolButton::DelayedPopup);
            connect(button, &QToolButton::clicked, this, slot);
        }
        m_commandLayout->insertWidget(insertAt + m_contextualCommands.size(), button);
        m_contextualCommands.append(button);
    };

    if (isComputerView())
        return;

    const QList<KFileItem> selection = selectedItems();
    if (isTrashView()) {
        if (!selection.isEmpty()) {
            addButton(tr("Restore this item"), nullptr,
                      [this] { m_actRestore->trigger(); });
        }
        addButton(tr("Empty the Recycle Bin"), nullptr,
                  [this] { m_actEmptyTrash->trigger(); });
        return;
    }

    if (selection.isEmpty())
        return;

    addButton(tr("Open"), nullptr, [this] { m_actOpen->trigger(); });

    // Win7 puts the extract command right here when an archive is picked
    if (selection.size() == 1 && Archives::isBrowsable(selection.first())) {
        addButton(tr("Extract all files"), nullptr,
                  [this] { m_actExtract->trigger(); });
    }

    // Files only, a folder having exactly one thing that opens it
    const bool anyFiles = std::any_of(selection.cbegin(), selection.cend(),
                                      [](const KFileItem &item) { return !item.isDir(); });
    if (anyFiles) {
        auto *openWithMenu = new QMenu(this);
        connect(openWithMenu, &QMenu::aboutToShow, this, [this, openWithMenu] {
            openWithMenu->clear();
            const QList<KFileItem> items = selectedItems();
            if (items.isEmpty())
                return;
            m_itemActions->setItemListProperties(
                KFileItemListProperties(KFileItemList(items)));
            m_itemActions->insertOpenWithActionsTo(nullptr, openWithMenu, {});
        });
        addButton(tr("Open with"), openWithMenu, [] {});
    }

    addButton(tr("Share with"), nullptr, [this] {
        // The desktop's share plugins live in the item's service menu
        const QList<KFileItem> items = selectedItems();
        if (items.isEmpty())
            return;
        QMenu menu(this);
        m_itemActions->setItemListProperties(
            KFileItemListProperties(KFileItemList(items)));
        m_itemActions->addActionsTo(&menu);
        if (menu.isEmpty())
            m_details->showMessage(tr("No sharing services are installed."));
        else
            menu.exec(QCursor::pos());
    });
}

QWidget *MainWindow::buildSearchAgainBar()
{
    // The ways to widen a search, and without them a content search cannot be
    // reached at all, the search box having nowhere to express a scope
    m_searchAgainBar = new QWidget;
    m_searchAgainBar->setObjectName(QStringLiteral("searchAgainBar"));
    m_searchAgainBar->setStyleSheet(
        Aero::panelSheet(m_searchAgainBar->objectName(), Aero::Palette::StripSurface,
                         Qt::BottomEdge, Aero::Palette::StripRule));
    m_searchAgainBar->hide();

    auto *row = new QHBoxLayout(m_searchAgainBar);
    row->setContentsMargins(10, 4, 10, 4);
    row->setSpacing(12);

    row->addWidget(Aero::bodyLabel(tr("Search again in:")));

    const auto link = [this, row](const QString &text, bool contents,
                                  bool wholeMachine) {
        auto *label = qobject_cast<Aero::LinkLabel *>(Aero::bodyLabel(text, true));
        connect(label, &Aero::LinkLabel::clicked, this,
                [this, contents, wholeMachine] { searchAgain(contents, wholeMachine); });
        row->addWidget(label);
        return label;
    };

    link(tr("Computer"), false, true);
    link(tr("File Contents"), true, false);
    row->addStretch(1);

    return m_searchAgainBar;
}

void MainWindow::updateSearchAgainBar()
{
    const QUrl url = currentUrl();
    // Only the search worker can widen the scope
    m_searchAgainBar->setVisible(
        Locations::isSearch(url)
        && KProtocolInfo::isKnownProtocol(QStringLiteral("filenamesearch")));
}

void MainWindow::searchAgain(bool contents, bool wholeMachine)
{
    const QUrl url = currentUrl();
    if (!Locations::isSearch(url))
        return;

    const QString term = Locations::searchTerm(url);
    if (term.isEmpty())
        return;

    // Win7's computer scope is the whole machine, so the filesystem root
    const QUrl folder = wholeMachine ? QUrl::fromLocalFile(QStringLiteral("/"))
                                     : Locations::searchFolder(url);
    navigateTo(Locations::search(folder, term, contents));
}

QWidget *MainWindow::buildNotificationBar()
{
    m_notification = new Aero::NotificationStrip;

    // The strip carries no policy, only reporting the click, and which dialog
    // that means is decided here
    connect(m_notification, &Aero::NotificationStrip::clicked, this, [this] {
        if (m_notice == Notice::Administrator)
            AccessDialogs::showAdministratorWarning(this);
        else
            showMountDialog();
    });
    // Dismissing hides it for the rest of the session
    connect(m_notification, &Aero::NotificationStrip::dismissed, this, [this] {
        m_notificationDismissed = true;
    });

    return m_notification;
}

MainWindow::Notice MainWindow::pendingNotice() const
{
    // Only one strip, and the administrator warning outranks the drives notice
    if (currentUrl().scheme() == QLatin1String("admin"))
        return Notice::Administrator;

    if (isComputerView() && !m_notificationDismissed
        && m_computerModel->unmountedCount() > 0) {
        return Notice::UnmountedDrives;
    }

    return Notice::None;
}

void MainWindow::updateNotification()
{
    if (!m_notification)
        return;

    m_notice = pendingNotice();
    if (m_notice == Notice::None) {
        m_notification->clear();
        return;
    }

    // The drives notice can be waved away, where the administrator one stays
    // for as long as it is true
    m_notification->showMessage(
        m_notice == Notice::Administrator
            ? tr("You're navigating as an administrator, be careful. "
                 "Click for information...")
            : unmountedDrivesNotice(m_computerModel->unmountedCount()),
        m_notice != Notice::Administrator);
}

// Windows counts the drives it found rather than saying some
QString MainWindow::unmountedDrivesNotice(int hidden)
{
    return hidden == 1
        ? tr("A drive is connected to your computer but is not mounted. "
             "Click to change...")
        : tr("%1 drives are connected to your computer but are not mounted. "
             "Click to change...").arg(hidden);
}

QWidget *MainWindow::buildBody()
{
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);

    m_splitter->addWidget(m_places);

    m_fileView = new FileView(m_model);
    connect(m_fileView, &FileView::activated, this, &MainWindow::activateIndex);
    connect(m_fileView, &FileView::contextMenuRequested,
            this, &MainWindow::showContextMenu);
    connect(m_fileView, &FileView::selectionChanged, this, [this] {
        updateDetailsPane();
        updateActionStates();
        rebuildContextualCommands();
        if (m_previewPane->isVisible())
            m_previewPane->setItems(selectedItems());
    });
    connect(m_fileView, &FileView::renameRequested, this, &MainWindow::applyRename);
    connect(m_fileView, &FileView::dropped, this, &MainWindow::handleDrop);
    // Silent, the drag still being in progress and the navigation sound
    // belonging to something the user has finished doing
    connect(m_fileView, &FileView::springLoaded, this, [this](const QUrl &folder) {
        if (folder.isValid() && !folder.matches(currentUrl(), QUrl::StripTrailingSlash)) {
            pushHistory(folder);
            showLocation(folder);
        }
    });
    connect(m_fileView, &FileView::viewModeChanged, this,
            [this](Settings::ViewMode mode) {
        for (QAction *action : m_viewModeGroup->actions())
            action->setChecked(action->data().toInt() == int(mode));
    });
    connect(m_fileView, &FileView::groupColumnChanged, this, [this](int column) {
        // Also fires when picking an icon mode dropped the grouping, hence the
        // menu following the view rather than the other way round
        for (QAction *action : m_groupGroup->actions())
            action->setChecked(action->data().toInt() == column);
    });

    m_computerView = new ComputerView(m_computerModel);
    connect(m_computerView, &ComputerView::urlActivated, this, &MainWindow::navigateTo);
    connect(m_computerView, &ComputerView::selectionChanged,
            this, &MainWindow::updateDetailsPane);
    connect(m_computerView, &ComputerView::contextMenuRequested,
            this, &MainWindow::showComputerContextMenu);
    connect(m_computerView, &ComputerView::viewModeChanged, this,
            [this](Settings::ViewMode mode) {
        for (QAction *action : m_viewModeGroup->actions())
            action->setChecked(action->data().toInt() == int(mode));
    });

    m_listPage = new QWidget;
    m_listPage->setObjectName(QStringLiteral("fileListPage"));
    m_listPage->setStyleSheet(Aero::panelSheet(m_listPage->objectName(), Aero::Palette::Surface));
    auto *listLayout = new QHBoxLayout(m_listPage);
    listLayout->setContentsMargins(kListLeftMargin, 0, 0, 0);
    listLayout->setSpacing(0);
    listLayout->addWidget(m_fileView);

    m_loadingPage = new QWidget;
    m_loadingPage->setObjectName(QStringLiteral("loadingPage"));
    m_loadingPage->setStyleSheet(Aero::panelSheet(m_loadingPage->objectName(), Aero::Palette::Surface));
    auto *loadingLayout = new QVBoxLayout(m_loadingPage);
    loadingLayout->setContentsMargins(0, 0, 0, 0);
    loadingLayout->addSpacing(20);
    m_loadingLabel = Aero::label(tr("Working on it..."), 9);
    loadingLayout->addWidget(m_loadingLabel, 0, Qt::AlignHCenter);
    loadingLayout->addStretch(1);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_listPage);
    m_stack->addWidget(m_computerView);
    m_stack->addWidget(m_loadingPage);
    m_splitter->addWidget(m_stack);

    // In the splitter rather than floating over the list, so its edge resizes
    // the file list as the navigation pane's does
    m_previewPane = new PreviewPane;
    m_previewPane->hide();
    m_splitter->addWidget(m_previewPane);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({190, 750, 250});

    return m_splitter;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Every window installs this filter on the application, so without the
    // check on which is active the first one opened answers for all of them
    if (event->type() == QEvent::MouseButtonPress && isActiveWindow()) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::XButton1) { goBack();    return true; }
        if (me->button() == Qt::XButton2) { goForward(); return true; }
    }

    // Here rather than in the views, both answering it and neither owning what
    // changing mode means
    if (event->type() == QEvent::Wheel && isActiveWindow()) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        auto *widget = qobject_cast<QWidget *>(watched);
        QWidget *zoomable = isComputerView() ? static_cast<QWidget *>(m_computerView)
                                             : static_cast<QWidget *>(m_fileView);
        if ((wheel->modifiers() & Qt::ControlModifier) && widget && zoomable
            && (widget == zoomable || zoomable->isAncestorOf(widget))) {
            zoomViewMode(wheel->angleDelta().y());
            return true;
        }
    }

    // Any other key pressed while it is held cancels this, so the combinations
    // are unaffected
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        m_altAlone = (ke->key() == Qt::Key_Alt);
    } else if (event->type() == QEvent::KeyRelease) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Alt && m_altAlone && isActiveWindow()) {
            m_altAlone = false;
            menuBar()->setVisible(!menuBar()->isVisible());
            return true;
        }
        m_altAlone = false;
    }

    // Crumb links consume their own clicks below, so only the empty space, the
    // leading icon and the current segment reach here
    if (watched == m_pathBox && event->type() == QEvent::MouseButtonPress) {
        beginPathEdit();
        return true;
    }

    // Not in the box's layout, so it has to be resized with it by hand
    if (watched == m_pathBox && event->type() == QEvent::Resize)
        layoutPathProgress();

    if (m_pathEdit && watched == m_pathEdit) {
        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            endPathEdit();
            return true;
        }
        // Clicking away cancels, since committing on focus loss would navigate
        // somewhere the user never pressed Enter on, and the completion popup
        // counts as a focus change too, hence checking the reason
        if (event->type() == QEvent::FocusOut) {
            auto *fe = static_cast<QFocusEvent *>(event);
            if (fe->reason() != Qt::PopupFocusReason) {
                endPathEdit();
                return true;
            }
        }
    }

    // Plain labels for the same reason the crumbs are, Win7's blue task link
    // look not being something a real button can be talked into
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::isComputerView() const
{
    return m_stack && m_stack->currentWidget() == m_computerView;
}

bool MainWindow::isTrashView() const
{
    return currentUrl().scheme() == QLatin1String("trash");
}

QUrl MainWindow::operationFolder() const
{
    const QUrl current = currentUrl();
    // Nothing can be created in a search result, so those commands act on the
    // folder that was searched
    if (Locations::isSearch(current))
        return Locations::searchFolder(current);
    return current;
}

void MainWindow::showLocation(const QUrl &url)
{
    if (Locations::isComputer(url)) {
        // Never handed to KIO, there being no worker, and this page is built
        // in process and always ready so a pending placeholder is cancelled
        m_loading = false;
        m_loadingTimer->stop();
        stopPathProgress();
        m_stack->setCurrentWidget(m_computerView);

        // Win7 opens this page in tiles, so it falls back to its own mode
        // rather than the global default
        m_computerView->setViewMode(Settings::hasViewModeFor(url)
                                        ? Settings::viewModeFor(url)
                                        : Settings::ViewMode::Tiles);
    } else {
        m_stack->setCurrentWidget(m_listPage);
        m_model->setUrl(url);

        // Not through the path a user takes, which would write this folder's
        // mode back out as the default for every folder
        m_fileView->setViewMode(Settings::viewModeFor(url));
    }

    m_places->setCurrentUrl(url);
    setCrumbTrail(url);

    m_fileView->setDestination(operationFolder());

    // A stale filter would narrow the new folder with no visible cause, and
    // search results keep their term, that being what produced them
    if (Locations::isSearch(url)) {
        QSignalBlocker blocker(m_searchBox);
        m_searchBox->setText(Locations::searchTerm(url));
        m_model->setNameFilter(QString());
    } else {
        m_searchBox->clear();
    }
    m_searchBox->setEnabled(!Locations::isComputer(url));

    updateWindowTitle();
    updateDetailsPane();
    updateListMessage();
    updateNotification();
    updateSearchAgainBar();
    updateActionStates();
    rebuildContextualCommands();
}

void MainWindow::navigateTo(const QUrl &url)
{
    if (!url.isValid() || url.matches(currentUrl(), QUrl::StripTrailingSlash))
        return;   // no sound and no history entry for navigating nowhere

    m_navSound.play();
    pushHistory(url);
    showLocation(url);
}

void MainWindow::goBack()
{
    if (m_historyIndex <= 0)
        return;
    m_navSound.play();
    --m_historyIndex;
    showLocation(m_history.at(m_historyIndex));
    updateNavButtons();
}

void MainWindow::goForward()
{
    if (m_historyIndex >= m_history.size() - 1)
        return;
    m_navSound.play();
    ++m_historyIndex;
    showLocation(m_history.at(m_historyIndex));
    updateNavButtons();
}

void MainWindow::goUp()
{
    const QUrl current = currentUrl();
    if (Locations::isComputer(current))
        return;   // Computer is the top of the tree, with nowhere above it

    // The folder that was searched is the only sensible parent for results
    if (Locations::isSearch(current)) {
        navigateTo(Locations::searchFolder(current));
        return;
    }

    // Up out of an archive's root lands in the folder the archive sits in, the
    // ordinary walk carrying on and producing a location nothing can list
    if (Archives::isInsideArchive(current)) {
        const QUrl archiveFile = Archives::archiveFileFor(current);
        if (archiveFile.isValid()
            && QDir::cleanPath(current.path())
                   == QDir::cleanPath(archiveFile.path())) {
            navigateTo(KIO::upUrl(archiveFile));
            return;
        }
    }

    // The ordinary walk stops at the filesystem root, where Win7 goes on to
    // Computer instead
    if (current.isLocalFile()
        && QDir::cleanPath(current.toLocalFile()) == QLatin1String("/")) {
        navigateTo(Locations::computer());
        return;
    }

    const QUrl parent = KIO::upUrl(current);
    if (parent.isValid() && !parent.matches(current, QUrl::StripTrailingSlash))
        navigateTo(parent);
}

void MainWindow::refresh()
{
    if (isComputerView())
        m_computerModel->refresh();
    else
        m_model->refresh();
    m_places->refresh();
}

void MainWindow::pushHistory(const QUrl &url)
{
    // Navigating to the current location again must not grow the history
    if (m_historyIndex >= 0 && m_history.at(m_historyIndex) == url)
        return;
    // Drop any forward entries before branching to a new location
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(url);
    m_historyIndex = m_history.size() - 1;
    updateNavButtons();
}

void MainWindow::updateNavButtons()
{
    if (m_backBtn)
        m_backBtn->setEnabled(m_historyIndex > 0);
    if (m_forwardBtn)
        m_forwardBtn->setEnabled(m_historyIndex < m_history.size() - 1);
}

void MainWindow::updateWindowTitle()
{
    // The last crumb, so the title agrees with the address bar
    const QList<QPair<QString, QUrl>> crumbs = crumbsFor(currentUrl());
    const QString label = crumbs.isEmpty() ? tr("Computer") : crumbs.last().first;
    setWindowTitle(label);
    // Win7's search box names the folder it will search
    m_searchBox->setPlaceholderText(tr("Search %1").arg(label));
}

void MainWindow::startSearch()
{
    const QString term = m_searchBox->text().trimmed();
    const QUrl folder = operationFolder();
    if (term.isEmpty() || isComputerView() || !folder.isValid())
        return;

    // With subfolder search off, Enter does nothing beyond what typing did
    if (!Settings::searchSubfolders())
        return;

    // Without the search worker the navigation fails with a message that says
    // nothing about what to install, and the filter typing applied is still in
    // place so declining leaves the folder narrowed rather than empty
    if (!KProtocolInfo::isKnownProtocol(QStringLiteral("filenamesearch"))) {
        m_details->showMessage(tr("Searching subfolders needs kio-extras. "
                                  "Showing matches in this folder only."));
        return;
    }

    navigateTo(Locations::search(folder, term, Settings::searchFileContents()));
}

void MainWindow::openNewWindow(const QUrl &url)
{
    // The delete on close flag set in the constructor owns this
    auto *window = new MainWindow(url.isValid() ? url : Locations::computer());
    window->show();
}

void MainWindow::showMountDialog()
{
    MountDialog dialog(m_places->placesModel(), this);
    dialog.exec();
}

void MainWindow::showAboutDialog()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::showOptionsDialog()
{
    OptionsDialog dialog(this);

    // Every window, these being application settings
    connect(&dialog, &OptionsDialog::applied, this, [] {
        for (MainWindow *window : std::as_const(g_windows))
            window->applyOptions();
    });

    connect(&dialog, &OptionsDialog::applyViewToAllFolders, this, [this] {
        // This folder's view becomes the default, and clearing the remembered
        // ones is what extends it to folders that already had a preference
        const Settings::ViewMode mode = isComputerView() ? m_computerView->viewMode()
                                                         : m_fileView->viewMode();
        Settings::clearRememberedViewModes();
        Settings::setDefaultViewMode(mode);
        Settings::setViewModeFor(currentUrl(), mode);
    });

    connect(&dialog, &OptionsDialog::resetAllFolders, this, [this] {
        Settings::clearRememberedViewModes();
        for (MainWindow *window : std::as_const(g_windows))
            window->setViewMode(Settings::defaultViewMode());
    });

    dialog.exec();
}

void MainWindow::applyOptions()
{
    m_model->setShowHiddenFiles(Settings::showHiddenFiles());
    m_model->refreshDisplayNames();
    m_fileView->setCheckBoxesVisible(Settings::useCheckBoxes());
    m_fileView->setSingleClickToOpen(Settings::singleClickToOpen());

    // Only ever forces the bar on, hiding it snatching it from a user who had
    // just opened it themselves
    if (Settings::alwaysShowMenus())
        menuBar()->show();

    // The menu actions are a second view of the same settings
    m_actShowHidden->setChecked(Settings::showHiddenFiles());
    m_actHideExtensions->setChecked(Settings::hideKnownExtensions());
    m_actUseCheckBoxes->setChecked(Settings::useCheckBoxes());

    refreshBranding();
    updateActionStates();
}

void MainWindow::showMapDriveDialog()
{
    MapDriveDialog dialog(m_places->placesModel(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Also the only way to find out whether the share is reachable, nothing
    // having been contacted yet and the prompt coming with the first listing
    m_places->refresh();
    navigateTo(dialog.mappedUrl());
}

void MainWindow::setViewMode(Settings::ViewMode mode)
{
    if (isComputerView()) {
        // For this page alone, a mode picked for drives not following the user
        // into folders
        m_computerView->setViewMode(mode);
        Settings::setViewModeFor(currentUrl(), mode);
        return;
    }

    m_fileView->setViewMode(mode);
    // Also the default for folders with no preference of their own
    Settings::setViewModeFor(currentUrl(), mode);
    Settings::setDefaultViewMode(mode);
}

void MainWindow::zoomViewMode(int angleDelta)
{
    // Anything finer than a notch is carried over, so a touchpad still gets
    // there eventually
    m_zoomRemainder += angleDelta;
    const int steps = m_zoomRemainder / 120;
    if (steps == 0)
        return;
    m_zoomRemainder -= steps * 120;

    const Settings::ViewMode current = isComputerView() ? m_computerView->viewMode()
                                                        : m_fileView->viewMode();
    // The modes run largest first, so scrolling up walks the list backwards
    const int next = qBound(int(Settings::ViewMode::ExtraLargeIcons),
                            int(current) - steps,
                            int(Settings::ViewMode::Content));
    if (next == int(current))
        return;

    setViewMode(Settings::ViewMode(next));
    // The menu ticks its own action group, where from the wheel there is none
    updateActionStates();
}

void MainWindow::refreshBranding()
{
    m_model->refreshDisplayNames();
    setCrumbTrail(currentUrl());
    m_places->refresh();
    updateWindowTitle();
}

void MainWindow::updateListMessage()
{
    // Nothing to say while the loading page is up, while the drives are
    // showing, or when the folder failed to open
    if (isComputerView() || m_loading || m_lastLoadFailed || m_model->rowCount() > 0) {
        m_fileView->setStatusMessage(QString());
        return;
    }

    // An empty search result says something different from an empty folder
    if (Locations::isSearch(currentUrl()))
        m_fileView->setStatusMessage(tr("No items match your search."));
    else if (!m_searchBox->text().isEmpty())
        m_fileView->setStatusMessage(tr("No items match your search."));
    else
        m_fileView->setStatusMessage(tr("This folder is empty."));
}

void MainWindow::updateDetailsPane()
{
    if (isComputerView()) {
        const QModelIndex selected = m_computerView->selectedIndex();
        if (selected.isValid())
            m_details->showDrive(selected);
        else
            m_details->showFolderSummary(m_computerView->deviceCount());
        return;
    }

    const QList<KFileItem> selection = selectedItems();
    if (!selection.isEmpty()) {
        m_details->showSelection(selection);
        return;
    }

    // Before the pane is filled in, or a folder on another volume briefly
    // shows the previous figure
    updateFreeSpace();
    m_details->showFolderSummary(m_model->rowCount(), m_freeSpace);
}

void MainWindow::updateFreeSpace()
{
    const QUrl folder = currentUrl();

    // Only a real directory sits on a volume, anything else answering about
    // the wrong filesystem at best
    if (!folder.isLocalFile() || Locations::isSearch(folder)
        || Archives::isInsideArchive(folder)) {
        m_freeSpace.clear();
        return;
    }

    // Once per folder, the figure barely moving and the query not being free
    // on a network mount
    if (folder == m_freeSpaceUrl)
        return;
    m_freeSpaceUrl = folder;
    m_freeSpace.clear();

    // Through a job rather than a direct query, which blocks on an unresponsive
    // mount and would freeze the window on every cleared selection
    KIO::FileSystemFreeSpaceJob *job = KIO::fileSystemFreeSpace(folder);
    connect(job, &KJob::result, this, [this, job, folder] {
        if (job->error() || folder != m_freeSpaceUrl)
            return;   // navigated away while the job was running

        m_freeSpace = tr("%1 free of %2")
                          .arg(KIO::convertSize(job->availableSize()),
                               KIO::convertSize(job->size()));
        // A selection made in the meantime owns the pane now
        if (selectedItems().isEmpty() && !isComputerView())
            m_details->showFolderSummary(m_model->rowCount(), m_freeSpace);
    });
}

bool MainWindow::isFolderWritable(const QUrl &folder) const
{
    if (!folder.isLocalFile())
        return true;   // not our question to answer, KIO reports what it finds

    // Cached per folder, this being called on every selection change, which
    // during a rubber band drag is every mouse move, and the check can stall
    if (folder != m_writableUrl) {
        m_writableUrl = folder;
        m_writable = QFileInfo(folder.toLocalFile()).isWritable();
    }
    return m_writable;
}

void MainWindow::updateActionStates()
{
    const QList<KFileItem> selection = selectedItems();
    const bool any = !selection.isEmpty();
    const bool one = selection.size() == 1;
    const bool computer = isComputerView();
    const bool trash = isTrashView();
    const QUrl folder = operationFolder();

    // The archive workers are read only, so everything that would write has to
    // stand down
    const bool readOnly = Archives::isInsideArchive(folder);

    m_actOpen->setEnabled(any);
    m_actOpenWith->setEnabled(any && !trash);
    m_actCut->setEnabled(any && !trash);
    m_actCopy->setEnabled(any && !trash);
    m_actCopyPath->setEnabled(any);
    m_actCreateLink->setEnabled(any && !trash && folder.isLocalFile());
    m_actPaste->setEnabled(!computer && !trash && !readOnly && FileOps::canPaste());
    m_actDelete->setEnabled(any && !trash && !readOnly);
    m_actDeleteForever->setEnabled(any && !readOnly);
    // Win7 renames a whole selection at once, so this is not a single item
    m_actRename->setEnabled(any && !trash && !readOnly);
    m_newFileMenu->setEnabled(!computer && !trash && !readOnly);
    m_actProperties->setEnabled(!computer);
    m_actRestore->setEnabled(any && trash);
    m_actRestore->setVisible(trash);
    m_actEmptyTrash->setEnabled(trash || !computer);
    m_actSelectAll->setEnabled(!computer);
    m_actInvertSelection->setEnabled(!computer);
    m_actOpenTerminal->setEnabled(folder.isLocalFile());
    m_actUp->setEnabled(!computer);
    m_actUndo->setEnabled(FileOps::isUndoAvailable());

    m_actExtract->setEnabled(
        (one && Archives::isBrowsable(selection.first()))
        || Archives::isInsideArchive(currentUrl()));

    // Only where the user cannot already write, or elevation becomes something
    // people click by reflex, and the admin worker is local only
    const bool alreadyAdmin = currentUrl().scheme() == QLatin1String("admin");
    m_actOpenAsAdmin->setEnabled(folder.isLocalFile() && !alreadyAdmin
                                 && !isFolderWritable(folder));

    // The modes apply to the drives page too, but not the ordering, this menu
    // being over the file list's columns while drives sort from their own
    const Settings::ViewMode mode = computer ? m_computerView->viewMode()
                                             : m_fileView->viewMode();
    for (QAction *action : m_viewModeGroup->actions()) {
        action->setEnabled(true);
        action->setChecked(action->data().toInt() == int(mode));
    }
    for (QAction *action : m_sortGroup->actions()) {
        action->setEnabled(!computer);
        action->setChecked(action->data().toInt() == m_model->sortColumn());
    }
    m_sortAscending->setChecked(m_model->sortOrder() == Qt::AscendingOrder);
    m_sortDescending->setChecked(m_model->sortOrder() == Qt::DescendingOrder);
}

void MainWindow::activateIndex(const QModelIndex &index)
{
    const KFileItem item = m_model->itemForIndex(index);
    if (item.isNull())
        return;
    openItems({item});
}

void MainWindow::openItems(const QList<KFileItem> &items)
{
    // The setting that opens each folder in its own window, folders only
    const bool separateWindows = Settings::browseInNewWindow();

    for (const KFileItem &item : items) {
        if (item.isDir()) {
            if (separateWindows)
                openNewWindow(item.url());
            else
                navigateTo(item.url());
            continue;
        }

        // Anything with no archive worker goes to the desktop's handler
        const QUrl archive = Archives::urlFor(item);
        if (archive.isValid()) {
            if (separateWindows)
                openNewWindow(archive);
            else
                navigateTo(archive);
            continue;
        }

        FileOps::openItem(item, this);
    }
}

void MainWindow::extractSelection()
{
    // Either an archive picked in the list, or the one being browsed
    const QList<KFileItem> selection = selectedItems();
    QUrl archive;
    if (selection.size() == 1 && Archives::isBrowsable(selection.first()))
        archive = Archives::urlFor(selection.first());
    else if (Archives::isInsideArchive(currentUrl()))
        archive = currentUrl();

    if (!archive.isValid())
        return;

    const QUrl archiveFile = Archives::archiveFileFor(archive);
    if (!archiveFile.isValid())
        return;

    // Windows suggests a folder beside the archive, named after it
    const QString name = archiveFile.fileName();
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const QString folder = dot > 0 ? name.left(dot) : name;
    const QString suggested =
        KIO::upUrl(archiveFile).toLocalFile() + QLatin1Char('/') + folder;

    bool ok = false;
    const QString destination = QInputDialog::getText(
        this, tr("Extract Compressed (Zipped) Folders"),
        tr("Files will be extracted to this folder:"), QLineEdit::Normal,
        suggested, &ok);
    if (!ok || destination.isEmpty())
        return;

    FileOps::extractArchive(archive, QUrl::fromLocalFile(destination), this);
}

void MainWindow::openAsAdministrator()
{
    const QString failure = openFolderAsAdministrator(operationFolder());
    if (!failure.isEmpty())
        m_details->showMessage(failure);
}

QString MainWindow::openFolderAsAdministrator(const QUrl &folder)
{
    if (!folder.isLocalFile())
        return tr("Only folders on this computer can be opened as "
                  "administrator.");

    // The admin worker is a separate package, and without this check its
    // absence surfaces as an unsupported protocol
    if (!KProtocolInfo::isKnownProtocol(QStringLiteral("admin")))
        return tr("Opening a folder as administrator needs kio-admin.");

    // A new window, since mixing an elevated view into the same history makes
    // it far too easy to lose track of which one is in front
    QUrl admin = folder;
    admin.setScheme(QStringLiteral("admin"));
    openNewWindow(admin);
    return {};
}

void MainWindow::reportListingFailure(int error, const QUrl &url)
{
    // Anything else is already in the details pane in KIO's own words
    if (!AccessDialogs::isPermissionError(error))
        return;

    const QUrl folder = url.isValid() ? url : currentUrl();
    const QString path = displayPath(folder);

    // Only where elevation could change the answer, an elevated view having
    // nothing above it and a server refusing on its own account, so both of
    // those get Windows' flat refusal instead
    const bool alreadyAdmin = folder.scheme() == QLatin1String("admin");
    if (alreadyAdmin || !folder.isLocalFile()) {
        AccessDialogs::showLocationUnavailable(this, path);
        return;
    }

    // Windows titles this one with the name of the folder that refused
    QString name = folder.fileName();
    if (name.isEmpty())
        name = path;
    // Windows ends the sequence with the same box whether the rights were
    // declined or simply not granted
    if (!AccessDialogs::askForAdminAccess(this, name)) {
        AccessDialogs::showLocationUnavailable(this, path);
        return;
    }

    // Aimed at the folder that refused rather than whatever this window ended
    // up showing
    const QString failure = openFolderAsAdministrator(folder);
    if (!failure.isEmpty())
        AccessDialogs::showLocationUnavailable(this, path, failure);
}

QString MainWindow::displayPath(const QUrl &url)
{
    // The worker's own location is plumbing, and nothing to put in a message
    QUrl local = url;
    if (local.scheme() == QLatin1String("admin"))
        local.setScheme(QStringLiteral("file"));
    return local.toDisplayString(QUrl::PreferLocalFile);
}

void MainWindow::handleDrop(QDropEvent *event, const QUrl &destination)
{
    if (!destination.isValid() || Locations::isComputer(destination))
        return;

    // The job puts up the copy, move and link menu, performs the transfer and
    // records it with the undo manager
    FileOps::dropOn(event, destination, this);
}

void MainWindow::showContextMenu(const QPoint &globalPos, bool onItem)
{
    const QList<KFileItem> selection = selectedItems();
    updateActionStates();

    QMenu menu(this);

    // The views clear the selection when a click misses, so an empty selection
    // means the same as a click on the background
    if (onItem && !selection.isEmpty())
        buildItemContextMenu(menu, selection);
    else
        buildFolderContextMenu(menu);

    menu.exec(globalPos);
}

void MainWindow::buildItemContextMenu(QMenu &menu,
                                      const QList<KFileItem> &selection)
{
    menu.addAction(m_actOpen);
    if (std::any_of(selection.cbegin(), selection.cend(),
                    [](const KFileItem &item) { return !item.isDir(); })) {
        // Also where installed service menus come from
        m_itemActions->setItemListProperties(
            KFileItemListProperties(KFileItemList(selection)));
        m_itemActions->insertOpenWithActionsTo(nullptr, &menu, {});
    }
    menu.addSeparator();

    if (isTrashView()) {
        menu.addAction(m_actRestore);
        menu.addAction(m_actDeleteForever);
        menu.addSeparator();
        menu.addAction(m_actProperties);
        return;
    }

    // Right above cut, where Win7's archive menu puts it
    if (selection.size() == 1 && Archives::isBrowsable(selection.first())) {
        menu.addAction(m_actExtract);
        menu.addSeparator();
    }

    menu.addAction(m_actCut);
    menu.addAction(m_actCopy);
    menu.addAction(m_actCopyPath);
    menu.addAction(m_actCreateLink);
    buildSendToMenu(menu.addMenu(tr("Send to")));

    menu.addSeparator();
    menu.addAction(m_actRename);
    menu.addAction(m_actDelete);

    menu.addSeparator();
    menu.addAction(m_actProperties);

    // Service menus attach to the selection, which a folder menu has none of
    m_itemActions->setItemListProperties(
        KFileItemListProperties(KFileItemList(selection)));
    m_itemActions->addActionsTo(&menu);
}

void MainWindow::buildFolderContextMenu(QMenu &menu)
{
    // Win7's order, arrangement, then clipboard, then what can be created
    QMenu *view = menu.addMenu(tr("View"));
    for (QAction *action : m_viewModeGroup->actions())
        view->addAction(action);

    QMenu *sort = menu.addMenu(tr("Sort by"));
    for (QAction *action : m_sortGroup->actions())
        sort->addAction(action);
    sort->addSeparator();
    sort->addAction(m_sortAscending);
    sort->addAction(m_sortDescending);

    QMenu *group = menu.addMenu(tr("Group by"));
    for (QAction *action : m_groupGroup->actions())
        group->addAction(action);

    menu.addAction(m_actRefresh);

    menu.addSeparator();
    menu.addAction(m_actPaste);

    menu.addSeparator();
    m_newFileMenu->setWorkingDirectory(operationFolder());
    m_newFileMenu->checkUpToDate();
    menu.addAction(m_newFileMenu);

    menu.addSeparator();
    menu.addAction(m_actSelectAll);
    menu.addAction(m_actInvertSelection);

    // Extracting the archive being browsed, from inside it
    if (Archives::isInsideArchive(currentUrl())) {
        menu.addSeparator();
        menu.addAction(m_actExtract);
    }

    if (operationFolder().isLocalFile()) {
        menu.addSeparator();
        menu.addAction(m_actOpenTerminal);
        // Only where it would help, offering it on a writable folder teaching
        // the user to reach for elevation by reflex
        if (m_actOpenAsAdmin->isEnabled())
            menu.addAction(m_actOpenAsAdmin);
    }

    if (isTrashView()) {
        menu.addSeparator();
        menu.addAction(m_actEmptyTrash);
    }

    menu.addSeparator();
    menu.addAction(m_actProperties);
}

void MainWindow::showComputerContextMenu(const QUrl &url, const QPoint &globalPos)
{
    QMenu menu(this);

    const bool mounted = url.isValid();
    if (mounted) {
        menu.addAction(tr("Open"), this, [this, url] { navigateTo(url); });
        menu.addAction(tr("Open in new window"), this,
                       [this, url] { openNewWindow(url); });
        menu.addSeparator();
    }

    // The same route the desktop's device notifier takes, the unmount, the
    // prompt and the reporting all belonging to the system beneath
    KFilePlacesModel *places = m_places->placesModel();
    const QModelIndex placeIndex = m_computerView->placeIndexFor(url);
    if (placeIndex.isValid()) {
        if (places->setupNeeded(placeIndex)) {
            menu.addAction(tr("Connect"), this,
                           [places, placeIndex] { places->requestSetup(placeIndex); });
        } else if (places->isDevice(placeIndex)) {
            // Never the drive the running system is on
            const QString mount = places->url(placeIndex).toLocalFile();
            if (QDir::cleanPath(mount) != QLatin1String("/")) {
                menu.addAction(tr("Eject"), this,
                               [places, placeIndex] { places->requestEject(placeIndex); });
                menu.addAction(tr("Disconnect"), this,
                               [places, placeIndex] { places->requestTeardown(placeIndex); });
            }
        }
        menu.addAction(tr("Rename..."), this, [this, places, placeIndex] {
            // The places model's label, the volume's own belonging to a
            // partition tool
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Rename"), tr("Drive name:"), QLineEdit::Normal,
                places->text(placeIndex), &ok);
            if (ok && !name.isEmpty()) {
                places->editPlace(placeIndex, name, places->url(placeIndex),
                                  places->icon(placeIndex).name());
            }
        });
        menu.addSeparator();
    }

    menu.addAction(m_actConnectDrives);
    menu.addAction(m_actRefresh);

    QMenu *sort = menu.addMenu(tr("Sort by"));
    const std::pair<const char *, ComputerModel::SortKey> keys[] = {
        {QT_TR_NOOP("Name"),       ComputerModel::SortByName},
        {QT_TR_NOOP("Total size"), ComputerModel::SortBySize},
        {QT_TR_NOOP("Free space"), ComputerModel::SortByFree},
    };
    for (const auto &[label, key] : keys) {
        QAction *action = sort->addAction(tr(label));
        action->setCheckable(true);
        action->setChecked(m_computerModel->sortKey() == key);
        connect(action, &QAction::triggered, this,
                [this, key] { m_computerModel->setSortKey(key); });
    }

    if (mounted) {
        menu.addSeparator();
        menu.addAction(tr("Properties"), this, [this, url] {
            FileOps::showProperties({KFileItem(url)}, this);
        });
    }

    menu.exec(globalPos);
}

void MainWindow::buildSendToMenu(QMenu *menu)
{
    const QList<QUrl> urls = selectedUrls();
    if (urls.isEmpty()) {
        menu->setEnabled(false);
        return;
    }

    // Local files only, the archiver working on paths rather than locations
    const bool allLocal = std::all_of(urls.cbegin(), urls.cend(),
                                      [](const QUrl &url) { return url.isLocalFile(); });
    if (allLocal && FileOps::canCompress()) {
        menu->addAction(Aero::themeIcon({"application-zip", "package-x-generic"}),
                        tr("Compressed (zipped) folder"), this, [this, urls] {
            if (!FileOps::compress(urls, this))
                m_details->showMessage(tr("Creating an archive needs Ark."));
        });
        menu->addSeparator();
    }

    const QString desktop = QStandardPaths::writableLocation(
        QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
        menu->addAction(Aero::themeIcon({"user-desktop"}), tr("Desktop (create shortcut)"),
                        this, [this, urls, desktop] {
            FileOps::createLink(urls, QUrl::fromLocalFile(desktop), this);
        });
    }

    const std::pair<QStandardPaths::StandardLocation, const char *> folders[] = {
        {QStandardPaths::DocumentsLocation, "folder-documents"},
        {QStandardPaths::PicturesLocation,  "folder-pictures"},
        {QStandardPaths::MusicLocation,     "folder-music"},
    };
    for (const auto &[location, iconName] : folders) {
        const QString path = QStandardPaths::writableLocation(location);
        if (path.isEmpty())
            continue;
        const QUrl target = QUrl::fromLocalFile(path);
        menu->addAction(Aero::themeIcon({iconName, "folder"}), QDir(path).dirName(),
                        this, [this, urls, target] {
            FileOps::copy(urls, target, this);
        });
    }

    // Removable drives only
    bool separatorAdded = false;
    for (int row = 0; row < m_computerModel->rowCount(); ++row) {
        const QModelIndex index = m_computerModel->index(row, ComputerModel::Name);
        if (!index.data(ComputerModel::RemovableRole).toBool())
            continue;
        const QUrl target = index.data(ComputerModel::UrlRole).toUrl();
        if (!target.isValid())
            continue;
        if (!separatorAdded) {
            menu->addSeparator();
            separatorAdded = true;
        }
        menu->addAction(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)),
                        index.data(Qt::DisplayRole).toString(), this,
                        [this, urls, target] { FileOps::copy(urls, target, this); });
    }
}

void MainWindow::selectOnArrival(const QList<QUrl> &urls)
{
    m_pendingSelection = urls;
    m_pendingRename = false;
    // Tried straight away too, a reveal for the folder already on screen having
    // nothing to wait for
    applyPendingSelection(!m_loading);
}

void MainWindow::applyPendingSelection(bool listingFinished)
{
    if (m_pendingSelection.isEmpty() || isComputerView())
        return;

    if (m_pendingRename) {
        // The new menu's case, straight into the rename editor once listed
        const QUrl target = m_pendingSelection.constFirst();
        if (!m_fileView->selectUrl(target, true)) {
            if (listingFinished)
                m_pendingSelection.clear();
            return;
        }
        m_pendingSelection.clear();
        m_pendingRename = false;
        return;
    }

    m_pendingSelection = m_fileView->selectUrls(m_pendingSelection);

    // Dropped once the listing ends, found or not, or a request left pending
    // hijacks the user's next selection
    if (listingFinished)
        m_pendingSelection.clear();
}

void MainWindow::renameSelection()
{
    const QList<KFileItem> selection = selectedItems();
    if (selection.isEmpty())
        return;

    // A rename over several files opens one editor and applies what it commits
    // to all of them, and they are remembered now since opening the editor
    // moves the current index
    m_batchRename = selection.size() > 1 ? selection : QList<KFileItem>();

    // The editor reports the new name rather than writing to the model, so the
    // rename goes through KIO and lands in the undo history
    m_fileView->renameItem(selection.first().url());
}

void MainWindow::applyRename(const QUrl &url, const QString &newName)
{
    const QList<KFileItem> batch = m_batchRename;
    m_batchRename.clear();

    // Checked against what was actually committed, so a batch left over from an
    // abandoned rename cannot capture the next single one
    const bool sameBatch = std::any_of(batch.cbegin(), batch.cend(),
                                       [&url](const KFileItem &item) {
        return item.url() == url;
    });
    if (batch.size() > 1 && sameBatch) {
        FileOps::renameBatch(batch, newName, this);
        return;
    }

    FileOps::rename(url, newName, this);
}

void MainWindow::createNewFolder()
{
    if (isComputerView())
        return;   // Computer is not a directory, so nothing can be created in it

    m_newFileMenu->setWorkingDirectory(operationFolder());
    m_newFileMenu->createDirectory();
}

QList<KFileItem> MainWindow::selectedItems() const
{
    // The drives page has devices for rows, and its indices bear no relation to
    // the directory model's
    if (!m_fileView || isComputerView())
        return {};
    return m_model->itemsForIndexes(m_fileView->selectedIndexes());
}

QList<QUrl> MainWindow::selectedUrls() const
{
    QList<QUrl> urls;
    const QList<KFileItem> items = selectedItems();
    urls.reserve(items.size());
    for (const KFileItem &item : items)
        urls.append(item.url());
    return urls;
}

QUrl MainWindow::currentUrl() const
{
    return m_historyIndex >= 0 ? m_history.at(m_historyIndex) : QUrl();
}
