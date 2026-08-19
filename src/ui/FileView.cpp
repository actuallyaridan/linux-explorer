#include "FileView.h"
#include "DirectoryModel.h"
#include "GroupingProxy.h"
#include "aero/text.h"
#include "aero/listview.h"
#include "aero/palette.h"

#include <KFilePreviewGenerator>
#include <KIO/Global>

#include <QAbstractProxyModel>
#include <QApplication>
#include <QBasicTimer>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QStyleOptionButton>
#include <QTimerEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QRubberBand>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kMessageTop = 20;

// The selection tick box, when the options have it switched on
constexpr int kCheckSize = 16;
constexpr int kCheckMargin = 2;

// Long enough that dragging across a folder on the way past does not open it
constexpr int kSpringLoadDelay = 700;

// A 48px icon with three lines, and a 32px icon with two
constexpr int kTileWidth = 280;
constexpr int kTileHeight = 60;
constexpr int kContentHeight = 50;

int defaultColumnWidth(int sourceColumn)
{
    switch (sourceColumn) {
    case DirectoryModel::Name:         return 260;
    case DirectoryModel::ModifiedTime: return 140;
    case DirectoryModel::Type:         return 130;
    case DirectoryModel::Size:         return 90;
    default:                           return 110;
    }
}

QString columnTitle(int sourceColumn)
{
    switch (sourceColumn) {
    case DirectoryModel::Name:         return QObject::tr("Name");
    case DirectoryModel::Size:         return QObject::tr("Size");
    case DirectoryModel::ModifiedTime: return QObject::tr("Date modified");
    case DirectoryModel::Permissions:  return QObject::tr("Permissions");
    case DirectoryModel::Owner:        return QObject::tr("Owner");
    case DirectoryModel::Group:        return QObject::tr("Group");
    case DirectoryModel::Type:         return QObject::tr("Type");
    default:                           return {};
    }
}

// Win7's visual order, which is not the order the model stores them in
const QList<int> &visualColumnOrder()
{
    static const QList<int> order = {
        DirectoryModel::Name,
        DirectoryModel::ModifiedTime,
        DirectoryModel::Type,
        DirectoryModel::Size,
        DirectoryModel::Permissions,
        DirectoryModel::Owner,
        DirectoryModel::Group,
    };
    return order;
}

// Back to the flat listing the directory model speaks, since with grouping on
// the details view shows a proxy whose indices it cannot read
QModelIndex mapToFlat(const QModelIndex &index, const QAbstractItemModel *flat)
{
    QModelIndex walk = index;
    while (walk.isValid() && walk.model() != flat) {
        const auto *proxy = qobject_cast<const QAbstractProxyModel *>(walk.model());
        if (!proxy)
            return {};
        walk = proxy->mapToSource(walk);
    }
    return walk;
}

// The two layouts the base delegate has no notion of, plus the inline rename
// editor
//
// The rename is not written back through the model, which would perform it
// without recording it with the undo manager
class ItemDelegate : public QStyledItemDelegate {
public:
    enum Layout { Standard, Tile, Content };

    explicit ItemDelegate(DirectoryModel *model, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_model(model)
    {
    }

    void setLayout(Layout layout) { m_layout = layout; }
    void setRowWidth(int width) { m_rowWidth = width; }

    // A row based mode centres the box at the left of the row, where the icon
    // modes get it in the cell's top left corner
    void setCheckBoxes(bool on, bool rowBased)
    {
        m_checkBoxes = on;
        m_rowBased = rowBased;
    }

    bool checkBoxes() const { return m_checkBoxes; }

    // The views need this too, to tell a click on the box from one on the item
    QRect checkRect(const QRect &itemRect) const
    {
        if (!m_checkBoxes)
            return {};
        if (m_rowBased) {
            return QRect(itemRect.left() + kCheckMargin,
                         itemRect.center().y() - kCheckSize / 2,
                         kCheckSize, kCheckSize);
        }
        return QRect(itemRect.left() + kCheckMargin, itemRect.top() + kCheckMargin,
                     kCheckSize, kCheckSize);
    }

    std::function<void(const QModelIndex &, const QString &)> onRename;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        switch (m_layout) {
        case Tile:    return QSize(kTileWidth, kTileHeight);
        case Content: return QSize(qMax(m_rowWidth, 120), kContentHeight);
        case Standard:
        default:      return QStyledItemDelegate::sizeHint(option, index);
        }
    }

