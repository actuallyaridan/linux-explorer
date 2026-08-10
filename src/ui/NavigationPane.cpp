#include "NavigationPane.h"
#include "Branding.h"
#include "DriveLabel.h"
#include "IconHelper.h"
#include "Locations.h"
#include "Win7Ui.h"

#include <KDirWatch>
#include <KFilePlacesModel>

#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

// Marks the stand-in child that gives a collapsed folder its expander,
// replaced by the real subfolders the first time it is opened.
constexpr int kPlaceholderRole = Qt::UserRole + 1;

// The places model row an entry came from, or -1 for the headings and for the
// subfolders discovered by walking the filesystem.
constexpr int kPlaceRowRole = Qt::UserRole + 2;

// Height of the gap Win7 leaves between the groups.
constexpr int kGroupSpacing = 10;

// An empty top-level row rather than extra height on the headings: padding a
// heading moves its text but not its expander triangle, which the tree centres
// in whatever row height it is given. A spacer has no children, so the
// stylesheet's blanked childless-branch rule already draws nothing for it.
QTreeWidgetItem *makeSpacer()
{
    auto *spacer = new QTreeWidgetItem;
    spacer->setFlags(Qt::NoItemFlags);   // not selectable, not clickable
    spacer->setSizeHint(0, QSize(0, kGroupSpacing));
    return spacer;
}

// KDE files Documents, Music, Pictures and Videos under PlacesType alongside
// Desktop and Downloads, but Win7 splits them into Libraries, and no flag
// distinguishes them; matched against the XDG user directories instead.
bool isLibraryPlace(const QUrl &url)
{
    if (!url.isLocalFile())
        return false;

    static const QStandardPaths::StandardLocation locations[] = {
        QStandardPaths::DocumentsLocation,
        QStandardPaths::MusicLocation,
        QStandardPaths::PicturesLocation,
        QStandardPaths::MoviesLocation,
    };

    const QString path = QDir::cleanPath(url.toLocalFile());
    for (const auto location : locations) {
        const QString dir = QStandardPaths::writableLocation(location);
        if (!dir.isEmpty() && QDir::cleanPath(dir) == path)
            return true;
    }
    return false;
}

// Reads one directory's subfolders. On a worker thread: a folder on an
// unresponsive network mount can take seconds to list, which on the GUI thread
// froze the whole window.
QList<QUrl> scanSubdirectories(const QUrl &url)
{
    QList<QUrl> children;
    if (!url.isLocalFile())
        return children;

    QDir dir(url.toLocalFile());
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name | QDir::LocaleAware | QDir::IgnoreCase);

    const QFileInfoList entries = dir.entryInfoList();
    children.reserve(entries.size());
    for (const QFileInfo &entry : entries) {
        // Agreeing with the file list about what Windows-friendly mode hides.
        if (Branding::isSystemFolder(entry.absoluteFilePath()))
            continue;
        children.append(QUrl::fromLocalFile(entry.absoluteFilePath()));
    }
    return children;
}

// Whether an item is on show rather than merely present: a row under a
// collapsed parent is still in the tree, and highlighting it would put the
// selection somewhere the user cannot see.
bool isItemVisible(QTreeWidgetItem *item)
{
    for (QTreeWidgetItem *parent = item->parent(); parent; parent = parent->parent()) {
        if (!parent->isExpanded())
            return false;
    }
    return true;
}

} // namespace

