#pragma once

#include <QList>
#include <QModelIndexList>
#include <QObject>
#include <QUrl>

#include <KFileItem>

class KDirModel;
class Win7ColumnProxy;
class QAbstractItemModel;

// KIO's directory model and its sorting proxy, bolted together and translating
// between the proxy indices the views speak and the items the rest of the app
// speaks, so no call site has to map an index itself
class DirectoryModel : public QObject {
    Q_OBJECT

public:
    // Source columns, the proxy dropping the hidden ones and renumbering the
    // rest, so cross the boundary with viewColumnFor rather than assuming
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

    // The proxy, so every index crossing this boundary is a proxy index
    QAbstractItemModel *model() const;

    void setUrl(const QUrl &url);
    QUrl url() const;

    // For what the lister's watch cannot see, such as a network mount that
    // changed underneath us
    void refresh();

    void setShowHiddenFiles(bool show);
    bool showHiddenFiles() const;

    // Dropped by the proxy rather than hidden on the view, since a hidden
    // section still contributes its width to the header's length
    QList<int> visibleColumns() const;
    void setVisibleColumns(const QList<int> &sourceColumns);
    void setColumnVisible(int sourceColumn, bool visible);
    bool isColumnVisible(int sourceColumn) const;

    // Negative when the column is not currently on show
    int viewColumnFor(int sourceColumn) const;
    int sourceColumnFor(int viewColumn) const;

    int sortColumn() const;
    Qt::SortOrder sortOrder() const;
    void sort(int sourceColumn, Qt::SortOrder order);

    // Asks the rows for their text again after a naming setting changes
    void refreshDisplayNames();

    // Filters what is already listed, searching subfolders being a navigation
    void setNameFilter(const QString &pattern);

    // An invalid index yields a null item
    KFileItem itemForIndex(const QModelIndex &proxyIndex) const;
    QList<KFileItem> itemsForIndexes(const QModelIndexList &proxyIndexes) const;

    QModelIndex indexForUrl(const QUrl &url) const;

    // Top level only, the details view being flat
    int rowCount() const;

Q_SIGNALS:
    void loadingStarted();
    void loadingFinished();
    // The location is carried too, the offer to retry as administrator having
    // to name the right folder
    void errorOccurred(int error, const QString &message, const QUrl &url);

    // Not emitted at all for a directory that lists in one go
    void loadingProgress(int percent);

    // The window selects a newly created folder from here, since creating one
    // returns before the lister has seen it
    void itemsAdded(const QList<QUrl> &urls);

private:
    KDirModel *m_dirModel = nullptr;
    Win7ColumnProxy *m_proxy = nullptr;
};
