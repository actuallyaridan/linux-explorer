#pragma once

#include <QHash>
#include <QSet>
#include <QUrl>
#include <QWidget>

class KDirWatch;
class KFilePlacesModel;
class QDropEvent;
class QModelIndex;
class QTreeWidget;
class QTreeWidgetItem;

// The left-hand navigation pane, grouped as Windows 7 groups it: Favorites,
// Libraries, Computer and Network, each collapsible.
//
// The entries come from KFilePlacesModel, the same bookmark store the file
// dialogs and Dolphin use, and its GroupType is what the four headings are
// built from. The one split it cannot express is Favorites versus Libraries,
// KDE filing both under PlacesType, so the standard Documents/Music/Pictures/
// Videos directories are matched by path.
class NavigationPane : public QWidget {
    Q_OBJECT

public:
    explicit NavigationPane(QWidget *parent = nullptr);

    // Shared with the address bar, which resolves its leading icon from the
    // same entries.
    KFilePlacesModel *placesModel() const;

    // Marks where `url` lies without emitting urlActivated, so the pane can
    // follow navigation that started elsewhere without looping.
    //
    // The tree is never opened up to it: the highlight lands on the deepest row
    // already on show that leads to the folder, and steps down a level with
    // each branch the user opens towards it.
    void setCurrentUrl(const QUrl &url);

    // Rebuilds the tree from the places model, keeping expanded folders open:
    // the model re-emits on every device plug and unplug.
    void refresh();

Q_SIGNALS:
    void urlActivated(const QUrl &url);

    // From the Recycle Bin's context menu. The window owns FileOps and the
    // parent widget the confirmation needs.
    void emptyTrashRequested();

    // From the Computer heading's context menu, so mounting a drive does not
    // depend on the notification strip appearing.
    void connectDrivesRequested();

    // From the Network heading's context menu, where someone looking for a
    // share on another machine will go first.
    void mapDriveRequested();

    // Files were dropped onto an entry. Performed by the window, which owns the
    // KIO facade; the event is still live and must be used before returning.
    void dropped(QDropEvent *event, const QUrl &destination);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    QTreeWidgetItem *addGroup(const QString &title,
                              std::initializer_list<const char *> iconNames,
                              const QUrl &url = QUrl());
    void addEntry(QTreeWidgetItem *group, const QModelIndex &placeIndex);

    // Local folders get an expander without their subfolders being read: a
    // placeholder child makes the triangle appear, and the real children are
    // listed on first open. Scanning up front would stat the first level of
    // every drive at startup and stall on an unresponsive mount.
    void addPlaceholderIfExpandable(QTreeWidgetItem *item, const QUrl &url);
    void populateChildren(QTreeWidgetItem *item);

    // Applies a finished scan to the item it was started for, if that item
    // still exists: scans run off the GUI thread, and the tree may have been
    // rebuilt underneath one by the time it lands.
    void applyChildren(const QUrl &url, const QList<QUrl> &children);

    // Moves the highlight to wherever m_currentUrl now shows up. Called again
    // whenever that can change: a branch opened or closed, or a scan landing.
    void syncHighlight();

    // The current folder's own row if it is on show, otherwise the deepest
    // visible one containing it, or nullptr if nothing leads to it.
    QTreeWidgetItem *highlightTarget() const;

    void showContextMenu(const QPoint &pos);
    QTreeWidgetItem *itemForUrl(const QUrl &url) const;
    QUrl urlForItem(QTreeWidgetItem *item) const;

    KFilePlacesModel *m_places = nullptr;
    QTreeWidget      *m_tree = nullptr;

    // Watches every expanded folder, so a directory created or deleted
    // elsewhere shows up here rather than leaving the pane out of date.
    KDirWatch *m_watch = nullptr;

    // So the highlight survives a rebuild, which happens on every device
    // plug/unplug.
    QUrl m_currentUrl;

    // Which folders were open before the last rebuild, so they can be opened
    // again afterwards.
    QSet<QUrl> m_expanded;

    // Scans in flight, so re-expanding a folder starts no duplicate.
    QSet<QUrl> m_scanning;
};
