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

// Windows 7's Computer page: drives grouped under "Hard Disk Drives" and
// "Devices with Removable Storage", in any of Win7's eight view modes.
//
// Real item views on ComputerModel rather than composed widgets, which is what
// lets the View menu apply here as it does to a folder: selection, the rubber
// band, arrow keys, activation and the context menu all come from
// QAbstractItemView. Only the tile and content layouts are drawn by hand, the
// capacity bar being beyond QStyledItemDelegate.
//
// The grouping is structural rather than a proxy: each heading owns a QListView
// filtered to its own devices, stacked down a scrolling page. QListView shows
// one level and cannot draw headings, so a grouping proxy would hide every
// drive inside its heading. Details is the exception, a single flat table,
// since a header split across two trees would be two headers to keep in step.
class ComputerView : public QWidget {
    Q_OBJECT

public:
    explicit ComputerView(ComputerModel *model, QWidget *parent = nullptr);

    // Win7's Computer page opens in Tiles and switches like any other folder.
    void setViewMode(Settings::ViewMode mode);
    Settings::ViewMode viewMode() const { return m_mode; }

    // Number of drives shown, for the details pane's summary.
    int deviceCount() const;

    // The highlighted drive, or an invalid index. Always into ComputerModel
    // itself, never a section filter, so callers can read the drive's figures
    // off it without knowing how the page is put together.
    QModelIndex selectedIndex() const;

    // The places-model row behind a drive, so the window can offer eject and
    // rename without reaching past the view.
    QModelIndex placeIndexFor(const QUrl &url) const;

    // Puts the keyboard on whichever view is on show. The page is a stack of
    // containers, none of which can take the keyboard itself.
    void focusView();

Q_SIGNALS:
    void urlActivated(const QUrl &url);
    void contextMenuRequested(const QUrl &url, const QPoint &globalPos);
    void selectionChanged();
    void viewModeChanged(Settings::ViewMode mode);

private:
    // One heading and the view of the drives under it, hidden wholesale when
    // the filter matches nothing.
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

    // One per section, plus the details tree.
    QList<QAbstractItemView *> allViews() const;

    // Records `url` as the page's selection and restates it everywhere, giving
    // the several views one shared highlight.
    void setSelectedUrl(const QUrl &url);

    // Puts the highlight back on every view, after a mode switch or a model
    // reset leaves them holding nothing.
    void restoreSelection();

    // Moves the selection out of `from` and into the next section along, for
    // an arrow key that ran off the end of one group.
    bool stepToNeighbour(QListView *from, int direction);

    QUrl urlAt(const QModelIndex &index) const;

    ComputerModel *m_model = nullptr;
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_scroll = nullptr;
    QTreeView *m_details = nullptr;
    QList<Section> m_sections;

    Settings::ViewMode m_mode = Settings::ViewMode::Tiles;

    // By URL rather than by index: the model resets on every sort and device
    // change, which an index would not survive.
    QUrl m_selectedUrl;

    // Guards the cross-view selection sync, which clears other views and so
    // re-enters through their own selection signals.
    bool m_syncing = false;
};