NavigationPane::NavigationPane(QWidget *parent)
    : QWidget(parent)
    , m_places(new KFilePlacesModel(this))
    , m_tree(new QTreeWidget(this))
    , m_watch(new KDirWatch(this))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tree->setColumnCount(1);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    // Off deliberately: uniform heights take the first row's height for every
    // row and ignore the size hints the spacer rows depend on.
    m_tree->setUniformRowHeights(false);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setExpandsOnDoubleClick(false);
    Win7::setPointSize(m_tree, 9);

    // This is a container around the tree; without the proxy, F6 and Tab would
    // land on a widget that cannot take the keyboard and appear to skip the
    // pane entirely.
    setFocusProxy(m_tree);

    // Filtered on the viewport rather than handled by the widget: QTreeWidget
    // would otherwise offer the drop to its own item model, which knows
    // nothing about files.
    m_tree->setAcceptDrops(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->viewport()->installEventFilter(this);

    // Spring-loaded folders: resting a drag on a collapsed branch opens it.
    m_tree->setAutoExpandDelay(700);

    // ID-scoped: a declaration-only sheet acts as `* { ... }` and would pull
    // every descendant, scroll bars included, into the stylesheet engine.
    m_tree->setObjectName("win7NavPane");
    m_tree->setStyleSheet(
        "#win7NavPane { background: #FFFFFF; border-right: 1px solid #D9D9D9; }"
        // No connector lines, just the expander triangle. Only the childless
        // branch states are blanked, those being the dotted elbows and
        // pass-through verticals. :!has-children matters on all three: a
        // heading with children also has siblings and adjoins its item, so a
        // looser selector would blank the expander itself.
        "#win7NavPane::branch:!has-children:has-siblings:!adjoins-item,"
        "#win7NavPane::branch:!has-children:has-siblings:adjoins-item,"
        "#win7NavPane::branch:!has-children:!has-siblings:adjoins-item"
        " { border-image: none; image: none; }"
        // Win7's expander glyphs in place of the style's box indicator. They
        // must come from a resource: the stylesheet engine can only take an
        // image by URL, so a QProxyStyle would never be reached. See win7ui.qrc.
        "#win7NavPane::branch:has-children:!has-siblings:closed,"
        "#win7NavPane::branch:has-children:has-siblings:closed"
        " { border-image: none; image: url(:/win7/branch-closed.png); }"
        "#win7NavPane::branch:open:has-children:!has-siblings,"
        "#win7NavPane::branch:open:has-children:has-siblings"
        " { border-image: none; image: url(:/win7/branch-open.png); }");

    // Single click activates here, unlike the file list. Headings that are not
    // themselves locations toggle instead.
    connect(m_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
        if (item->flags() == Qt::NoItemFlags)
            return;   // a spacer row between groups

        const QUrl url = urlForItem(item);
        if (url.isValid())
            Q_EMIT urlActivated(url);
        else
            item->setExpanded(!item->isExpanded());
    });

    // Opening or closing a branch moves the highlight, which sits on the
    // deepest row on show leading to the current folder.
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        const QUrl url = urlForItem(item);
        if (url.isValid())
            m_expanded.insert(url);
        populateChildren(item);
        syncHighlight();
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        const QUrl url = urlForItem(item);
        if (url.isValid())
            m_expanded.remove(url);
        syncHighlight();
    });

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &NavigationPane::showContextMenu);

    // A folder added or removed elsewhere, including by our own file list,
    // repopulates the item watching it.
    connect(m_watch, &KDirWatch::dirty, this, [this](const QString &path) {
        QTreeWidgetItem *item = itemForUrl(QUrl::fromLocalFile(path));
        if (!item || !item->isExpanded())
            return;
        // populateChildren only acts on an item whose sole child is the
        // placeholder, so the marker goes back first.
        while (item->childCount() > 0)
            delete item->takeChild(0);
        auto *placeholder = new QTreeWidgetItem(item);
        placeholder->setData(0, kPlaceholderRole, true);
        populateChildren(item);
    });

    // The model repopulates asynchronously and on every device plug/unplug, so
    // the tree is rebuilt rather than patched.
    connect(m_places, &QAbstractItemModel::modelReset, this, &NavigationPane::rebuild);
    connect(m_places, &QAbstractItemModel::rowsInserted, this, &NavigationPane::rebuild);
    connect(m_places, &QAbstractItemModel::rowsRemoved, this, &NavigationPane::rebuild);
    connect(m_places, &QAbstractItemModel::dataChanged, this, &NavigationPane::rebuild);

    root->addWidget(m_tree);
    rebuild();
}

KFilePlacesModel *NavigationPane::placesModel() const
{
    return m_places;
}

void NavigationPane::refresh()
{
    rebuild();
}

QUrl NavigationPane::urlForItem(QTreeWidgetItem *item) const
{
    return item ? item->data(0, Qt::UserRole).toUrl() : QUrl();
}

QTreeWidgetItem *NavigationPane::itemForUrl(const QUrl &url) const
{
    if (!url.isValid())
        return nullptr;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if (urlForItem(*it).matches(url, QUrl::StripTrailingSlash))
            return *it;
    }
    return nullptr;
}

QTreeWidgetItem *NavigationPane::addGroup(const QString &title,
                                          std::initializer_list<const char *> iconNames,
                                          const QUrl &url)
{
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, title);
    item->setIcon(0, themeIcon(iconNames));
    item->setData(0, kPlaceRowRole, -1);
    if (url.isValid()) {
        item->setData(0, Qt::UserRole, url);
    } else {
        // Headings, not destinations: enabled so clicks reach the toggle
        // handler, never selectable.
        item->setFlags(Qt::ItemIsEnabled);
    }
    return item;
}

