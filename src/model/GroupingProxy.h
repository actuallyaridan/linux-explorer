#pragma once

#include <QAbstractProxyModel>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

class DirectoryModel;
class QTimer;

// Win7's group by, turning the flat listing into headings with the files
// underneath
//
// A tree is the only shape Qt's views have for this, which is why grouping
// applies to the details view alone, and the groups are rebuilt wholesale on
// any source change since a listing is small enough that it costs less than
// patching, which is where a proxy like this usually goes wrong
class GroupingProxy : public QAbstractProxyModel {
    Q_OBJECT

public:
    explicit GroupingProxy(DirectoryModel *model, QObject *parent = nullptr);

    // A source column, or negative for no grouping, and only name, size, date
    // and type are meaningful, anything else grouping by name
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

    // A heading row has no file behind it and must be skipped by callers
    // turning a selection into files
    bool isGroup(const QModelIndex &proxyIndex) const;

private:
    struct Group {
        QString title;
        // In source order, so the sort holds within a heading
        QList<int> rows;

        bool operator==(const Group &other) const
        {
            return title == other.title && rows == other.rows;
        }
    };

    // Resets only if the groups came out different, since most source churn
    // changes no grouping and a reset would throw away the selection
    void rebuild();

    // Coalesces bursts of source signals, previews changing a row per file
    void scheduleRebuild();

    void computeGroups(QList<Group> *groups,
                       QHash<int, QPair<int, int>> *lookup) const;
    QString groupTitleFor(int sourceRow, int *rank) const;

    DirectoryModel *m_model = nullptr;
    QTimer *m_rebuildTimer = nullptr;
    QList<Group> m_groups;

    // Source row to its group and its position within that group
    QHash<int, QPair<int, int>> m_rowLookup;

    int m_groupColumn = -1;
};
