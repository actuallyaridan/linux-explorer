#include "ComputerView.h"
#include "Assets.h"
#include "aero/capacitybar.h"
#include "ComputerModel.h"
#include "aero/text.h"
#include "aero/listview.h"
#include "aero/artwork.h"
#include "aero/palette.h"

#include <KIO/Global>

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

namespace {

// A 250px tile with a 4px gap
constexpr int kTileWidth = 250;
constexpr int kTileSpacingH = 4;
constexpr int kTileSpacingV = 8;

constexpr int kIconGap = 10;
constexpr int kLineGap = 3;

int tileHeight(const QFontMetrics &fm)
{
    const int text = fm.height() * 2 + 2 * kLineGap + Aero::kCapacityBarHeight;
    return qMax(48, text) + 8;
}

int contentHeight(const QFontMetrics &fm)
{
    const int text = fm.height() + kLineGap + Aero::kCapacityBarHeight;
    return qMax(32, text) + 10;
}

QModelIndex indexForUrl(const QAbstractItemModel *model, const QUrl &url)
{
    if (!url.isValid())
        return {};
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, ComputerModel::Name);
        if (index.data(ComputerModel::UrlRole).toUrl() == url)
            return index;
    }
    return {};
}

// One heading's worth of drives, with no sorting of its own so the order stays
// the drive model's
class DriveFilter : public QSortFilterProxyModel {
public:
    DriveFilter(bool removable, QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
        , m_removable(removable)
    {
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex index =
            sourceModel()->index(row, ComputerModel::Name, parent);
        return index.data(ComputerModel::RemovableRole).toBool() == m_removable;
    }

private:
    bool m_removable;
};

// The two layouts that carry a capacity bar, the rest showing none in Win7
// either and going through the base delegate untouched
class DriveDelegate : public QStyledItemDelegate {
public:
    enum Layout { Standard, Tile, Content };

    using QStyledItemDelegate::QStyledItemDelegate;

