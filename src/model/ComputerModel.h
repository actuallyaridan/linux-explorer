#pragma once

#include <QAbstractTableModel>
#include <QIcon>
#include <QList>
#include <QString>
#include <QUrl>

#include <kio/global.h>

class KFilePlacesModel;

// The drive list behind Explorer's "Computer" view.
//
// There is no computer:/ KIO worker in KF6 (see Locations.h), so this is not a
// directory listing and does not pretend to be one: it is a table built from
// the devices KFilePlacesModel already tracks through Solid.
//
// Free space is filled in after the fact, with an asynchronous KIO job per row:
// querying a filesystem can block on a spun-down disk or a dead network mount.
class ComputerModel : public QAbstractTableModel {
    Q_OBJECT

public:
    // Windows 7's Computer view shows exactly these columns, in this order.
    enum Column { Name = 0, Type, TotalSize, FreeSpace, ColumnCount };

    // The tile view needs the raw numbers rather than the formatted strings.
    enum Role {
        UrlRole = Qt::UserRole + 1,
        TotalSizeRole,
        AvailableSizeRole,
        SizeKnownRole,
        RemovableRole,
    };

    // Win7's Computer page offers the first three on its context menu and
    // defaults to name; Type is here for the details header's fourth column.
    enum SortKey { SortByName = 0, SortBySize, SortByFree, SortByType };

    explicit ComputerModel(KFilePlacesModel *places, QObject *parent = nullptr);

    // Re-reads the drive list and re-queries every free-space figure. The
    // places model tracks devices coming and going on its own, so this is for
    // the numbers, which nothing watches.
    void refresh();

    SortKey sortKey() const { return m_sortKey; }
    Qt::SortOrder sortOrder() const { return m_sortOrder; }
    void setSortKey(SortKey key, Qt::SortOrder order = Qt::AscendingOrder);

    // Behind the details header's sort clicks. Columns map onto the keys above;
    // anything else is ignored, so an unsortable column leaves the order alone.
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // The places-model row a drive came from, for the mount, eject and rename
    // operations only that model can perform.
    QModelIndex placeIndexFor(const QUrl &url) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // The location a row navigates to, or an invalid URL for an invalid index.
    QUrl urlForIndex(const QModelIndex &index) const;

    // Drives that exist but are not mounted, and so are absent from the rows
    // above. The view offers to connect them.
    int unmountedCount() const;

private:
    struct Device {
        QString name;
        QString type;
        QUrl url;
        QIcon icon;
        KIO::filesize_t total = 0;
        KIO::filesize_t available = 0;
        bool sizeKnown = false;
        bool removable = false;
        // So eject and rename can be routed back to the owning model.
        int placeRow = -1;

        // Whether a rebuild would draw the same page. The icon is left out:
        // QIcon has no equality, and cache keys differ every time the places
        // model hands back a fresh instance of the same themed icon, which is
        // the churn this is here to absorb.
        bool operator==(const Device &other) const
        {
            return name == other.name && type == other.type && url == other.url
                && total == other.total && available == other.available
                && sizeKnown == other.sizeKnown && removable == other.removable
                && placeRow == other.placeRow;
        }
    };

    void rebuild();
    void queryFreeSpace(const QUrl &url);
    void applySort(QList<Device> &devices) const;

    KFilePlacesModel *m_places = nullptr;
    QList<Device> m_devices;
    int m_unmountedCount = 0;
    SortKey m_sortKey = SortByName;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
