#include "GroupingProxy.h"
#include "DirectoryModel.h"

#include <KFileItem>
#include <KIO/Global>

#include <QDateTime>
#include <QFont>
#include <QTimer>

namespace {

// Windows 7's size buckets, in the order it lists them.
struct SizeBucket {
    KIO::filesize_t limit;
    const char *title;
};

const SizeBucket kSizeBuckets[] = {
    {0,                    QT_TRANSLATE_NOOP("GroupingProxy", "Empty")},
    {10ull * 1024,         QT_TRANSLATE_NOOP("GroupingProxy", "Tiny (0 - 10 KB)")},
    {100ull * 1024,        QT_TRANSLATE_NOOP("GroupingProxy", "Small (10 - 100 KB)")},
    {1024ull * 1024,       QT_TRANSLATE_NOOP("GroupingProxy", "Medium (100 KB - 1 MB)")},
    {16ull * 1024 * 1024,  QT_TRANSLATE_NOOP("GroupingProxy", "Large (1 - 16 MB)")},
    {128ull * 1024 * 1024, QT_TRANSLATE_NOOP("GroupingProxy", "Huge (16 - 128 MB)")},
};

// Internal ids distinguish the two kinds of index. A heading carries 0; a file
// carries its heading's position plus one, which is what parent() reads back.
constexpr quintptr kGroupId = 0;

} // namespace

GroupingProxy::GroupingProxy(DirectoryModel *model, QObject *parent)
    : QAbstractProxyModel(parent)
    , m_model(model)
    , m_rebuildTimer(new QTimer(this))
{
    // Long enough to swallow a burst of per-file source changes, short enough
    // that a real one still shows up immediately.
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(30);
    connect(m_rebuildTimer, &QTimer::timeout, this, &GroupingProxy::rebuild);
}

void GroupingProxy::scheduleRebuild()
{
    if (m_groupColumn >= 0)
        m_rebuildTimer->start();
}

void GroupingProxy::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (QAbstractItemModel *old = QAbstractProxyModel::sourceModel())
        old->disconnect(this);

    QAbstractProxyModel::setSourceModel(sourceModel);

    if (sourceModel) {
        // See the header on why this is not incremental, and scheduleRebuild on
        // why it is debounced.
        connect(sourceModel, &QAbstractItemModel::modelReset,
                this, &GroupingProxy::scheduleRebuild);
        connect(sourceModel, &QAbstractItemModel::rowsInserted,
                this, &GroupingProxy::scheduleRebuild);
        connect(sourceModel, &QAbstractItemModel::rowsRemoved,
                this, &GroupingProxy::scheduleRebuild);
        connect(sourceModel, &QAbstractItemModel::layoutChanged,
                this, &GroupingProxy::scheduleRebuild);

        // A data change is not structural and must not reset: the preview
        // generator replaces an icon per thumbnail, which wiped the selection
        // several times a second while a folder rendered. Forwarded row by row,
        // since a source range can straddle several groups.
        connect(sourceModel, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                       const QList<int> &roles) {
            QAbstractItemModel *source = this->sourceModel();
            if (!source)
                return;
            for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
                const QModelIndex from =
                    mapFromSource(source->index(row, topLeft.column()));
                const QModelIndex to =
                    mapFromSource(source->index(row, bottomRight.column()));
                if (from.isValid() && to.isValid())
                    Q_EMIT dataChanged(from, to, roles);
            }
        });
    }

    rebuild();
}

void GroupingProxy::setGroupColumn(int sourceColumn)
{
    if (m_groupColumn == sourceColumn)
        return;
    m_groupColumn = sourceColumn;
    m_rebuildTimer->stop();
    rebuild();
}

QString GroupingProxy::groupTitleFor(int sourceRow, int *rank) const
{
    *rank = 0;

    QAbstractItemModel *source = sourceModel();
    if (!source)
        return {};

    const QModelIndex sourceIndex = source->index(sourceRow, 0);
    const KFileItem item = m_model->itemForIndex(sourceIndex);
    if (item.isNull())
        return tr("Unspecified");

    switch (m_groupColumn) {
    case DirectoryModel::Type:
        // Folders group together above the file types, as they sort.
        if (item.isDir()) {
            *rank = -1;
            return tr("File folder");
        }
        return item.mimeComment().isEmpty() ? tr("Unspecified") : item.mimeComment();

    case DirectoryModel::Size: {
        if (item.isDir()) {
            *rank = -1;
            return tr("File folder");
        }
        const KIO::filesize_t size = item.size();
        for (int i = 0; i < int(std::size(kSizeBuckets)); ++i) {
            if (size <= kSizeBuckets[i].limit) {
                *rank = i;
                return tr(kSizeBuckets[i].title);
            }
        }
        *rank = int(std::size(kSizeBuckets));
        return tr("Gigantic (> 128 MB)");
    }

    case DirectoryModel::ModifiedTime: {
        const QDateTime when = item.time(KFileItem::ModificationTime);
        if (!when.isValid()) {
            *rank = 100;
            return tr("Unspecified");
        }
        const QDate today = QDate::currentDate();
        const QDate date = when.date();
        const qint64 days = date.daysTo(today);

        if (days <= 0)                        { *rank = 0; return tr("Today"); }
        if (days == 1)                        { *rank = 1; return tr("Yesterday"); }
        if (days < 7)                         { *rank = 2; return tr("Earlier this week"); }
        if (days < 14)                        { *rank = 3; return tr("Last week"); }
        if (date.year() == today.year()
            && date.month() == today.month()) { *rank = 4; return tr("Earlier this month"); }
        if (date.year() == today.year())      { *rank = 5; return tr("Earlier this year"); }
        *rank = 6;
        return tr("A long time ago");
    }

    case DirectoryModel::Name:
    default: {
        // Win7 groups names by their first character, folders included.
        const QString name = sourceIndex.data(Qt::DisplayRole).toString();
        if (name.isEmpty())
            return tr("Unspecified");
        const QChar first = name.at(0).toUpper();
        if (first.isDigit()) {
            *rank = -1;
            return tr("0 - 9");
        }
        if (!first.isLetter()) {
            *rank = -2;
            return tr("Other");
        }
        return QString(first);
    }
    }
}