    // Reports the space taken, so the caller can paint clear of it
    int paintCheckBox(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index) const
    {
        // The first logical column is the name whatever the header's order,
        // and a grouping heading has no file to select
        if (!m_checkBoxes || index.column() != 0 || itemFor(index).isNull())
            return 0;

        // The box follows the selection rather than a state of its own
        QStyleOptionButton box;
        box.rect = checkRect(option.rect);
        box.state = QStyle::State_Enabled
            | ((option.state & QStyle::State_Selected) ? QStyle::State_On
                                                       : QStyle::State_Off);
        QStyle *style = option.widget ? option.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &box, painter,
                             option.widget);

        return m_rowBased ? kCheckSize + 2 * kCheckMargin : 0;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (m_layout == Standard) {
            QStyleOptionViewItem shifted = option;
            // The icon modes need no shift, the box sitting over empty space
            const int reserved = paintCheckBox(painter, option, index);
            shifted.rect.setLeft(shifted.rect.left() + reserved);
            QStyledItemDelegate::paint(painter, shifted, index);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        // The style still draws the row, so only the content is ours, hence
        // stripping the icon and text out first
        opt.text.clear();
        opt.icon = QIcon();
        opt.features &= ~QStyleOptionViewItem::HasDecoration;
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        // After the background, or the row's highlight would paint over it
        const int reserved = paintCheckBox(painter, option, index);

        const bool selected = opt.state & QStyle::State_Selected;
        const QColor primary = selected ? opt.palette.color(QPalette::HighlightedText)
                                        : Aero::Palette::rgb(Aero::Palette::Text);
        const QColor secondary = selected ? opt.palette.color(QPalette::HighlightedText)
                                          : Aero::Palette::rgb(Aero::Palette::MutedText);

        const int iconSize = (m_layout == Tile) ? 48 : 32;
        const QRect body = option.rect.adjusted(4 + reserved, 3, -6, -3);

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect iconRect(body.left(),
                             body.top() + (body.height() - iconSize) / 2,
                             iconSize, iconSize);
        icon.paint(painter, iconRect, Qt::AlignCenter,
                   selected ? QIcon::Selected : QIcon::Normal);

        const KFileItem item = itemFor(index);
        const QString name = index.data(Qt::DisplayRole).toString();
        const QString type = item.isNull() ? QString() : item.mimeComment();
        const QString size = (item.isNull() || item.isDir())
            ? QString() : KIO::convertSize(item.size());

        QRect text = body.adjusted(iconSize + 8, 0, 0, 0);
        const QFontMetrics fm(opt.font);
        painter->save();
        painter->setFont(opt.font);

        if (m_layout == Tile) {
            // Three lines, centred as a block against the icon beside them
            QStringList lines{name};
            if (!type.isEmpty())
                lines << type;
            if (!size.isEmpty())
                lines << size;

            const int lineHeight = fm.height();
            int y = text.top() + (text.height() - lineHeight * lines.size()) / 2;
            for (int i = 0; i < lines.size(); ++i) {
                painter->setPen(i == 0 ? primary : secondary);
                painter->drawText(QRect(text.left(), y, text.width(), lineHeight),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  fm.elidedText(lines.at(i), Qt::ElideRight, text.width()));
                y += lineHeight;
            }
        } else {
            // Name and type on the left, size at the far edge
            const int lineHeight = fm.height();
            const int top = text.top() + (text.height() - lineHeight * 2) / 2;
            const int sizeWidth = size.isEmpty() ? 0 : fm.horizontalAdvance(size) + 12;
            const int leftWidth = qMax(20, text.width() - sizeWidth);

            painter->setPen(primary);
            painter->drawText(QRect(text.left(), top, leftWidth, lineHeight),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(name, Qt::ElideRight, leftWidth));
            painter->setPen(secondary);
            painter->drawText(QRect(text.left(), top + lineHeight, leftWidth, lineHeight),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(type, Qt::ElideRight, leftWidth));
            if (!size.isEmpty()) {
                painter->drawText(QRect(text.left() + leftWidth, top,
                                        sizeWidth, lineHeight),
                                  Qt::AlignRight | Qt::AlignVCenter, size);
            }
        }
        painter->restore();
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *line = qobject_cast<QLineEdit *>(editor);
        if (!line) {
            QStyledItemDelegate::setEditorData(editor, index);
            return;
        }

        // The real name on disk, since committing a friendly label or an
        // elided extension would rename the file to a caption
        const KFileItem item = itemFor(index);
        const QString name = item.isNull() ? index.data(Qt::DisplayRole).toString()
                                           : item.name();
        line->setText(name);

        // Windows preselects the base name, leaving the extension alone
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        const bool hasExtension = dot > 0 && !item.isNull() && !item.isDir();
        line->setSelection(0, hasExtension ? dot : name.length());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *,
                      const QModelIndex &index) const override
    {
        auto *line = qobject_cast<QLineEdit *>(editor);
        if (!line || !onRename)
            return;
        onRename(index, line->text().trimmed());
    }

private:
    // The index belongs to whichever model the view is currently on
    KFileItem itemFor(const QModelIndex &index) const
    {
        return m_model->itemForIndex(mapToFlat(index, m_model->model()));
    }