void NavigationPane::addEntry(QTreeWidgetItem *group, const QModelIndex &placeIndex)
{
    const QUrl url = m_places->url(placeIndex);

    // A heading that is itself a destination would otherwise list a child
    // pointing at the same place, as KDE's "Network" entry does. Win7 shows
    // that as one row.
    const QUrl groupUrl = group->data(0, Qt::UserRole).toUrl();
    if (groupUrl.isValid() && groupUrl.matches(url, QUrl::StripTrailingSlash))
        return;

    auto *item = new QTreeWidgetItem(group);
    // DriveLabel appends the device node for a drive and hands back the plain
    // label for everything else, so one call covers every kind of place.
    item->setText(0, Branding::displayName(url, DriveLabel::forPlace(m_places, placeIndex)));
    item->setIcon(0, m_places->icon(placeIndex));
    item->setData(0, Qt::UserRole, url);
    item->setData(0, kPlaceRowRole, placeIndex.row());
    addPlaceholderIfExpandable(item, url);
}

void NavigationPane::addPlaceholderIfExpandable(QTreeWidgetItem *item, const QUrl &url)
{
    // Local directories only: trash:/ and smb:// cannot be walked with QDir,
    // and doing it properly would mean a KIO job per node.
    if (!url.isLocalFile())
        return;

    const QFileInfo info(url.toLocalFile());
    if (!info.isDir() || !info.isReadable())
        return;

    auto *placeholder = new QTreeWidgetItem(item);
    placeholder->setData(0, kPlaceholderRole, true);
}

void NavigationPane::populateChildren(QTreeWidgetItem *item)
{
    // Nothing to do unless the only child is the stand-in.
    if (item->childCount() != 1 || !item->child(0)->data(0, kPlaceholderRole).toBool())
        return;

    const QUrl url = urlForItem(item);
    if (!url.isLocalFile() || m_scanning.contains(url))
        return;

    m_scanning.insert(url);

    // Nothing captures the item pointer: a rebuild while the scan runs would
    // leave it dangling, so the result is matched back up by URL.
    auto *watcher = new QFutureWatcher<QList<QUrl>>(this);
    connect(watcher, &QFutureWatcher<QList<QUrl>>::finished, this,
            [this, watcher, url] {
        const QList<QUrl> children = watcher->result();
        watcher->deleteLater();
        m_scanning.remove(url);
        applyChildren(url, children);
    });
    watcher->setFuture(QtConcurrent::run(scanSubdirectories, url));
}

void NavigationPane::applyChildren(const QUrl &url, const QList<QUrl> &children)
{
    QTreeWidgetItem *item = itemForUrl(url);
    if (!item)
        return;   // the tree was rebuilt while the scan was running

    // Anything but the lone placeholder means the item was repopulated while
    // this scan was running.
    if (item->childCount() != 1 || !item->child(0)->data(0, kPlaceholderRole).toBool())
        return;

    delete item->takeChild(0);

    const QIcon folderIcon = themeIcon({"folder"});
    for (const QUrl &childUrl : children) {
        auto *child = new QTreeWidgetItem(item);
        child->setText(0, Branding::displayName(childUrl, childUrl.fileName()));
        child->setIcon(0, folderIcon);
        child->setData(0, Qt::UserRole, childUrl);
        child->setData(0, kPlaceRowRole, -1);
        addPlaceholderIfExpandable(child, childUrl);

        // Expanding schedules that child's own scan, so a deep tree comes back
        // one level per round.
        if (m_expanded.contains(childUrl))
            child->setExpanded(true);
    }

    if (url.isLocalFile() && !m_watch->contains(url.toLocalFile()))
        m_watch->addDir(url.toLocalFile());

    // The new rows may include the next step towards the current folder. (A
    // folder that turned out to have none keeps no expander: Qt drops it once
    // the last child is gone.)
    syncHighlight();
}

