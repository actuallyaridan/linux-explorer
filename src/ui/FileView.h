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

// The file list, in all eight of Win7's view modes
//
// Details is a tree for the sake of its header and the other seven are a list
// view, the only Qt view that lays items out in a grid, and the two share one
// model and one selection model so switching modes keeps the selection
class FileView : public QWidget {
    Q_OBJECT

public:
    explicit FileView(DirectoryModel *model, QWidget *parent = nullptr);

    void setViewMode(Settings::ViewMode mode);
    Settings::ViewMode viewMode() const { return m_mode; }

    QAbstractItemView *currentView() const;

    // Focusing this container directly would do nothing, it having no policy
    void focusView();

    // Shared by both views, so a caller can connect to it once
    QItemSelectionModel *selectionModel() const;
    QModelIndexList selectedIndexes() const;

    // Where a paste or a drop onto empty space lands
    void setDestination(const QUrl &url) { m_destination = url; }

    // Selects the base name only, as Windows does
    void renameItem(const QUrl &url);

    bool selectUrl(const QUrl &url, bool startRename = false);

    // Returns whatever was not listed, so a caller waiting on a running
    // listing knows what is left
    QList<QUrl> selectUrls(const QList<QUrl> &urls);

    void selectAll();
    void invertSelection();

    // Driven through the header, since with sorting enabled moving the
    // indicator is what reorders, and going behind it leaves the two disagreeing
    void sortBy(int sourceColumn, Qt::SortOrder order);

    // A source column, or negative for none, and only the details view can show
    // groups so turning grouping on switches to it
    void setGroupColumn(int sourceColumn);
    int groupColumn() const;

    QByteArray headerState() const;
    void setHeaderState(const QByteArray &state);

    // Drawn over an empty listing, and an empty string hides it
    void setStatusMessage(const QString &message);

    // Win7's use check boxes to select items
    void setCheckBoxesVisible(bool visible);

    // Driven off the click signals rather than the style's own hint, which is
    // the desktop's global setting rather than this application's
    void setSingleClickToOpen(bool single);

Q_SIGNALS:
    // The index is into the flat listing, never the grouping proxy
    void activated(const QModelIndex &index);
    // The flag separates a click on a row from one on empty space
    void contextMenuRequested(const QPoint &globalPos, bool onItem);
    void selectionChanged();

    // Not applied here, but through the file operations, so KIO records it with
    // the undo manager, which writing to the model would not
    void renameRequested(const QUrl &url, const QString &newName);

    // The destination is the folder under the cursor, or the current folder for
    // a drop on empty space, and the event is still live so must be used at once
    void dropped(QDropEvent *event, const QUrl &destination);

    // A drag rested on a folder long enough for it to open, and is still going
    void springLoaded(const QUrl &folder);

    void viewModeChanged(Settings::ViewMode mode);

    void groupColumnChanged(int sourceColumn);

protected:
    void resizeEvent(QResizeEvent *event) override;

    // Catches clicks on a selection tick box before they become an ordinary
    // selection, the box being drawn by the delegate rather than a model role
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildDetailsView();
    void buildIconView();

    // Shared, so the two views cannot answer different gestures
    void bindActivation(QAbstractItemView *view);
    void applyMode();
    void configureColumns();
    void showHeaderMenu(const QPoint &pos);
    void repositionMessage();

    // Whether indices coming out of the details view need mapping
    bool grouped() const;

    QModelIndex viewIndexFor(const QUrl &url) const;

    // While grouping is on those indices are the proxy's, and the directory
    // model reads only the flat listing's
    KFileItem itemAtViewIndex(const QModelIndex &index) const;

    // A model swap replaces the details view's selection model with a fresh one
    void rebindSelection();

    DirectoryModel *m_model = nullptr;
    GroupingProxy  *m_grouping = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTreeView *m_details = nullptr;
    QListView *m_icons = nullptr;
    QLabel *m_message = nullptr;

    // One per view, a generator attaching to a single view
    KFilePreviewGenerator *m_detailsPreviews = nullptr;
    KFilePreviewGenerator *m_iconPreviews = nullptr;

    Settings::ViewMode m_mode = Settings::ViewMode::Details;
    bool m_checkBoxes = false;
    bool m_singleClick = false;
    QUrl m_destination;

    // So switching a column on later does not reset every other width
    QSet<int> m_sizedColumns;
};
