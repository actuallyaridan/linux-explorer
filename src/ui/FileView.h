#pragma once

#include "Settings.h"

#include <KFileItem>

#include <QModelIndexList>
#include <QSet>
#include <QUrl>
#include <QWidget>

class DirectoryModel;
class GroupingProxy;
class KFilePreviewGenerator;
class QAbstractItemView;
class QDropEvent;
class QItemSelectionModel;
class QLabel;
class QListView;
class QModelIndex;
class QStackedWidget;
class QTreeView;

// The file list, in all eight of Windows 7's view modes.
//
// Two views rather than one: details is a QTreeView for its sortable,
// resizable, reorderable header, and the other seven are a QListView, the only
// Qt view that lays items out in a grid. They share one model and one
// QItemSelectionModel, so switching modes keeps the selection.
//
// Everything that must behave the same in every mode lives here rather than in
// the window: drag and drop, the rubber band, the rename editor, previews and
// the empty-folder message.
class FileView : public QWidget {
    Q_OBJECT

public:
    explicit FileView(DirectoryModel *model, QWidget *parent = nullptr);

    void setViewMode(Settings::ViewMode mode);
    Settings::ViewMode viewMode() const { return m_mode; }

    // The view currently on show, for callers that need to scroll or measure.
    QAbstractItemView *currentView() const;

    // Puts the keyboard on whichever view is up. Focusing this container
    // directly would land on a widget with no focus policy and do nothing.
    void focusView();

    // Shared by both views, so a caller can connect to it once.
    QItemSelectionModel *selectionModel() const;
    QModelIndexList selectedIndexes() const;

    // Where a paste or a drop onto empty space lands. Set on every navigation.
    void setDestination(const QUrl &url) { m_destination = url; }

    // Starts the inline rename editor on `url`, selecting the base name only,
    // as Windows does.
    void renameItem(const QUrl &url);

    // Selects `url`, scrolls it into view and focuses it. Does nothing if it is
    // not listed.
    bool selectUrl(const QUrl &url, bool startRename = false);

    // The same for several items at once. Returns the URLs that were not
    // listed, so a caller waiting on a running listing knows what is left.
    QList<QUrl> selectUrls(const QList<QUrl> &urls);

    void selectAll();
    void invertSelection();

    // Driven through the header rather than the model: the tree has sorting
    // enabled, so moving its indicator is what re-sorts, and going behind it
    // would leave the two disagreeing about where the arrow belongs.
    void sortBy(int sourceColumn, Qt::SortOrder order);

    // Groups the listing under headings, by a DirectoryModel source column, or
    // -1 for none. Only the details view can show groups, so turning grouping
    // on switches to it and picking an icon mode turns it back off.
    void setGroupColumn(int sourceColumn);
    int groupColumn() const;

    // The details header's section order, widths and sort indicator, for
    // saving between runs.
    QByteArray headerState() const;
    void setHeaderState(const QByteArray &state);

    // The message drawn over an empty listing. Empty string hides it.
    void setStatusMessage(const QString &message);

    // Win7's "Use check boxes to select items": a tick box on each row, so a
    // multiple selection can be built without holding Ctrl.
    void setCheckBoxesVisible(bool visible);

    // Whether one click opens an item or only selects it. Driven off
    // clicked/doubleClicked rather than the style's activate-on-single-click
    // hint, which is the desktop's global setting rather than this dialog's.
    void setSingleClickToOpen(bool single);

Q_SIGNALS:
    // An item was opened. The index is into the flat listing, never the
    // grouping proxy, so it can go straight to DirectoryModel.
    void activated(const QModelIndex &index);
    // A context menu was asked for. `onItem` distinguishes a click on a row
    // from one on the empty space, which get different menus.
    void contextMenuRequested(const QPoint &globalPos, bool onItem);
    void selectionChanged();

    // The inline editor committed a new name. Not applied here: it has to go
    // through FileOps so KIO records it with the undo manager, which
    // KDirModel::setData would not.
    void renameRequested(const QUrl &url, const QString &newName);

    // A drop landed. `destination` is the folder under the cursor, or the
    // current folder for a drop on empty space. The event is still live and
    // must be used before returning.
    void dropped(QDropEvent *event, const QUrl &destination);

    // A drag rested on a folder long enough for it to open. The drag is still
    // in progress; the point is to carry on dropping in the new folder.
    void springLoaded(const QUrl &folder);

    // The user picked a different mode from the header or a shortcut.
    void viewModeChanged(Settings::ViewMode mode);

    // Grouping was turned on, changed or dropped, so the menus can follow.
    void groupColumnChanged(int sourceColumn);

protected:
    void resizeEvent(QResizeEvent *event) override;

    // Watches both viewports for clicks on a selection tick box. The box is
    // drawn by the delegate rather than backed by a model role, so there is no
    // Qt::ItemIsUserCheckable and the click must be caught before it becomes
    // an ordinary selection.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildDetailsView();
    void buildIconView();

    // Wires a view's clicks and Enter key to activated(), honouring the
    // single-click setting. Shared, so the two cannot answer different
    // gestures.
    void bindActivation(QAbstractItemView *view);
    void applyMode();
    void configureColumns();
    void showHeaderMenu(const QPoint &pos);
    void repositionMessage();

    // True while the details view is showing the grouping proxy, which decides
    // whether indices coming out of it need mapping.
    bool grouped() const;

    // The index to hand the current view for `url`, mapped through the
    // grouping proxy when one is in the way.
    QModelIndex viewIndexFor(const QUrl &url) const;

    // The file behind an index that came out of a view: while "Group by" is on
    // those are the proxy's, and DirectoryModel only reads the flat listing's.
    KFileItem itemAtViewIndex(const QModelIndex &index) const;

    // Reconnects the selection plumbing after a model swap, which replaces the
    // details view's selection model with a fresh one.
    void rebindSelection();

    DirectoryModel *m_model = nullptr;
    GroupingProxy  *m_grouping = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTreeView *m_details = nullptr;
    QListView *m_icons = nullptr;
    QLabel *m_message = nullptr;

    // One per view: a generator attaches to a single view.
    KFilePreviewGenerator *m_detailsPreviews = nullptr;
    KFilePreviewGenerator *m_iconPreviews = nullptr;

    Settings::ViewMode m_mode = Settings::ViewMode::Details;
    bool m_checkBoxes = false;
    bool m_singleClick = false;
    QUrl m_destination;

    // Columns that have already been given a width, so switching one on later
    // does not reset every other column back to its default.
    QSet<int> m_sizedColumns;
};