void NavigationPane::rebuild()
{
    m_tree->clear();

    QTreeWidgetItem *favorites = addGroup(tr("Favorites"), {"favorites", "bookmarks", "starred"});
    QTreeWidgetItem *libraries = addGroup(tr("Libraries"), {"folder-library", "folder-documents", "folder"});
    QTreeWidgetItem *computer  = addGroup(tr("Computer"), {"computer", "computer-laptop"},
                                          Locations::computer());
    QTreeWidgetItem *network   = addGroup(tr("Network"), {"folder-network", "network-workgroup", "network-server"},
                                          QUrl(QStringLiteral("remote:/")));

    for (int row = 0; row < m_places->rowCount(); ++row) {
        const QModelIndex index = m_places->index(row, 0);
        if (m_places->isHidden(index))
            continue;

        const QUrl placeUrl = m_places->url(index);

        QTreeWidgetItem *group = nullptr;
        switch (m_places->groupType(index)) {
        case KFilePlacesModel::PlacesType:
            // Win7's Favorites has no "Home": the folder is reached through
            // Computer > drive > Users and its contents through Libraries, so
            // an entry here would be a third route to the same place.
            if (placeUrl.isLocalFile()
                && QDir::cleanPath(placeUrl.toLocalFile()) == QDir::homePath()) {
                continue;
            }
            group = isLibraryPlace(placeUrl) ? libraries : favorites;
            break;
        case KFilePlacesModel::RecentlySavedType:
        case KFilePlacesModel::SearchForType:
            group = favorites;
            break;
        case KFilePlacesModel::DevicesType:
        case KFilePlacesModel::RemovableDevicesType:
            // Left out here as on the Computer page, so the two agree about
            // what exists. Connecting one is on the Computer heading's menu.
            if (m_places->setupNeeded(index))
                continue;
            group = computer;
            break;
        case KFilePlacesModel::RemoteType:
            group = network;
            break;
        default:
            continue;   // tags and anything new upstream adds
        }
        addEntry(group, index);
    }

    // Win7 opens with the groups expanded, and an empty heading would sit there
    // with an expander revealing nothing.
    for (QTreeWidgetItem *group : {favorites, libraries, computer, network}) {
        if (group->childCount() > 0)
            group->setExpanded(true);
        else if (!group->data(0, Qt::UserRole).toUrl().isValid())
            delete group;   // a heading with neither children nor a destination
    }

    // Each expand starts a scan whose children reopen in turn as results land,
    // so an arbitrarily deep branch comes back without blocking here.
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const QUrl url = urlForItem(*it);
        if (url.isValid() && m_expanded.contains(url))
            (*it)->setExpanded(true);
    }

    // The gap belongs to the heading below it, so every heading gets one
    // including the first. Inserted only once the empty headings are gone, so a
    // dropped group leaves no double gap; backwards, so the indices ahead of
    // the cursor stay valid as rows are inserted.
    for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i)
        m_tree->insertTopLevelItem(i, makeSpacer());

    setCurrentUrl(m_currentUrl);
}

void NavigationPane::setCurrentUrl(const QUrl &url)
{
    m_currentUrl = url;
    syncHighlight();
}

QTreeWidgetItem *NavigationPane::highlightTarget() const
{
    if (!m_currentUrl.isValid())
        return nullptr;

    const bool local = m_currentUrl.isLocalFile();
    const QString target = local ? QDir::cleanPath(m_currentUrl.toLocalFile())
                                 : QString();

    // Every row rather than the places model's closest entry: the tree also
    // carries items the model has no row for, both headings that are
    // destinations (Computer) and every subfolder found on the filesystem.
    QTreeWidgetItem *best = nullptr;
    int bestLength = -1;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const QUrl url = urlForItem(*it);
        if (!url.isValid() || !isItemVisible(*it))
            continue;

        // The folder itself, wherever it turns up: a Favorites entry counts,
        // and beats any ancestor.
        if (url.matches(m_currentUrl, QUrl::StripTrailingSlash))
            return *it;

        if (!local || !url.isLocalFile())
            continue;

        const QString path = QDir::cleanPath(url.toLocalFile());
        const QString prefix = path.endsWith(QLatin1Char('/')) ? path
                                                               : path + QLatin1Char('/');
        if (!target.startsWith(prefix))
            continue;
        if (path.length() > bestLength) {
            bestLength = path.length();
            best = *it;
        }
    }
    return best;
}

void NavigationPane::syncHighlight()
{
    // Nothing is ever expanded from here: the pane marks where the current
    // folder lies rather than opening itself up to it.
    QTreeWidgetItem *item = highlightTarget();
    if (m_tree->currentItem() == item)
        return;

    QSignalBlocker blocker(m_tree);
    m_tree->clearSelection();
    m_tree->setCurrentItem(item);
    if (item)
        m_tree->scrollToItem(item);
}

// ---- Context menu -----------------------------------------------------------

