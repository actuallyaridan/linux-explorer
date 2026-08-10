#pragma once

#include <QList>
#include <QModelIndexList>
#include <QObject>
#include <QUrl>

#include <KFileItem>

class KDirModel;
class Win7ColumnProxy;
class QAbstractItemModel;

// The directory listing is KIO's throughout: KDirModel keeps the item list in
// sync with the filesystem, and KDirSortFilterProxyModel supplies the ordering
// Explorer wants (folders first, then a natural sort).
//
// This class bolts the two together and translates between the proxy indices
// the views speak and the KFileItems the rest of the app speaks, so no call
// site has to do its own mapToSource().
class DirectoryModel : public QObject {
    Q_OBJECT

public:
    // KDirModel's column numbering, re-exported so callers need not include it.
    // These are SOURCE columns: the proxy drops the ones the user has not
    // switched on and re-indexes the rest, so cross the boundary with
    // viewColumnFor() rather than assuming the two line up.
    enum Column {
        Name = 0,
        Size,
        ModifiedTime,
        Permissions,
        Owner,
        Group,
        Type,
        ColumnCount,
    };

    explicit DirectoryModel(QObject *parent = nullptr);

    // The model to hand to a view. This is the proxy, so every index that
    // crosses this boundary in either direction is a proxy index.
    QAbstractItemModel *model() const;

    void setUrl(const QUrl &url);
    QUrl url() const;

    // Re-reads the current directory. KDirLister's watch keeps itself current,
    // so this is for what the watch cannot see: a network mount that changed
    // underneath us, or a user who wants to be sure (F5).
    void refresh();

    void setShowHiddenFiles(bool show);
    bool showHiddenFiles() const;

    // Which source columns the details view offers, in source order. Dropped by
    // the proxy rather than hidden on the view: a hidden section still
    // contributes its width to QHeaderView::length(), leaving the tree
    // convinced its contents are wider than they are.
    QList<int> visibleColumns() const;
    void setVisibleColumns(const QList<int> &sourceColumns);
    void setColumnVisible(int sourceColumn, bool visible);
    bool isColumnVisible(int sourceColumn) const;

    // -1 when the column is not currently on show.
    int viewColumnFor(int sourceColumn) const;
    int sourceColumnFor(int viewColumn) const;

    // The column the listing is ordered by, in source numbering.
    int sortColumn() const;
    Qt::SortOrder sortOrder() const;
    void sort(int sourceColumn, Qt::SortOrder order);

    // Repaints every name after the Windows-friendly naming or the hidden-
    // extension setting changes. Nothing is cached and the listing is
    // untouched; the rows are only asked for their text again.
    void refreshDisplayNames();

    // Narrows the listing to names containing `pattern`, case-insensitively.
    // Filters what is already listed; searching subfolders is a different
    // operation, through Locations::search().
    void setNameFilter(const QString &pattern);

    // An invalid index yields a null KFileItem, so a view's currentIndex() can
    // go straight through.
    KFileItem itemForIndex(const QModelIndex &proxyIndex) const;
    QList<KFileItem> itemsForIndexes(const QModelIndexList &proxyIndexes) const;

    // The proxy index showing `url`, or an invalid index if it is not listed.
    QModelIndex indexForUrl(const QUrl &url) const;

    // Top level only; Explorer's details view is flat.
    int rowCount() const;

Q_SIGNALS:
    void loadingStarted();
    void loadingFinished();
    // A listing failed. The window needs both the KIO error code and the
    // location that refused: Windows words a refusal differently, and the
    // offer to retry as administrator has to name the right folder.
    void errorOccurred(int error, const QString &message, const QUrl &url);

    // Straight from the lister's own job. Not emitted at all for a directory
    // that lists in one go, so its absence does not mean no progress.
    void loadingProgress(int percent);

    // One per batch of newly listed items. The window selects a folder it just
    // created from here: mkdir returns before the lister has seen it.
    void itemsAdded(const QList<QUrl> &urls);

private:
    KDirModel *m_dirModel = nullptr;
    Win7ColumnProxy *m_proxy = nullptr;
};