void GroupingProxy::rebuild()
{
    QList<Group> groups;
    QHash<int, QPair<int, int>> lookup;
    computeGroups(&groups, &lookup);

    // Nothing moved, so no reset. The common case while a folder renders its
    // thumbnails, and what keeps a selection alive through it.
    if (groups == m_groups)
        return;

    beginResetModel();
    m_groups = groups;
    m_rowLookup = lookup;
    endResetModel();
}

void GroupingProxy::computeGroups(QList<Group> *outGroups,
                                  QHash<int, QPair<int, int>> *outLookup) const
{
    outGroups->clear();
    outLookup->clear();

    QAbstractItemModel *source = sourceModel();
    if (!source || m_groupColumn < 0)
        return;

    // Built in source order, so the groups follow their first member and the
    // files keep the listing's sort within each. The ranks then reorder the
    // groups themselves where the grouping has an order of its own.
    QHash<QString, int> byTitle;
    QList<int> ranks;
    QList<Group> groups;

    const int rows = source->rowCount();
    for (int row = 0; row < rows; ++row) {
        int rank = 0;
        const QString title = groupTitleFor(row, &rank);

        const auto existing = byTitle.constFind(title);
        int groupIndex;
        if (existing == byTitle.constEnd()) {
            groupIndex = groups.size();
            groups.append({title, {}});
            ranks.append(rank);
            byTitle.insert(title, groupIndex);
        } else {
            groupIndex = existing.value();
        }
        groups[groupIndex].rows.append(row);
    }

    // Stable, so groups sharing a rank keep their discovery order, which for
    // names is already alphabetical.
    QList<int> order;
    order.reserve(groups.size());
    for (int i = 0; i < groups.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        return ranks.at(left) < ranks.at(right);
    });

    outGroups->reserve(groups.size());
    for (int i : order)
        outGroups->append(groups.at(i));

    for (int g = 0; g < outGroups->size(); ++g) {
        const Group &group = outGroups->at(g);
        for (int p = 0; p < group.rows.size(); ++p)
            outLookup->insert(group.rows.at(p), {g, p});
    }
}

QModelIndex GroupingProxy::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    if (!parent.isValid())
        return createIndex(row, column, kGroupId);

    // A file, carrying its heading's position plus one so parent() can get back.
    if (parent.internalId() != kGroupId)
        return {};   // files have no children
    return createIndex(row, column, quintptr(parent.row() + 1));
}

QModelIndex GroupingProxy::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == kGroupId)
        return {};
    const int group = int(child.internalId()) - 1;
    if (group < 0 || group >= m_groups.size())
        return {};
    return createIndex(group, 0, kGroupId);
}

int GroupingProxy::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_groups.size();
    if (parent.internalId() != kGroupId)
        return 0;                      // files have no children
    if (parent.row() < 0 || parent.row() >= m_groups.size())
        return 0;
    return m_groups.at(parent.row()).rows.size();
}

int GroupingProxy::columnCount(const QModelIndex &) const
{
    return sourceModel() ? sourceModel()->columnCount() : 0;
}

bool GroupingProxy::isGroup(const QModelIndex &proxyIndex) const
{
    return proxyIndex.isValid() && proxyIndex.internalId() == kGroupId;
}

QModelIndex GroupingProxy::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || !sourceModel())
        return {};
    // A heading is invented by this proxy; there is no source row behind it.
    if (proxyIndex.internalId() == kGroupId)
        return {};

    const int group = int(proxyIndex.internalId()) - 1;
    if (group < 0 || group >= m_groups.size())
        return {};
    const QList<int> &rows = m_groups.at(group).rows;
    if (proxyIndex.row() < 0 || proxyIndex.row() >= rows.size())
        return {};

    return sourceModel()->index(rows.at(proxyIndex.row()), proxyIndex.column());
}

QModelIndex GroupingProxy::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid())
        return {};
    const auto found = m_rowLookup.constFind(sourceIndex.row());
    if (found == m_rowLookup.constEnd())
        return {};
    return createIndex(found.value().second, sourceIndex.column(),
                       quintptr(found.value().first + 1));
}

QVariant GroupingProxy::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (index.internalId() == kGroupId) {
        if (index.row() < 0 || index.row() >= m_groups.size())
            return {};
        const Group &group = m_groups.at(index.row());

        switch (role) {
        case Qt::DisplayRole:
            // Win7 puts the count beside the heading.
            return index.column() == 0
                ? QStringLiteral("%1 (%2)").arg(group.title).arg(group.rows.size())
                : QVariant();
        case Qt::FontRole: {
            QFont font;
            font.setBold(true);
            return font;
        }
        default:
            return {};
        }
    }

    return QAbstractProxyModel::data(index, role);
}

QVariant GroupingProxy::headerData(int section, Qt::Orientation orientation,
                                   int role) const
{
    return sourceModel() ? sourceModel()->headerData(section, orientation, role)
                         : QVariant();
}

Qt::ItemFlags GroupingProxy::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    // A heading collapses its group on a click but is not selectable: there is
    // nothing behind it for Delete to act on.
    if (index.internalId() == kGroupId)
        return Qt::ItemIsEnabled;

    return QAbstractProxyModel::flags(index);
}
