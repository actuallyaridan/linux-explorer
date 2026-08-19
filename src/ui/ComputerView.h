#pragma once

#include "Settings.h"

#include <QList>
#include <QModelIndex>
#include <QUrl>
#include <QWidget>

class ComputerModel;
class QAbstractItemView;
class QLabel;
class QListView;
class QScrollArea;
class QSortFilterProxyModel;
class QStackedWidget;
class QTreeView;
class QVBoxLayout;

// Win7's Computer page, drives grouped under headings for the fixed and the
// removable ones, in any of the eight view modes
//
// These are real item views on the drive model, so selection, the rubber band,
// the arrow keys and the context menu all come from Qt, and the grouping is
// structural rather than a proxy, each heading owning its own list view, since
// a list view shows one level and would hide every drive inside its heading
//
// Details is the exception, being a single flat table
class ComputerView : public QWidget {
    Q_OBJECT

public:
    explicit ComputerView(ComputerModel *model, QWidget *parent = nullptr);

    void setViewMode(Settings::ViewMode mode);
    Settings::ViewMode viewMode() const { return m_mode; }

    int deviceCount() const;

    // Always into the drive model itself, never a section filter
    QModelIndex selectedIndex() const;

    // So the window can offer eject and rename without reaching past the view
    QModelIndex placeIndexFor(const QUrl &url) const;

    // The page is a stack of containers, none of which take the keyboard
    void focusView();

Q_SIGNALS:
    void urlActivated(const QUrl &url);
    void contextMenuRequested(const QUrl &url, const QPoint &globalPos);
    void selectionChanged();
    void viewModeChanged(Settings::ViewMode mode);

private:
    // Hidden wholesale when its filter matches nothing
    struct Section {
        QWidget *container = nullptr;
        QLabel *heading = nullptr;
        QListView *view = nullptr;
        QSortFilterProxyModel *filter = nullptr;
        QString title;
    };

    void buildSections();
    void addSection(QVBoxLayout *layout, const QString &title, bool removable);
    void buildDetailsView();
    void applyMode();
    void updateSections();

    QList<QAbstractItemView *> allViews() const;

    // Restates the selection everywhere, giving the views one shared highlight
    void setSelectedUrl(const QUrl &url);

    // After a mode switch or a model reset leaves the views holding nothing
    void restoreSelection();

    // For an arrow key that ran off the end of one group
    bool stepToNeighbour(QListView *from, int direction);

    QUrl urlAt(const QModelIndex &index) const;

    ComputerModel *m_model = nullptr;
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_scroll = nullptr;
    QTreeView *m_details = nullptr;
    QList<Section> m_sections;

    Settings::ViewMode m_mode = Settings::ViewMode::Tiles;

    // By location, the model resetting on every sort and device change
    QUrl m_selectedUrl;

    // The sync clears the other views and so reenters through their own
    // selection signals
    bool m_syncing = false;
};