    DirectoryModel *m_model = nullptr;
    Layout m_layout = Standard;
    int m_rowWidth = 400;
    bool m_checkBoxes = false;
    bool m_rowBased = true;
};

// Shared drag and drop plumbing
//
// The drop is overridden because Qt would otherwise hand it to the model, which
// refuses, where it has to become a KIO job, and the drag is overridden because
// the base class accepts drops only over rows advertising themselves as targets
// and leaves the empty space rejecting everything
template <typename Base>
class DropTarget : public Base {
public:
    using Base::Base;

    std::function<void(QDropEvent *)> onDrop;

    // Spring loaded folders, where resting on one during a drag opens it
    std::function<void(const QModelIndex &)> onHoverOpen;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        Base::dragEnterEvent(event);
        if (!event->isAccepted() && event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        // The base call runs first so the drop indicator is still drawn, and
        // only the verdict is overridden
        Base::dragMoveEvent(event);
        if (!event->isAccepted() && event->mimeData()->hasUrls())
            event->acceptProposedAction();

        // Restarted on every row change, so only resting on one opens it
        const QModelIndex under = Base::indexAt(event->position().toPoint());
        if (under == m_hoverIndex)
            return;
        m_hoverIndex = under;
        if (under.isValid())
            m_hoverTimer.start(kSpringLoadDelay, this);
        else
            m_hoverTimer.stop();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        cancelHover();
        Base::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        cancelHover();
        if (onDrop)
            onDrop(event);
        else
            Base::dropEvent(event);
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() != m_hoverTimer.timerId()) {
            // The views run timers of their own
            Base::timerEvent(event);
            return;
        }

        m_hoverTimer.stop();
        const QModelIndex target = m_hoverIndex;
        m_hoverIndex = QModelIndex();
        if (onHoverOpen && target.isValid())
            onHoverOpen(target);
    }

private:
    void cancelHover()
    {
        m_hoverTimer.stop();
        m_hoverIndex = QModelIndex();
    }

    QBasicTimer m_hoverTimer;
    QPersistentModelIndex m_hoverIndex;
};

// The details view, plus the rubber band a tree lacks, which starts only on a
// press that missed every row so it cannot compete with dragging an item out
class DetailsTree : public DropTarget<QTreeView> {
public:
    using DropTarget<QTreeView>::DropTarget;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !indexAt(event->pos()).isValid()) {
            m_banding = true;
            m_origin = event->pos();
            // A modifier adds to the selection, so the starting point is
            // remembered and reapplied on every move
            if (!(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
                clearSelection();
            m_initial = selectionModel()->selection();
            return;
        }
        DropTarget<QTreeView>::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_banding) {
            DropTarget<QTreeView>::mouseMoveEvent(event);
            return;
        }

        if (!m_band)
            m_band = new QRubberBand(QRubberBand::Rectangle, viewport());
        const QRect rect = QRect(m_origin, event->pos()).normalized();
        m_band->setGeometry(rect);
        m_band->show();

        selectionModel()->select(m_initial, QItemSelectionModel::ClearAndSelect);
        setSelection(rect, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_banding) {
            m_banding = false;
            if (m_band)
                m_band->hide();
            return;
        }
        DropTarget<QTreeView>::mouseReleaseEvent(event);
    }

private:
    QRubberBand *m_band = nullptr;
    QItemSelection m_initial;
    QPoint m_origin;
    bool m_banding = false;
};

using IconList = DropTarget<QListView>;

} // namespace