void NavigationPane::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item || item->flags() == Qt::NoItemFlags)
        return;

    const QUrl url = urlForItem(item);
    const int placeRow = item->data(0, kPlaceRowRole).toInt();
    const QModelIndex placeIndex = placeRow >= 0 ? m_places->index(placeRow, 0)
                                                 : QModelIndex();

    QMenu menu(this);

    if (url.isValid()) {
        menu.addAction(tr("Open"), this, [this, url] { Q_EMIT urlActivated(url); });
        menu.addSeparator();
    }

    if (url.scheme() == QLatin1String("trash")) {
        menu.addAction(tr("Empty Recycle Bin"), this,
                       [this] { Q_EMIT emptyTrashRequested(); });
        menu.addSeparator();
    }

    if (Locations::isComputer(url)) {
        menu.addAction(tr("Connect drives..."), this,
                       [this] { Q_EMIT connectDrivesRequested(); });
        menu.addSeparator();
    }

    if (url.scheme() == QLatin1String("remote")) {
        menu.addAction(tr("Map network drive..."), this,
                       [this] { Q_EMIT mapDriveRequested(); });
        menu.addSeparator();
    }

    // Only entries from the places model can be managed; a subfolder found on
    // the filesystem has no bookmark behind it to edit.
    if (placeIndex.isValid()) {
        const bool device = m_places->isDevice(placeIndex);

        menu.addAction(tr("Rename..."), this, [this, placeIndex] {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Rename"), tr("Name:"), QLineEdit::Normal,
                m_places->text(placeIndex), &ok);
            if (ok && !name.isEmpty()) {
                m_places->editPlace(placeIndex, name, m_places->url(placeIndex),
                                    m_places->icon(placeIndex).name());
            }
        });

        if (!device) {
            // Devices come from Solid rather than a bookmark, so removing one
            // could only mean hiding it, which is the entry below.
            menu.addAction(tr("Remove"), this,
                           [this, placeIndex] { m_places->removePlace(placeIndex); });
        }
        menu.addAction(tr("Hide"), this, [this, placeIndex] {
            m_places->setPlaceHidden(placeIndex, true);
        });

        menu.addSeparator();
        QAction *up = menu.addAction(tr("Move up"));
        up->setEnabled(placeRow > 0);
        connect(up, &QAction::triggered, this,
                [this, placeRow] { m_places->movePlace(placeRow, placeRow - 1); });
        QAction *down = menu.addAction(tr("Move down"));
        down->setEnabled(placeRow < m_places->rowCount() - 1);
        connect(down, &QAction::triggered, this,
                [this, placeRow] { m_places->movePlace(placeRow, placeRow + 2); });
        menu.addSeparator();
    } else if (url.isLocalFile()) {
        menu.addAction(tr("Add to Favorites"), this, [this, url, item] {
            m_places->addPlace(item->text(0), url,
                               QStringLiteral("folder"), QString());
        });
        menu.addSeparator();
    }

    QAction *showHidden = menu.addAction(tr("Show hidden places"));
    showHidden->setCheckable(true);
    showHidden->setChecked(false);
    connect(showHidden, &QAction::triggered, this, [this] {
        // Unhides everything at once; Win7's equivalent is likewise a single
        // "Restore default favorites".
        for (int row = 0; row < m_places->rowCount(); ++row) {
            const QModelIndex index = m_places->index(row, 0);
            if (m_places->isHidden(index))
                m_places->setPlaceHidden(index, false);
        }
    });

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ---- Drops ------------------------------------------------------------------

bool NavigationPane::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_tree->viewport())
        return QWidget::eventFilter(watched, event);

    const auto targetAt = [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        const QUrl url = urlForItem(item);
        // Computer is not a directory and nothing can be copied into it.
        return Locations::isComputer(url) ? QUrl() : url;
    };

    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *e = static_cast<QDragEnterEvent *>(event);
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::DragMove: {
        auto *e = static_cast<QDragMoveEvent *>(event);
        const QUrl target = targetAt(e->position().toPoint());
        if (e->mimeData()->hasUrls() && target.isValid()) {
            e->acceptProposedAction();
            m_tree->setCurrentItem(m_tree->itemAt(e->position().toPoint()));
        } else {
            e->ignore();
        }
        return true;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        const QUrl target = targetAt(e->position().toPoint());
        if (target.isValid()) {
            Q_EMIT dropped(e, target);
            e->acceptProposedAction();
        }
        // Consumed either way: QTreeWidget's own item model would otherwise try
        // to make tree rows out of the file list.
        return true;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}