    void setLayout(Layout layout) { m_layout = layout; }
    void setRowWidth(int width) { m_rowWidth = width; }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        switch (m_layout) {
        case Tile:
            return QSize(kTileWidth, tileHeight(option.fontMetrics));
        case Content:
            return QSize(qMax(m_rowWidth, 160), contentHeight(option.fontMetrics));
        case Standard:
        default:
            return QStyledItemDelegate::sizeHint(option, index);
        }
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (m_layout == Standard) {
            QStyledItemDelegate::paint(painter, option, index);
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

        const bool selected = opt.state & QStyle::State_Selected;
        const QColor primary = selected ? opt.palette.color(QPalette::HighlightedText)
                                        : Aero::Palette::rgb(Aero::Palette::Text);
        const QColor secondary = selected ? opt.palette.color(QPalette::HighlightedText)
                                          : Aero::Palette::rgb(Aero::Palette::MutedText);

        const int iconSize = (m_layout == Tile) ? 48 : 32;
        const QRect body = option.rect.adjusted(3, 4, -3, -4);

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect iconRect(body.left(),
                             body.top() + (body.height() - iconSize) / 2,
                             iconSize, iconSize);
        icon.paint(painter, iconRect, Qt::AlignCenter,
                   selected ? QIcon::Selected : QIcon::Normal);

        const QRect text = body.adjusted(iconSize + kIconGap, 0, 0, 0);
        if (text.width() <= 0)
            return;

        const bool known = index.data(ComputerModel::SizeKnownRole).toBool();
        const auto total = index.data(ComputerModel::TotalSizeRole).toULongLong();
        const auto available = index.data(ComputerModel::AvailableSizeRole).toULongLong();

        // A pseudo filesystem can report nothing at all
        const int percentUsed = (known && total > 0)
            ? qRound(100.0 * double(total - available) / double(total))
            : 0;

        const QString name = index.data(Qt::DisplayRole).toString();
        // Unmounted or unreadable, so listed but with no figure to draw from
        const QString figures = known
            ? QObject::tr("%1 free of %2").arg(KIO::convertSize(available),
                                               KIO::convertSize(total))
            : QObject::tr("Size unavailable");

        const QFontMetrics fm(opt.font);
        const int lineHeight = fm.height();
        const int barWidth = qMin(Aero::kCapacityBarWidth, text.width());

        painter->save();
        painter->setFont(opt.font);

        if (m_layout == Tile) {
            const int block = known
                ? lineHeight * 2 + 2 * kLineGap + Aero::kCapacityBarHeight
                : lineHeight * 2 + kLineGap;
            int y = text.top() + (text.height() - block) / 2;

            painter->setPen(primary);
            painter->drawText(QRect(text.left(), y, text.width(), lineHeight),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(name, Qt::ElideRight, text.width()));
            y += lineHeight + kLineGap;

            if (known) {
                Aero::paintCapacityBar(
                    painter, QRect(text.left(), y, barWidth, Aero::kCapacityBarHeight),
                    percentUsed, Explorer::Art::capacityBar());
                y += Aero::kCapacityBarHeight + kLineGap;
            }

            painter->setPen(secondary);
            painter->drawText(QRect(text.left(), y, text.width(), lineHeight),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(figures, Qt::ElideRight, text.width()));
        } else {
            // Name over its bar on the left, figures at the far edge
            const int figuresWidth = qMin(fm.horizontalAdvance(figures) + 16,
                                          text.width() / 2);
            const int leftWidth = qMax(60, text.width() - figuresWidth);
            const int block = lineHeight
                + (known ? kLineGap + Aero::kCapacityBarHeight : 0);
            const int y = text.top() + (text.height() - block) / 2;

            painter->setPen(primary);
            painter->drawText(QRect(text.left(), y, leftWidth, lineHeight),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(name, Qt::ElideRight, leftWidth));
            if (known) {
                Aero::paintCapacityBar(
                    painter,
                    QRect(text.left(), y + lineHeight + kLineGap,
                          qMin(barWidth, leftWidth), Aero::kCapacityBarHeight),
                    percentUsed, Explorer::Art::capacityBar());
            }

            painter->setPen(secondary);
            painter->drawText(QRect(text.left() + leftWidth, text.top(),
                                    text.width() - leftWidth, text.height()),
                              Qt::AlignRight | Qt::AlignVCenter,
                              fm.elidedText(figures, Qt::ElideRight, figuresWidth));
        }

        painter->restore();
    }

private:
    Layout m_layout = Tile;
    int m_rowWidth = 400;
};

// One section's drives, and the page scrolls as a whole so a section is laid
// out at exactly the height its drives need and never scrolls on its own
class DriveList : public QListView {
public:
    explicit DriveList(QWidget *parent = nullptr)
        : QListView(parent)
    {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    // Called when an arrow key ran off either end of this section
    std::function<bool(int)> onLeaveEdge;

    // Content rows span the viewport, so their width is restated on resize
    void setFullWidthRows(bool on) { m_fullWidthRows = on; }

    // From the grid rather than the view's own hint, which reports the layout
    // already performed rather than the one for the width about to be given,
    // and so clipped a section that had wrapped
    QSize sizeHint() const override
    {
        const int count = model() ? model()->rowCount(rootIndex()) : 0;
        if (count == 0)
            return QSize(0, 0);

        const QSize grid = gridSize();
        const int width = qMax(viewport()->width(), 1);

        if (viewMode() == QListView::IconMode && isWrapping()) {
            const int cell = qMax(1, grid.width());
            const int perRow = qMax(1, width / cell);
            const int rows = (count + perRow - 1) / perRow;
            return QSize(cell, rows * qMax(1, grid.height()));
        }

        const int rowHeight = grid.height() > 0 ? grid.height()
                                                : qMax(1, sizeHintForRow(0));
        return QSize(width, count * rowHeight);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListView::resizeEvent(event);

        if (m_fullWidthRows && gridSize().width() != viewport()->width()) {
            static_cast<DriveDelegate *>(itemDelegate())
                ->setRowWidth(viewport()->width());
            setGridSize(QSize(viewport()->width(), gridSize().height()));
        }

        updateGeometry();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        const QModelIndex before = currentIndex();
        QListView::keyPressEvent(event);

        if (!onLeaveEdge || !before.isValid() || currentIndex() != before)
            return;

        // The cursor did not move, so it was already at this section's edge,
        // and Win7's groups read as one continuous list to the arrow keys
        switch (event->key()) {
        case Qt::Key_Down:
        case Qt::Key_Right:
            onLeaveEdge(1);
            break;
        case Qt::Key_Up:
        case Qt::Key_Left:
            onLeaveEdge(-1);
            break;
        default:
            break;
        }
    }

private:
    bool m_fullWidthRows = false;
};

} // namespace

ComputerView::ComputerView(ComputerModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget;
    root->addWidget(m_stack);

    buildSections();
    buildDetailsView();

    // The section filters connect first and direct connections run in order,
    // so every proxy has already reset by now
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        updateSections();
        restoreSelection();
    });

    updateSections();
    applyMode();
}

void ComputerView::buildSections()
{
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setObjectName(QStringLiteral("computerScroll"));
    m_scroll->setStyleSheet(Aero::panelSheet(m_scroll->objectName(), Aero::Palette::Surface));

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("computerContent"));
    content->setStyleSheet(Aero::panelSheet(content->objectName(), Aero::Palette::Surface));

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(16);

    addSection(layout, tr("Hard Disk Drives"), false);
    addSection(layout, tr("Devices with Removable Storage"), true);
    layout->addStretch(1);

    m_scroll->setWidget(content);
    m_stack->addWidget(m_scroll);
}