FileView::FileView(DirectoryModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget;
    root->addWidget(m_stack);

    buildDetailsView();
    buildIconView();

    m_grouping = new GroupingProxy(m_model, this);
    // Any rebuild collapses the tree, so the groups are reopened every time,
    // and queued since a direct connection would run before the view's own
    // reset handling and be undone by it
    connect(m_grouping, &QAbstractItemModel::modelReset, this, [this] {
        if (grouped())
            m_details->expandAll();
    }, Qt::QueuedConnection);

    // One selection model for both views, so switching modes keeps it
    rebindSelection();

    // Parented here rather than to a viewport, so it survives the views being
    // swapped under it
    m_message = new QLabel(this);
    m_message->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_message->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_message->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                             .arg(QLatin1String(Aero::Palette::Text)));
    Aero::setPointSize(m_message, 9);
    m_message->hide();

    // Tick box clicks are caught before they turn into selections
    m_details->viewport()->installEventFilter(this);
    m_icons->viewport()->installEventFilter(this);

    m_checkBoxes = Settings::useCheckBoxes();
    applyMode();
}

void FileView::buildDetailsView()
{
    auto *tree = new DetailsTree;
    m_details = tree;
    m_details->setModel(m_model->model());
    m_details->setContextMenuPolicy(Qt::CustomContextMenu);
    m_details->setDragDropMode(QAbstractItemView::DragDrop);
    m_details->setDefaultDropAction(Qt::MoveAction);
    m_details->setDropIndicatorShown(true);
    Aero::setPointSize(m_details, 9);
    Aero::configureListTree(m_details);

    tree->onDrop = [this](QDropEvent *event) {
        const KFileItem item =
            itemAtViewIndex(m_details->indexAt(event->position().toPoint()));
        Q_EMIT dropped(event, (!item.isNull() && item.isDir()) ? item.url()
                                                               : m_destination);
    };

    // Only folders spring open, a drag resting on a file heading for the
    // folder behind it
    tree->onHoverOpen = [this](const QModelIndex &index) {
        const KFileItem item = itemAtViewIndex(index);
        if (!item.isNull() && item.isDir())
            Q_EMIT springLoaded(item.url());
    };

    auto *delegate = new ItemDelegate(m_model, this);
    delegate->onRename = [this](const QModelIndex &index, const QString &name) {
        const KFileItem item = itemAtViewIndex(index);
        if (!item.isNull() && !name.isEmpty() && name != item.name())
            Q_EMIT renameRequested(item.url(), name);
    };
    m_details->setItemDelegate(delegate);

    QHeaderView *header = m_details->header();
    header->setSectionsMovable(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested,
            this, &FileView::showHeaderMenu);

    // Not the view's own activation, which follows the style's single click
    // hint, where the folder options have to override it
    bindActivation(m_details);
    connect(m_details, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const bool onItem = m_details->indexAt(pos).isValid();
        // A click that missed every row gets the folder's menu, so the
        // selection is dropped or the properties report on it instead
        if (!onItem)
            m_details->clearSelection();
        Q_EMIT contextMenuRequested(m_details->viewport()->mapToGlobal(pos),
                                    onItem);
    });

    // On for the visible view only, both generators running against the same
    // model, so leaving the hidden one on renders every file twice
    m_detailsPreviews = new KFilePreviewGenerator(m_details);
    m_detailsPreviews->setPreviewShown(false);

    m_stack->addWidget(m_details);
    configureColumns();
}

void FileView::buildIconView()
{
    auto *list = new IconList;
    m_icons = list;
    m_icons->setModel(m_model->model());
    m_icons->setModelColumn(0);
    m_icons->setContextMenuPolicy(Qt::CustomContextMenu);
    m_icons->setDragDropMode(QAbstractItemView::DragDrop);
    m_icons->setDefaultDropAction(Qt::MoveAction);
    m_icons->setDropIndicatorShown(true);
    m_icons->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_icons->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_icons->setFrameShape(QFrame::NoFrame);
    m_icons->setResizeMode(QListView::Adjust);
    m_icons->setSelectionRectVisible(true);
    m_icons->setMovement(QListView::Static);
    Aero::setPointSize(m_icons, 9);

    list->onDrop = [this](QDropEvent *event) {
        const KFileItem item =
            itemAtViewIndex(m_icons->indexAt(event->position().toPoint()));
        Q_EMIT dropped(event, (!item.isNull() && item.isDir()) ? item.url()
                                                               : m_destination);
    };

    // Only folders spring open, a drag resting on a file heading for the
    // folder behind it
    list->onHoverOpen = [this](const QModelIndex &index) {
        const KFileItem item = itemAtViewIndex(index);
        if (!item.isNull() && item.isDir())
            Q_EMIT springLoaded(item.url());
    };

    auto *delegate = new ItemDelegate(m_model, this);
    delegate->onRename = [this](const QModelIndex &index, const QString &name) {
        const KFileItem item = itemAtViewIndex(index);
        if (!item.isNull() && !name.isEmpty() && name != item.name())
            Q_EMIT renameRequested(item.url(), name);
    };
    m_icons->setItemDelegate(delegate);

    // The icon views never show the grouping proxy, but map anyway so both
    // paths keep the promise by construction
    bindActivation(m_icons);
    connect(m_icons, &QListView::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const bool onItem = m_icons->indexAt(pos).isValid();
        if (!onItem)
            m_icons->clearSelection();
        Q_EMIT contextMenuRequested(m_icons->viewport()->mapToGlobal(pos),
                                    onItem);
    });

    m_iconPreviews = new KFilePreviewGenerator(m_icons);
    m_iconPreviews->setPreviewShown(false);

    m_stack->addWidget(m_icons);
}

