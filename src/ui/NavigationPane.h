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

// The left hand pane, grouped as Win7 groups it into favorites, libraries,
// computer and network
//
// The entries come from the places model, whose own grouping supplies the
// headings, and favorites against libraries is the one split it cannot express,
// so the standard user directories are matched by path
class NavigationPane : public QWidget {
    Q_OBJECT

public:
    explicit NavigationPane(QWidget *parent = nullptr);

    // Shared with the address bar, which resolves its icon from these entries
    KFilePlacesModel *placesModel() const;

    // Follows navigation that started elsewhere without reporting it back, and
    // the tree is never opened up to it, the highlight landing on the deepest
    // row already on show that leads to the folder
    void setCurrentUrl(const QUrl &url);

    // Keeps expanded folders open, the model repopulating on every plug
    void refresh();

Q_SIGNALS:
    void urlActivated(const QUrl &url);

    // From the recycle bin's context menu, the window owning the confirmation
    void emptyTrashRequested();

    // From the computer heading's context menu
    void connectDrivesRequested();

    // From the network heading's context menu
    void mapDriveRequested();

    // The event is still live and must be used before returning
    void dropped(QDropEvent *event, const QUrl &destination);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    QTreeWidgetItem *addGroup(const QString &title,
                              std::initializer_list<const char *> iconNames,
                              const QUrl &url = QUrl());
    void addEntry(QTreeWidgetItem *group, const QModelIndex &placeIndex);

    // A placeholder child makes the triangle appear without reading the
    // subfolders, since scanning up front would stall on an unresponsive mount
    void addPlaceholderIfExpandable(QTreeWidgetItem *item, const QUrl &url);
    void populateChildren(QTreeWidgetItem *item);

    // Scans run off the main thread, so the item may be gone by the time one
    // lands
    void applyChildren(const QUrl &url, const QList<QUrl> &children);

    // Called again whenever what is on show can change
    void syncHighlight();

    // The folder's own row if on show, otherwise the deepest visible one
    // containing it, and nothing at all if none leads to it
    QTreeWidgetItem *highlightTarget() const;

    void showContextMenu(const QPoint &pos);
    QTreeWidgetItem *itemForUrl(const QUrl &url) const;
    QUrl urlForItem(QTreeWidgetItem *item) const;

    KFilePlacesModel *m_places = nullptr;
    QTreeWidget      *m_tree = nullptr;

    // Watches every expanded folder, so changes made elsewhere show up here
    KDirWatch *m_watch = nullptr;

    // So the highlight survives a rebuild
    QUrl m_currentUrl;

    // Reopened after a rebuild
    QSet<QUrl> m_expanded;

    // Scans in flight, so reexpanding a folder starts no duplicate
    QSet<QUrl> m_scanning;
};