void ComputerView::addSection(QVBoxLayout *layout, const QString &title, bool removable)
{
    Section section;
    section.title = title;

    section.container = new QWidget;
    auto *box = new QVBoxLayout(section.container);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(8);

    auto *heading = new QHBoxLayout;
    heading->setContentsMargins(0, 0, 0, 0);
    heading->setSpacing(8);
    section.heading = Aero::label(title, 11, Aero::Palette::HeadingText);
    heading->addWidget(section.heading, 0, Qt::AlignVCenter);
    heading->addWidget(Aero::hairline(), 1, Qt::AlignVCenter);
    box->addLayout(heading);

    section.filter = new DriveFilter(removable, this);
    section.filter->setSourceModel(m_model);

    auto *list = new DriveList;
    section.view = list;
    list->setModel(section.filter);
    list->setModelColumn(ComputerModel::Name);
    list->setItemDelegate(new DriveDelegate(list));
    list->setFrameShape(QFrame::NoFrame);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setMovement(QListView::Static);
    list->setResizeMode(QListView::Adjust);
    list->setSelectionRectVisible(true);
    list->setUniformItemSizes(true);
    // Everything downstream acts on a single drive
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    Aero::setPointSize(list, 9);
    box->addWidget(list);

    connect(list, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
        const QUrl url = urlAt(index);
        if (url.isValid())
            Q_EMIT urlActivated(url);
    });
    connect(list, &QWidget::customContextMenuRequested, this, [this, list](const QPoint &pos) {
        const QModelIndex index = list->indexAt(pos);
        if (index.isValid())
            setSelectedUrl(urlAt(index));
        // The empty space past the last drive still gets the page's own menu
        Q_EMIT contextMenuRequested(urlAt(index), list->viewport()->mapToGlobal(pos));
    });
    connect(list->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this, list] {
        const QModelIndexList picked = list->selectionModel()->selectedIndexes();
        // An empty selection is this view being cleared as another takes the
        // highlight rather than the user dropping it
        if (!picked.isEmpty())
            setSelectedUrl(urlAt(picked.first()));
    });
    list->onLeaveEdge = [this, list](int direction) {
        return stepToNeighbour(list, direction);
    };

    layout->addWidget(section.container);
    m_sections.append(section);
}

void ComputerView::buildDetailsView()
{
    m_details = new QTreeView;
    m_details->setModel(m_model);
    m_details->setContextMenuPolicy(Qt::CustomContextMenu);
    Aero::setPointSize(m_details, 9);
    Aero::configureListTree(m_details);
    m_details->setSelectionMode(QAbstractItemView::SingleSelection);

    // Win7 gives every column a fixed width and leaves the rest of the row
    // empty rather than stranding the last column at the window edge
    QHeaderView *header = m_details->header();
    header->setStretchLastSection(false);
    header->setSectionsMovable(true);
    m_details->setColumnWidth(ComputerModel::Name, 240);
    m_details->setColumnWidth(ComputerModel::Type, 130);
    m_details->setColumnWidth(ComputerModel::TotalSize, 100);
    m_details->setColumnWidth(ComputerModel::FreeSpace, 100);

    connect(m_details, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
        const QUrl url = urlAt(index);
        if (url.isValid())
            Q_EMIT urlActivated(url);
    });
    connect(m_details, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = m_details->indexAt(pos);
        if (index.isValid())
            setSelectedUrl(urlAt(index));
        Q_EMIT contextMenuRequested(urlAt(index),
                                    m_details->viewport()->mapToGlobal(pos));
    });
    connect(m_details->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] {
        const QModelIndexList picked = m_details->selectionModel()->selectedIndexes();
        if (!picked.isEmpty())
            setSelectedUrl(urlAt(picked.first()));
    });

    m_stack->addWidget(m_details);
}

void ComputerView::setViewMode(Settings::ViewMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    applyMode();
    Q_EMIT viewModeChanged(mode);
}

void ComputerView::focusView()
{
    if (m_mode == Settings::ViewMode::Details) {
        m_details->setFocus(Qt::TabFocusReason);
        return;
    }

    // The first section with drives in it, focusing a hidden one doing nothing
    for (const Section &section : std::as_const(m_sections)) {
        if (section.view && section.view->isVisible()) {
            section.view->setFocus(Qt::TabFocusReason);
            return;
        }
    }
}