void FileView::configureColumns()
{
    QHeaderView *header = m_details->header();

    // Win7 gives every column a fixed width and leaves the remainder empty,
    // where the shared setup stretches the last section and would strand its
    // values at the window edge
    header->setStretchLastSection(false);

    int visual = 0;
    for (int source : visualColumnOrder()) {
        const int logical = m_model->viewColumnFor(source);
        if (logical < 0)
            continue;
        header->moveSection(header->visualIndex(logical), visual++);

        // Once only, applying it again undoing the user's dragged widths
        if (!m_sizedColumns.contains(source)) {
            m_details->setColumnWidth(logical, defaultColumnWidth(source));
            m_sizedColumns.insert(source);
        }
    }
}

void FileView::showHeaderMenu(const QPoint &pos)
{
    QMenu menu(this);

    // Name is absent, every other column annotating it
    for (int source : visualColumnOrder()) {
        if (source == DirectoryModel::Name)
            continue;

        QAction *action = menu.addAction(columnTitle(source));
        action->setCheckable(true);
        action->setChecked(m_model->isColumnVisible(source));
        connect(action, &QAction::toggled, this, [this, source](bool on) {
            // Adding or removing a column renumbers everything after it, so
            // the sort is restated in source terms and applied again
            const int sortSource = m_model->sortColumn();
            const Qt::SortOrder order = m_model->sortOrder();

            m_model->setColumnVisible(source, on);

            const int restored = m_model->viewColumnFor(
                sortSource >= 0 ? sortSource : DirectoryModel::Name);
            m_details->header()->setSortIndicator(
                restored >= 0 ? restored : 0, order);
            m_model->sort(sortSource >= 0 ? sortSource : DirectoryModel::Name, order);
            configureColumns();
        });
    }

    menu.addSeparator();
    menu.addAction(tr("Size All Columns to Fit"), this, [this] {
        for (int column = 0; column < m_model->visibleColumns().size(); ++column)
            m_details->resizeColumnToContents(column);
    });

    menu.exec(m_details->header()->mapToGlobal(pos));
}

bool FileView::grouped() const
{
    return m_details->model() == m_grouping;
}

KFileItem FileView::itemAtViewIndex(const QModelIndex &index) const
{
    return m_model->itemForIndex(mapToFlat(index, m_model->model()));
}

void FileView::rebindSelection()
{
    // Not while grouping is on, the views then being on different models and a
    // selection model belonging to one
    if (!grouped())
        m_icons->setSelectionModel(m_details->selectionModel());

    for (QAbstractItemView *view : {static_cast<QAbstractItemView *>(m_details),
                                    static_cast<QAbstractItemView *>(m_icons)}) {
        if (QItemSelectionModel *selection = view->selectionModel()) {
            // Setting a model hands the view a fresh selection model but
            // leaves the old one's connections in place
            disconnect(selection, &QItemSelectionModel::selectionChanged,
                       this, &FileView::selectionChanged);
            connect(selection, &QItemSelectionModel::selectionChanged,
                    this, &FileView::selectionChanged);
        }
    }
}

