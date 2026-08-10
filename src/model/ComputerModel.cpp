#include "ComputerModel.h"
#include "DriveLabel.h"

#include <KFilePlacesModel>
#include <KIO/FileSystemFreeSpaceJob>

#include <algorithm>

ComputerModel::ComputerModel(KFilePlacesModel *places, QObject *parent)
    : QAbstractTableModel(parent)
    , m_places(places)
{
    // The model populates asynchronously and on every device plug/unplug, so
    // the drive list is rebuilt rather than patched.
    connect(m_places, &QAbstractItemModel::modelReset, this, &ComputerModel::rebuild);
    connect(m_places, &QAbstractItemModel::rowsInserted, this, &ComputerModel::rebuild);
    connect(m_places, &QAbstractItemModel::rowsRemoved, this, &ComputerModel::rebuild);
    // Mounting inserts no row: the device is already listed while unmounted, and
    // connecting it only flips setupNeeded and fills in the URL, which arrives
    // as a data change on the row it already had.
    connect(m_places, &QAbstractItemModel::dataChanged, this, &ComputerModel::rebuild);

    rebuild();
}

void ComputerModel::rebuild()
{
    QList<Device> devices;
    int unmounted = 0;

    for (int row = 0; row < m_places->rowCount(); ++row) {
        const QModelIndex index = m_places->index(row, 0);
        if (m_places->isHidden(index))
            continue;

        const KFilePlacesModel::GroupType group = m_places->groupType(index);
        if (group != KFilePlacesModel::DevicesType
            && group != KFilePlacesModel::RemovableDevicesType) {
            continue;
        }

        // An unmounted drive has no size, no free space and no URL to open, so
        // listing it would invite clicking something that cannot do anything.
        // Counted instead, so the view can offer to connect it.
        if (m_places->setupNeeded(index)) {
            ++unmounted;
            continue;
        }

        Device device;
        device.name = DriveLabel::forPlace(m_places, index);
        device.icon = m_places->icon(index);
        device.url = m_places->url(index);
        device.placeRow = row;
        device.removable = (group == KFilePlacesModel::RemovableDevicesType);
        device.type = device.removable ? tr("Removable Disk") : tr("Local Disk");

        // Any data change on any place rebuilds the whole list, so starting each
        // drive's size from scratch would drop the page back to "Size
        // unavailable" and re-run a filesystem query per drive every time.
        for (const Device &existing : std::as_const(m_devices)) {
            if (existing.sizeKnown && existing.url == device.url) {
                device.total = existing.total;
                device.available = existing.available;
                device.sizeKnown = true;
                break;
            }
        }

        devices.append(device);
    }

    applySort(devices);

    // Nothing the page draws has moved, which is the common case: the places
    // model reports a data change for everything from a device's accessibility
    // to its label, and resetting on each would lose the selection.
    if (devices == m_devices && unmounted == m_unmountedCount)
        return;

    beginResetModel();
    m_devices = devices;
    m_unmountedCount = unmounted;
    endResetModel();

    // Only the drives with no figure yet, since the rest were carried over.
    for (const Device &device : std::as_const(m_devices)) {
        if (!device.sizeKnown)
            queryFreeSpace(device.url);
    }
}

void ComputerModel::refresh()
{
    rebuild();

    // rebuild() keeps the figures it has, so asking again is the half of a
    // refresh nothing else does: a drive filling up changes no row.
    for (const Device &device : std::as_const(m_devices))
        queryFreeSpace(device.url);
}

void ComputerModel::setSortKey(SortKey key, Qt::SortOrder order)
{
    if (m_sortKey == key && m_sortOrder == order)
        return;
    m_sortKey = key;
    m_sortOrder = order;
    beginResetModel();
    applySort(m_devices);
    endResetModel();
}

void ComputerModel::sort(int column, Qt::SortOrder order)
{
    switch (column) {
    case Name:      setSortKey(SortByName, order); break;
    case Type:      setSortKey(SortByType, order); break;
    case TotalSize: setSortKey(SortBySize, order); break;
    case FreeSpace: setSortKey(SortByFree, order); break;
    default:        break;
    }
}

