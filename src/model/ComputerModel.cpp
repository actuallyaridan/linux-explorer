#include "ComputerModel.h"
#include "DriveLabel.h"

#include <KFilePlacesModel>
#include <KIO/FileSystemFreeSpaceJob>

#include <algorithm>

ComputerModel::ComputerModel(KFilePlacesModel *places, QObject *parent)
    : QAbstractTableModel(parent)
    , m_places(places)
{
    // The places model populates asynchronously and on every plug or unplug,
    // so the drive list is rebuilt rather than patched
    connect(m_places, &QAbstractItemModel::modelReset, this, &ComputerModel::rebuild);
    connect(m_places, &QAbstractItemModel::rowsInserted, this, &ComputerModel::rebuild);
    connect(m_places, &QAbstractItemModel::rowsRemoved, this, &ComputerModel::rebuild);
    // Mounting inserts no row, connecting only filling in the location, which
    // arrives as a data change on the row the drive already had
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

        // An unmounted drive has nothing to open, so it is counted instead and
        // the view offers to connect it
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
        if (device.removable)
            device.type = tr("Removable Disk");
        else
            device.type = tr("Local Disk");
        device.fsType = DriveLabel::fileSystemType(m_places, index);

        // Any data change rebuilds the whole list, so starting each size from
        // scratch would drop the page back to an unknown figure every time
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

    // Nothing the page draws has moved, which is the common case, and
    // resetting anyway would lose the selection
    if (devices == m_devices && unmounted == m_unmountedCount)
        return;

    beginResetModel();
    m_devices = devices;
    m_unmountedCount = unmounted;
    endResetModel();

    // Only the drives with no figure yet, the rest being carried over
    for (const Device &device : std::as_const(m_devices)) {
        if (!device.sizeKnown)
            queryFreeSpace(device.url);
    }
}

void ComputerModel::refresh()
{
    rebuild();

    // A rebuild keeps the figures it has, and a drive filling up changes no row
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
    case Name:
        setSortKey(SortByName, order);
        break;
    case Type:
        setSortKey(SortByType, order);
        break;
    case TotalSize:
        setSortKey(SortBySize, order);
        break;
    case FreeSpace:
        setSortKey(SortByFree, order);
        break;
    default:
        break;
    }
}

void ComputerModel::applySort(QList<Device> &devices) const
{
    const bool descending = (m_sortOrder == Qt::DescendingOrder);

    std::stable_sort(devices.begin(), devices.end(),
                     [this, descending](const Device &left, const Device &right) {
        // Drives with no figure sink to the bottom, before the order is
        // applied so reversing does not float them to the top
        if (m_sortKey == SortBySize || m_sortKey == SortByFree) {
            if (left.sizeKnown != right.sizeKnown)
                return left.sizeKnown;
        }

        // Negative when left comes first, positive when right does
        int comparison = 0;
        switch (m_sortKey) {
        case SortBySize:
            if (left.sizeKnown && left.total != right.total)
                comparison = left.total < right.total ? -1 : 1;
            break;
        case SortByFree:
            if (left.sizeKnown && left.available != right.available)
                comparison = left.available < right.available ? -1 : 1;
            break;
        case SortByType:
            comparison = QString::compare(left.type, right.type, Qt::CaseInsensitive);
            break;
        case SortByName:
        default:
            break;
        }

        // Name breaks every tie, rather than discovery order
        if (comparison == 0)
            comparison = QString::compare(left.name, right.name, Qt::CaseInsensitive);

        if (descending)
            return comparison > 0;
        return comparison < 0;
    });
}

QModelIndex ComputerModel::placeIndexFor(const QUrl &url) const
{
    for (const Device &device : m_devices) {
        if (device.url == url && device.placeRow >= 0)
            return m_places->index(device.placeRow, 0);
    }

    // Normal for an unmounted drive, those being counted rather than listed
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

        // By location rather than a captured row, since a plug or unplug in
        // flight would leave the index on another drive
        for (int row = 0; row < m_devices.size(); ++row) {
            if (m_devices.at(row).url != url)
                continue;

            m_devices[row].total = job->size();
            m_devices[row].available = job->availableSize();
            m_devices[row].sizeKnown = true;

            // A figure arriving can reorder the list, which is structural, and
            // a data change would leave the old order with the new numbers
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
    // A flat table, so nothing has children
    if (parent.isValid())
        return 0;
    return m_devices.size();
}

int ComputerModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant ComputerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return {};

    const Device &device = m_devices.at(index.row());

    switch (role) {
    case Qt::DecorationRole:
        if (index.column() != Name)
            return {};
        return device.icon;
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
    case FileSystemTypeRole:
        return device.fsType;
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
        if (!device.sizeKnown)
            return QString();
        return KIO::convertSize(device.total);
    case FreeSpace:
        if (!device.sizeKnown)
            return QString();
        return KIO::convertSize(device.available);
    default:
        return {};
    }
}

QVariant ComputerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case Name:
        return tr("Name");
    case Type:
        return tr("Type");
    case TotalSize:
        return tr("Total Size");
    case FreeSpace:
        return tr("Free Space");
    default:
        return {};
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