void FileView::setGroupColumn(int sourceColumn)
{
    if (m_grouping->groupColumn() == sourceColumn)
        return;

    const bool enabling = sourceColumn >= 0;
    const bool wasGrouped = grouped();

    // Setting a model resets the header, and the widths should survive that
    const QByteArray header = m_details->header()->saveState();

    m_grouping->setGroupColumn(sourceColumn);

    if (enabling && !wasGrouped) {
        m_grouping->setSourceModel(m_model->model());
        m_details->setModel(m_grouping);
        m_details->setRootIsDecorated(true);
        m_details->setItemsExpandable(true);
        m_details->header()->restoreState(header);
        rebindSelection();
    } else if (!enabling && wasGrouped) {
        m_details->setModel(m_model->model());
        m_details->setRootIsDecorated(false);
        m_details->setItemsExpandable(false);
        m_details->header()->restoreState(header);
        rebindSelection();
    }

    if (enabling) {
        m_details->expandAll();
        // Only the details view can draw groups
        setViewMode(Settings::ViewMode::Details);
    }

    Q_EMIT groupColumnChanged(m_grouping->groupColumn());
    Q_EMIT selectionChanged();
}

int FileView::groupColumn() const
{
    return m_grouping->groupColumn();
}

void FileView::setViewMode(Settings::ViewMode mode)
{
    if (m_mode == mode)
        return;

    // The icon views show one level, so picking one drops the grouping rather
    // than hiding every file inside its heading
    if (mode != Settings::ViewMode::Details && groupColumn() >= 0)
        setGroupColumn(-1);

    m_mode = mode;
    applyMode();
    Q_EMIT viewModeChanged(mode);
}

void FileView::applyMode()
{
    const int iconSize = Settings::iconSizeFor(m_mode);
    const bool details = (m_mode == Settings::ViewMode::Details);

    // Only the view on show generates previews
    m_detailsPreviews->setPreviewShown(details);
    m_iconPreviews->setPreviewShown(!details);

    if (details) {
        static_cast<ItemDelegate *>(m_details->itemDelegate())
            ->setCheckBoxes(m_checkBoxes, true);
        m_stack->setCurrentWidget(m_details);
        m_details->setIconSize(QSize(iconSize, iconSize));
        repositionMessage();
        return;
    }

    m_stack->setCurrentWidget(m_icons);
    m_icons->setIconSize(QSize(iconSize, iconSize));

    auto *delegate = static_cast<ItemDelegate *>(m_icons->itemDelegate());

    // Full width rows put the box at the left, the grid modes in the corner
    delegate->setCheckBoxes(m_checkBoxes,
                            m_mode == Settings::ViewMode::List
                                || m_mode == Settings::ViewMode::Content);

    switch (m_mode) {
    case Settings::ViewMode::List:
        delegate->setLayout(ItemDelegate::Standard);
        m_icons->setViewMode(QListView::ListMode);
        m_icons->setFlow(QListView::TopToBottom);
        m_icons->setWrapping(true);
        m_icons->setWordWrap(false);
        m_icons->setUniformItemSizes(false);
        m_icons->setGridSize(QSize());
        m_icons->setTextElideMode(Qt::ElideRight);
        break;

    case Settings::ViewMode::Tiles:
        delegate->setLayout(ItemDelegate::Tile);
        m_icons->setViewMode(QListView::IconMode);
        m_icons->setFlow(QListView::LeftToRight);
        m_icons->setWrapping(true);
        m_icons->setWordWrap(false);
        m_icons->setUniformItemSizes(true);
        m_icons->setGridSize(QSize(kTileWidth, kTileHeight));
        break;

    case Settings::ViewMode::Content:
        // The grid width spans the viewport, so it is restated on every resize
        delegate->setLayout(ItemDelegate::Content);
        m_icons->setViewMode(QListView::ListMode);
        m_icons->setFlow(QListView::TopToBottom);
        m_icons->setWrapping(false);
        m_icons->setWordWrap(false);
        m_icons->setUniformItemSizes(true);
        delegate->setRowWidth(m_icons->viewport()->width());
        m_icons->setGridSize(QSize(m_icons->viewport()->width(), kContentHeight));
        break;

    default:
        // Room for two lines of wrapped name beneath the icon
        delegate->setLayout(ItemDelegate::Standard);
        m_icons->setViewMode(QListView::IconMode);
        m_icons->setFlow(QListView::LeftToRight);
        m_icons->setWrapping(true);
        m_icons->setWordWrap(true);
        m_icons->setUniformItemSizes(true);
        m_icons->setTextElideMode(Qt::ElideRight);
        m_icons->setGridSize(QSize(qMax(iconSize + 24, 80),
                                   iconSize + 4 * fontMetrics().height()));
        break;
    }

    repositionMessage();
}

