#pragma once

#include <QAbstractProxyModel>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

class DirectoryModel;
class QTimer;

// Windows 7's "Group by", as a proxy turning the flat listing into one level of
// headings with the files underneath.
//
// A tree is the only shape Qt's item views have for this: QSortFilterProxyModel
// cannot invent rows, so the headings are real rows with the files as children,
// drawn by the details view's expanders. That is also why grouping applies to
// the details view alone; QListView shows a single level and would hide every
// file inside its heading.
//
// The groups are rebuilt wholesale on any source change rather than patched.
// Working out which group an inserted row joins, and whether that group is new,
// is where a proxy like this usually goes wrong, and a directory listing is
// small enough that rebuilding costs less than the bookkeeping.
class GroupingProxy : public QAbstractProxyModel {
    Q_OBJECT

public:
    explicit GroupingProxy(DirectoryModel *model, QObject *parent = nullptr);

    // A DirectoryModel source column, or -1 for no grouping. Only Name, Size,
    // ModifiedTime and Type are meaningful; anything else groups by name.
    void setGroupColumn(int sourceColumn);
    int groupColumn() const { return m_groupColumn; }

    void setSourceModel(QAbstractItemModel *sourceModel) override;

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // True for a heading row, which has no file behind it and must be skipped
    // when turning a selection into files.
    bool isGroup(const QModelIndex &proxyIndex) const;

private:
    struct Group {
        QString title;
        // In the order the source listed them, so the sort holds within a
        // heading.
        QList<int> rows;

        bool operator==(const Group &other) const
        {
            return title == other.title && rows == other.rows;
        }
    };

    // Recomputes the groups, resetting only if they came out different. Most
    // source churn changes no grouping — a thumbnail landing re-sorts nothing —
    // and a reset for it would throw away the user's selection.
    void rebuild();

    // Coalesces bursts of source signals into one rebuild: rendering previews
    // produces a change per file, so a few hundred files meant a few hundred
    // rebuilds.
    void scheduleRebuild();

    void computeGroups(QList<Group> *groups,
                       QHash<int, QPair<int, int>> *lookup) const;
    QString groupTitleFor(int sourceRow, int *rank) const;

    DirectoryModel *m_model = nullptr;
    QTimer *m_rebuildTimer = nullptr;
    QList<Group> m_groups;

    // Source row -> (group, position within it), so mapFromSource need not
    // search every group.
    QHash<int, QPair<int, int>> m_rowLookup;

    int m_groupColumn = -1;
};