void ComputerView::applyMode()
{
    if (m_mode == Settings::ViewMode::Details) {
        m_stack->setCurrentWidget(m_details);
        restoreSelection();
        return;
    }

    m_stack->setCurrentWidget(m_scroll);
    const int iconSize = Settings::iconSizeFor(m_mode);

    for (const Section &section : std::as_const(m_sections)) {
        auto *view = static_cast<DriveList *>(section.view);
        auto *delegate = static_cast<DriveDelegate *>(view->itemDelegate());
        const QFontMetrics fm = view->fontMetrics();

        view->setIconSize(QSize(iconSize, iconSize));
        view->setFullWidthRows(m_mode == Settings::ViewMode::Content);

        switch (m_mode) {
        case Settings::ViewMode::List:
            // Win7 wraps this mode to the right, which a section cannot, the
            // page scrolling down so a wrapped column would run off the side
            delegate->setLayout(DriveDelegate::Standard);
            view->setViewMode(QListView::ListMode);
            view->setFlow(QListView::TopToBottom);
            view->setWrapping(false);
            view->setWordWrap(false);
            view->setTextElideMode(Qt::ElideRight);
            view->setGridSize(QSize());
            break;

        case Settings::ViewMode::Tiles:
            delegate->setLayout(DriveDelegate::Tile);
            view->setViewMode(QListView::IconMode);
            view->setFlow(QListView::LeftToRight);
            view->setWrapping(true);
            view->setWordWrap(false);
            view->setGridSize(QSize(kTileWidth + kTileSpacingH,
                                    tileHeight(fm) + kTileSpacingV));
            break;

        case Settings::ViewMode::Content:
            delegate->setLayout(DriveDelegate::Content);
            delegate->setRowWidth(view->viewport()->width());
            view->setViewMode(QListView::ListMode);
            view->setFlow(QListView::TopToBottom);
            view->setWrapping(false);
            view->setWordWrap(false);
            view->setGridSize(QSize(view->viewport()->width(), contentHeight(fm)));
            break;

        default:
            // Room for two lines of wrapped name beneath the icon
            delegate->setLayout(DriveDelegate::Standard);
            view->setViewMode(QListView::IconMode);
            view->setFlow(QListView::LeftToRight);
            view->setWrapping(true);
            view->setWordWrap(true);
            view->setTextElideMode(Qt::ElideRight);
            view->setGridSize(QSize(qMax(iconSize + 24, 90),
                                    iconSize + 4 * fm.height()));
            break;
        }

        view->updateGeometry();
    }

    restoreSelection();
}

void ComputerView::updateSections()
{
    for (const Section &section : std::as_const(m_sections)) {
        const int count = section.filter->rowCount();
        // A heading with no drives under it is not drawn at all
        section.container->setVisible(count > 0);
        section.heading->setText(
            QStringLiteral("%1 (%2)").arg(section.title).arg(count));
        section.view->updateGeometry();
    }
}

QList<QAbstractItemView *> ComputerView::allViews() const
{
    QList<QAbstractItemView *> views;
    views.reserve(m_sections.size() + 1);
    for (const Section &section : m_sections)
        views.append(section.view);
    views.append(m_details);
    return views;
}

QUrl ComputerView::urlAt(const QModelIndex &index) const
{
    return index.data(ComputerModel::UrlRole).toUrl();
}

void ComputerView::setSelectedUrl(const QUrl &url)
{
    if (m_syncing || m_selectedUrl == url)
        return;
    m_selectedUrl = url;
    restoreSelection();
    Q_EMIT selectionChanged();
}

void ComputerView::restoreSelection()
{
    if (m_syncing)
        return;

    // Restating it clears the other views, reentering here through their own
    // selection signals
    m_syncing = true;
    for (QAbstractItemView *view : allViews()) {
        QItemSelectionModel *selection = view->selectionModel();
        const QModelIndex index = indexForUrl(view->model(), m_selectedUrl);
        if (index.isValid()) {
            selection->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect
                                                  | QItemSelectionModel::Rows);
        } else {
            selection->clearSelection();
        }
    }
    m_syncing = false;
}

bool ComputerView::stepToNeighbour(QListView *from, int direction)
{
    int current = -1;
    for (int i = 0; i < m_sections.size(); ++i) {
        if (m_sections.at(i).view == from) {
            current = i;
            break;
        }
    }
    if (current < 0)
        return false;

    for (int next = current + direction;
         next >= 0 && next < m_sections.size(); next += direction) {
        const Section &section = m_sections.at(next);
        const int rows = section.filter->rowCount();
        if (!section.container->isVisible() || rows == 0)
            continue;

        // Entering from above lands on the first drive and from below the last
        const QModelIndex target = section.filter->index(
            direction > 0 ? 0 : rows - 1, ComputerModel::Name);
        section.view->setFocus();
        section.view->setCurrentIndex(target);
        return true;
    }
    return false;
}

int ComputerView::deviceCount() const
{
    return m_model->rowCount();
}

QModelIndex ComputerView::selectedIndex() const
{
    return indexForUrl(m_model, m_selectedUrl);
}

QModelIndex ComputerView::placeIndexFor(const QUrl &url) const
{
    return m_model->placeIndexFor(url);
}