QAbstractItemView *FileView::currentView() const
{
    return m_mode == Settings::ViewMode::Details
        ? static_cast<QAbstractItemView *>(m_details)
        : static_cast<QAbstractItemView *>(m_icons);
}

void FileView::focusView()
{
    currentView()->setFocus(Qt::TabFocusReason);
}

void FileView::bindActivation(QAbstractItemView *view)
{
    // Activation promises an index into the flat listing, so map first
    const auto activate = [this](const QModelIndex &index) {
        const QModelIndex flat = mapToFlat(index, m_model->model());
        if (flat.isValid())
            Q_EMIT activated(flat);
    };

    connect(view, &QAbstractItemView::clicked, this,
            [this, activate](const QModelIndex &index) {
        if (m_singleClick)
            activate(index);
    });
    connect(view, &QAbstractItemView::doubleClicked, this,
            [this, activate](const QModelIndex &index) {
        if (!m_singleClick)
            activate(index);
    });

    // A shortcut scoped to the view rather than a key handler, so neither view
    // class has to know about it
    for (const QKeySequence &key : {QKeySequence(Qt::Key_Return),
                                    QKeySequence(Qt::Key_Enter)}) {
        auto *action = new QAction(view);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, [view, activate] {
            const QModelIndex current = view->currentIndex();
            if (current.isValid())
                activate(current);
        });
        view->addAction(action);
    }
}

void FileView::setSingleClickToOpen(bool single)
{
    m_singleClick = single;
    // The pointing hand is what says a single click will do something
    const Qt::CursorShape shape = single ? Qt::PointingHandCursor : Qt::ArrowCursor;
    m_details->viewport()->setCursor(shape);
    m_icons->viewport()->setCursor(shape);
}

void FileView::setCheckBoxesVisible(bool visible)
{
    m_checkBoxes = visible;
    applyMode();   // the delegates are configured from there, per mode
    m_details->viewport()->update();
    m_icons->viewport()->update();
}

bool FileView::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_checkBoxes || event->type() != QEvent::MouseButtonPress)
        return QWidget::eventFilter(watched, event);

    QAbstractItemView *view = currentView();
    if (watched != view->viewport())
        return QWidget::eventFilter(watched, event);

    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton)
        return QWidget::eventFilter(watched, event);

    const QPoint pos = mouse->position().toPoint();
    const QModelIndex index = view->indexAt(pos);
    // Group headings have no box, so their clicks are ordinary ones
    if (!index.isValid() || itemAtViewIndex(index).isNull())
        return QWidget::eventFilter(watched, event);

    // Both views are given this delegate at construction and never any other,
    // and it carries no meta object for a checked cast to work from
    auto *delegate = static_cast<ItemDelegate *>(view->itemDelegate());
    if (!delegate->checkRect(view->visualRect(index)).contains(pos))
        return QWidget::eventFilter(watched, event);

    // A tick toggles that row alone, which is what the box is for
    const bool selected = view->selectionModel()->isSelected(index);
    view->selectionModel()->select(
        index, (selected ? QItemSelectionModel::Deselect : QItemSelectionModel::Select)
                   | QItemSelectionModel::Rows);
    return true;
}


QItemSelectionModel *FileView::selectionModel() const
{
    return m_details->selectionModel();
}

QModelIndexList FileView::selectedIndexes() const
{
    QAbstractItemView *view = currentView();
    QItemSelectionModel *selection = view->selectionModel();
    if (!selection)
        return {};

    const QModelIndexList indexes = selection->selectedIndexes();
    if (view != m_details || !grouped())
        return indexes;

    // Headings map to nothing and drop out, so selecting one and pressing
    // Delete does nothing rather than surprising anybody
    QModelIndexList mapped;
    mapped.reserve(indexes.size());
    for (const QModelIndex &index : indexes) {
        if (m_grouping->isGroup(index))
            continue;
        const QModelIndex source = m_grouping->mapToSource(index);
        if (source.isValid())
            mapped.append(source);
    }
    return mapped;
}

QModelIndex FileView::viewIndexFor(const QUrl &url) const
{
    const QModelIndex flat = m_model->indexForUrl(url);
    if (!flat.isValid())
        return {};
    if (currentView() == m_details && grouped())
        return m_grouping->mapFromSource(flat);
    return flat;
}