void ComputerModel::applySort(QList<Device> &devices) const
{
    const bool descending = (m_sortOrder == Qt::DescendingOrder);

    std::stable_sort(devices.begin(), devices.end(),
                     [this, descending](const Device &left, const Device &right) {
        // Sorting by a size nobody has reported yet would shuffle the page as
        // each query lands, so drives with no figure sink to the bottom. Before
        // the order is applied, so reversing does not float them to the top.
        if (m_sortKey == SortBySize || m_sortKey == SortByFree) {
            if (left.sizeKnown != right.sizeKnown)
                return left.sizeKnown;
        }

        int cmp = 0;
        switch (m_sortKey) {
        case SortBySize:
            if (left.sizeKnown && left.total != right.total)
                cmp = left.total < right.total ? -1 : 1;
            break;
        case SortByFree:
            if (left.sizeKnown && left.available != right.available)
                cmp = left.available < right.available ? -1 : 1;
            break;
        case SortByType:
            cmp = QString::compare(left.type, right.type, Qt::CaseInsensitive);
            break;
        case SortByName:
        default:
            break;
        }

        // Name breaks every tie, so drives of the same size or type come out in
        // a readable order rather than in discovery order.
        if (cmp == 0)
            cmp = QString::compare(left.name, right.name, Qt::CaseInsensitive);
        return descending ? cmp > 0 : cmp < 0;
    });
}

QModelIndex ComputerModel::placeIndexFor(const QUrl &url) const
{
    for (const Device &device : m_devices) {
        if (device.url == url && device.placeRow >= 0)
            return m_places->index(device.placeRow, 0);
    }

    // Not in the list, which is normal for an unmounted drive: those are counted
    // rather than listed, so the row has to be found directly.
    for (int row = 0; row < m_places->rowCount(); ++row) {
        const QModelIndex index = m_places->index(row, 0);
        if (m_places->url(index).matches(url, QUrl::StripTrailingSlash))
            return index;
    }
    return {};
}

void ComputerModel::queryFreeSpace(const QUrl &url)
{
    if (!url.isValid())
        return;

    KIO::FileSystemFreeSpaceJob *job = KIO::fileSystemFreeSpace(url);
    connect(job, &KJob::result, this, [this, job, url] {
        if (job->error())
            return;   // an unmounted or unreadable device simply shows no size

        // By URL rather than a captured row index: a device plugged or removed
        // while the query was in flight would leave the index on another drive.
        for (int row = 0; row < m_devices.size(); ++row) {
            if (m_devices.at(row).url != url)
                continue;

            m_devices[row].total = job->size();
            m_devices[row].available = job->availableSize();
            m_devices[row].sizeKnown = true;

            // A figure arriving can reorder the list, which is structural rather
            // than a data change; dataChanged would leave the view drawing the
            // old order with the new numbers in it.
            if (m_sortKey == SortBySize || m_sortKey == SortByFree) {
                beginResetModel();
                applySort(m_devices);
                endResetModel();
            } else {
                Q_EMIT dataChanged(index(row, TotalSize), index(row, FreeSpace));
            }
            return;
        }
    });
}

int ComputerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

int ComputerModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ComputerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return {};

    const Device &device = m_devices.at(index.row());

    switch (role) {
    case Qt::DecorationRole:
        return index.column() == Name ? device.icon : QVariant();
    case UrlRole:
        return device.url;
    case TotalSizeRole:
        return QVariant::fromValue(device.total);
    case AvailableSizeRole:
        return QVariant::fromValue(device.available);
    case SizeKnownRole:
        return device.sizeKnown;
    case RemovableRole:
        return device.removable;
    default:
        break;
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case Name:
        return device.name;
    case Type:
        return device.type;
    case TotalSize:
        return device.sizeKnown ? KIO::convertSize(device.total) : QString();
    case FreeSpace:
        return device.sizeKnown ? KIO::convertSize(device.available) : QString();
    default:
        return {};
    }
}

QVariant ComputerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case Name:      return tr("Name");
    case Type:      return tr("Type");
    case TotalSize: return tr("Total Size");
    case FreeSpace: return tr("Free Space");
    default:        return {};
    }
}

int ComputerModel::unmountedCount() const
{
    return m_unmountedCount;
}

QUrl ComputerModel::urlForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return {};
    return m_devices.at(index.row()).url;
}
