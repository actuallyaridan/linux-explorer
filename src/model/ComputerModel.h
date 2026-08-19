#pragma once

#include <QAbstractTableModel>
#include <QIcon>
#include <QList>
#include <QString>
#include <QUrl>

#include <kio/global.h>

class KFilePlacesModel;

// The drive list behind the Computer view
//
// There is no KIO worker that can list a machine's drives, so this is a table
// built from the devices the places model tracks, and free space is filled in
// afterwards by a job per row since querying can block on a dead mount
class ComputerModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Name = 0, Type, TotalSize, FreeSpace, ColumnCount };

    // The tile view needs the raw numbers rather than the formatted strings
    enum Role {
        UrlRole = Qt::UserRole + 1,
        TotalSizeRole,
        AvailableSizeRole,
        SizeKnownRole,
        RemovableRole,
    };

    // Type is here for the details header, Win7's menu offering only the rest
    enum SortKey { SortByName = 0, SortBySize, SortByFree, SortByType };

    explicit ComputerModel(KFilePlacesModel *places, QObject *parent = nullptr);

    // For the free space figures, which nothing watches, devices coming and
    // going being tracked by the places model itself
    void refresh();

    SortKey sortKey() const { return m_sortKey; }
    Qt::SortOrder sortOrder() const { return m_sortOrder; }
    void setSortKey(SortKey key, Qt::SortOrder order = Qt::AscendingOrder);

    // An unsortable column is ignored and leaves the order alone
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // For the mount, eject and rename operations only that model can perform
    QModelIndex placeIndexFor(const QUrl &url) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    QUrl urlForIndex(const QModelIndex &index) const;

    // Drives that exist but are not mounted, so are absent from the rows above
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
        // So eject and rename can be routed back to the owning model
        int placeRow = -1;

        // The icon is left out, QIcon having no equality and its cache key
        // differing on every fresh instance of the same themed icon
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