void FileView::renameItem(const QUrl &url)
{
    const QModelIndex index = viewIndexFor(url);
    if (!index.isValid())
        return;

    QAbstractItemView *view = currentView();
    // Editing is off by default, so a stray double click cannot start a rename
    view->setEditTriggers(QAbstractItemView::AllEditTriggers);
    view->setCurrentIndex(index);
    view->edit(index);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

bool FileView::selectUrl(const QUrl &url, bool startRename)
{
    const QModelIndex index = viewIndexFor(url);
    if (!index.isValid())
        return false;

    QAbstractItemView *view = currentView();
    view->setCurrentIndex(index);
    view->selectionModel()->select(index,
                                   QItemSelectionModel::ClearAndSelect
                                       | QItemSelectionModel::Rows);
    view->scrollTo(index, QAbstractItemView::EnsureVisible);
    if (startRename)
        renameItem(url);
    return true;
}

QList<QUrl> FileView::selectUrls(const QList<QUrl> &urls)
{
    QAbstractItemView *view = currentView();
    QItemSelectionModel *selection = view->selectionModel();
    if (!selection)
        return urls;

    QList<QUrl> missing;
    QModelIndexList found;
    for (const QUrl &url : urls) {
        const QModelIndex index = viewIndexFor(url);
        if (index.isValid())
            found.append(index);
        else
            missing.append(url);
    }

    // The existing selection is left alone, so a request naming a file that
    // never turns up changes nothing rather than clearing it
    if (found.isEmpty())
        return missing;

    // First and on its own, since setting the current index applies the view's
    // own selection command and would clear anything already selected
    view->setCurrentIndex(found.first());
    for (const QModelIndex &index : found) {
        selection->select(index, QItemSelectionModel::Select
                                     | QItemSelectionModel::Rows);
    }
    view->scrollTo(found.first(), QAbstractItemView::EnsureVisible);
    return missing;
}

void FileView::sortBy(int sourceColumn, Qt::SortOrder order)
{
    const int logical = m_model->viewColumnFor(sourceColumn);
    if (logical < 0)
        return;   // sorting by a column that is not on show has no indicator to move
    m_details->header()->setSortIndicator(logical, order);
}

void FileView::selectAll()
{
    currentView()->selectAll();
}

void FileView::invertSelection()
{
    QAbstractItemView *view = currentView();
    QAbstractItemModel *model = view->model();
    QItemSelectionModel *selection = view->selectionModel();
    if (!model || !selection)
        return;

    const int columns = model->columnCount();
    const int rows = model->rowCount();
    if (rows == 0 || columns == 0)
        return;

    // In one call, since each one reports a change and doing it per row would
    // rebuild the details pane once per file
    QItemSelection everything;
    if (view == m_details && grouped()) {
        // The files live under the headings, so each group is its own range
        for (int group = 0; group < rows; ++group) {
            const QModelIndex parent = model->index(group, 0);
            const int children = model->rowCount(parent);
            if (children > 0) {
                everything.select(model->index(0, 0, parent),
                                  model->index(children - 1, columns - 1, parent));
            }
        }
    } else {
        everything.select(model->index(0, 0), model->index(rows - 1, columns - 1));
    }

    selection->select(everything, QItemSelectionModel::Toggle);
}

QByteArray FileView::headerState() const
{
    return m_details->header()->saveState();
}

void FileView::setHeaderState(const QByteArray &state)
{
    if (state.isEmpty())
        return;
    m_details->header()->restoreState(state);
    // Or the restored widths go the next time a column is switched on
    for (int source : visualColumnOrder())
        m_sizedColumns.insert(source);
}

void FileView::setStatusMessage(const QString &message)
{
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
    repositionMessage();
}

void FileView::repositionMessage()
{
    if (!m_message || m_message->isHidden())
        return;

    // From the viewport rather than the widget, Win7 keeping the column headers
    // visible over an empty folder and putting the message below them
    QAbstractItemView *view = currentView();
    const int top = view->viewport()->mapTo(this, QPoint(0, 0)).y() + kMessageTop;
    m_message->setGeometry(0, top, width(), m_message->sizeHint().height());
    m_message->raise();
}

void FileView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    repositionMessage();

    if (m_mode == Settings::ViewMode::Content) {
        auto *delegate = static_cast<ItemDelegate *>(m_icons->itemDelegate());
        const int width = m_icons->viewport()->width();
        delegate->setRowWidth(width);
        m_icons->setGridSize(QSize(width, kContentHeight));
    }
}
