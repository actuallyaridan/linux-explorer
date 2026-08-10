#include "DirectoryModel.h"
#include "Branding.h"
#include "Settings.h"

#include <KDirLister>
#include <KDirModel>
#include <KDirSortFilterProxyModel>
#include <KIO/Global>
#include <KIO/Job>
#include <KIO/Paste>
#include <KIO/SimpleJob>
#include <KUrlMimeData>

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QPixmap>
#include <QSet>

namespace {

// How solid a hidden entry's icon is drawn. Windows ghosts them rather than
// greying them, so the shape stays readable.
constexpr qreal kHiddenIconOpacity = 0.45;

// Windows draws an item that is on the clipboard for a move noticeably fainter
// than a merely hidden one, so the two states stay distinguishable when a
// hidden file has been cut.
constexpr qreal kCutIconOpacity = 0.30;

// How many ghosted icons are kept before the cache is dropped and refilled.
// Comfortably more than a screenful in any view mode.
constexpr int kMaxFadedIcons = 512;

// What the details view starts with. The rest can be switched on from the
// header's context menu, as in Win7.
const QList<int> &defaultColumns()
{
    static const QList<int> columns = {
        DirectoryModel::Name,
        DirectoryModel::Size,
        DirectoryModel::ModifiedTime,
        DirectoryModel::Type,
    };
    return columns;
}

// An absolute timestamp in the user's own format.
//
// KDirModel's date column goes through KFormat, which prefers "Just now" and
// "26 minutes ago". That reads well in isolation but is useless in a column,
// where the point is comparing rows, and it goes stale while the window is
// open.
//
// QLocale::system() rather than QLocale(): the two disagree whenever LC_TIME
// differs from LANG, and LC_TIME is what says how this user writes a date.
QString formatTimestamp(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    const QLocale locale = QLocale::system();
    return locale.toString(when.date(), QLocale::ShortFormat)
         + QLatin1Char(' ')
         + locale.toString(when.time(), QLocale::ShortFormat);
}

} // namespace

// Win7's details view prints the name in black and every other column in grey,
// so the name stays what the eye lands on.
//
// Applied on the model rather than through a delegate: a delegate would have to
// draw the whole item to change one colour, and would then owe the selection
// highlight, focus rectangle and icon placement itself.
class Win7ColumnProxy : public KDirSortFilterProxyModel {
public:
    explicit Win7ColumnProxy(QObject *parent = nullptr)
        : KDirSortFilterProxyModel(parent)
        , m_columns(defaultColumns())
    {
        // Read from the clipboard rather than remembered from our own Ctrl+X:
        // another file manager's cut has to ghost these items too, and ours can
        // be superseded by any application that takes the clipboard next.
        connect(QApplication::clipboard(), &QClipboard::dataChanged,
                this, &Win7ColumnProxy::refreshCutItems);
        refreshCutItems();
    }

    QList<int> columns() const { return m_columns; }

    void setColumns(const QList<int> &sourceColumns)
    {
        // Every other column annotates the name.
        QList<int> wanted = sourceColumns;
        if (!wanted.contains(DirectoryModel::Name))
            wanted.prepend(DirectoryModel::Name);
        std::sort(wanted.begin(), wanted.end());
        if (wanted == m_columns)
            return;

        // Structural: without the begin/end pair, attached views go on
        // addressing columns that no longer exist.
        beginResetModel();
        m_columns = wanted;
        endResetModel();
    }

    // The survivors are re-indexed in source order, which viewColumnFor()
    // relies on.
    bool filterAcceptsColumn(int sourceColumn, const QModelIndex &) const override
    {
        return m_columns.contains(sourceColumn);
    }

    // Drops the directories Windows-friendly mode hides rather than renames
    // (see Branding::isSystemFolder). "Show hidden files" brings them back, so
    // nothing is ever made unreachable.
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        if (Branding::windowsFriendlyMode()) {
            if (auto *dirModel = qobject_cast<KDirModel *>(sourceModel())) {
                if (!dirModel->dirLister()->showHiddenFiles()) {
                    const QModelIndex sourceIndex =
                        dirModel->index(sourceRow, KDirModel::Name, sourceParent);
                    const KFileItem item = dirModel->itemForIndex(sourceIndex);
                    if (!item.isNull()
                        && Branding::isSystemFolder(item.url().toLocalFile())) {
                        return false;
                    }
                }
            }
        }
        return KDirSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }

    // Sorts on what the user can read. Otherwise the rows keep the order of
    // their real names: "Boot, dev, System Configuration, Users, lib" is
    // alphabetical by /boot, /dev, /etc, /home, /lib but nonsense on screen.
    //
    // Folders-first still comes from the base class, applied in lessThan()
    // before it delegates here.
    bool subSortLessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        if (left.column() == KDirModel::Name) {
            if (auto *dirModel = qobject_cast<KDirModel *>(sourceModel())) {
                const KFileItem leftItem = dirModel->itemForIndex(left);
                const KFileItem rightItem = dirModel->itemForIndex(right);
                if (!leftItem.isNull() && !rightItem.isNull()) {
                    const QString leftName = displayName(leftItem);
                    const QString rightName = displayName(rightItem);
                    const int order =
                        QString::compare(leftName, rightName, Qt::CaseInsensitive);
                    if (order != 0)
                        return order < 0;
                }
            }
        }
        return KDirSortFilterProxyModel::subSortLessThan(left, right);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return KDirSortFilterProxyModel::data(index, role);

        // In KDirModel's numbering: the view's column N is only the source's
        // column N while every column is switched on.
        const QModelIndex sourceIndex = mapToSource(index);
        const int column = sourceIndex.column();
        const KFileItem item = fileItem(sourceIndex);

        if (role == Qt::ForegroundRole && column != DirectoryModel::Name)
            return QBrush(QColor(0x5A, 0x5A, 0x5A));

        // Hidden entries are ghosted the way Windows marks them, and so are
        // entries on the clipboard for a move. Only the icon fades.
        if (role == Qt::DecorationRole && column == DirectoryModel::Name
            && !item.isNull()) {
            const bool cut = isCut(item);
            const bool ghosted = item.isHidden()
                || Branding::isSystemFolder(item.url().toLocalFile());
            if (cut || ghosted) {
                const QVariant base = KDirSortFilterProxyModel::data(index, role);
                const QIcon icon = qvariant_cast<QIcon>(base);
                if (!icon.isNull()) {
                    return fadedIcon(icon,
                                     cut ? kCutIconOpacity : kHiddenIconOpacity,
                                     cut);
                }
            }
        }

        if (role == Qt::DisplayRole && !item.isNull()) {
            switch (column) {
            case DirectoryModel::ModifiedTime:
                return formatTimestamp(item.time(KFileItem::ModificationTime));

            // Blank for folders, as in Win7. KDirModel reports the directory's
            // own inode size, a few hundred bytes saying nothing about what it
            // holds; the real figure needs a tree walk, which is a deliberate
            // action rather than something a listing does per row.
            case DirectoryModel::Size:
                if (item.isDir())
                    return QString();
                break;

            // Applied here so the whole list picks them up at once and only the
            // displayed string changes; the item's URL is untouched.
            case DirectoryModel::Name:
                return displayName(item);

            default:
                break;
            }
        }

        // Win7's infotip. The details view has columns for most of this, but the
        // icon views do not, and a name elided to the grid cell is exactly when
        // the user reaches for a hover.
        if (role == Qt::ToolTipRole && !item.isNull())
            return infoTip(item);

        return KDirSortFilterProxyModel::data(index, role);
    }

private:
    KFileItem fileItem(const QModelIndex &sourceIndex) const
    {
        auto *dirModel = qobject_cast<KDirModel *>(sourceModel());
        if (!dirModel || !sourceIndex.isValid())
            return KFileItem();
        return dirModel->itemForIndex(sourceIndex);
    }

    QString displayName(const KFileItem &item) const
    {
        QString name = item.text();

        if (Branding::windowsFriendlyMode()) {
            const QString mapped = Branding::folderName(item.url().toLocalFile());
            if (!mapped.isEmpty())
                return mapped;   // a renamed system folder has no extension to hide
        }

        if (Settings::hideKnownExtensions() && !item.isDir()) {
            // "Known" means the desktop can name the type: stripping the suffix
            // off an unrecognised file would hide the only clue to what it is.
            static const QString unknown =
                QMimeDatabase().mimeTypeForName(QStringLiteral("application/octet-stream")).name();
            if (!item.mimetype().isEmpty() && item.mimetype() != unknown) {
                const QString suffix = QFileInfo(name).completeSuffix();
                if (!suffix.isEmpty() && name.length() > suffix.length() + 1)
                    name.chop(suffix.length() + 1);
            }
        }
        return name;
    }

    QString infoTip(const KFileItem &item) const
    {
        QStringList lines;
        lines << displayName(item);
        if (!item.mimeComment().isEmpty())
            lines << QStringLiteral("Type: %1").arg(item.mimeComment());
        const QString when = formatTimestamp(item.time(KFileItem::ModificationTime));
        if (!when.isEmpty())
            lines << QStringLiteral("Date modified: %1").arg(when);
        if (!item.isDir())
            lines << QStringLiteral("Size: %1").arg(KIO::convertSize(item.size()));
        return lines.join(QLatin1Char('\n'));
    }

    bool isCut(const KFileItem &item) const
    {
        return !m_cutUrls.isEmpty() && m_cutUrls.contains(item.url());
    }

    void refreshCutItems()
    {
        QSet<QUrl> urls;
        const QMimeData *mimeData = QApplication::clipboard()->mimeData();
        if (mimeData && KIO::isClipboardDataCut(mimeData)) {
            const QList<QUrl> cut = KUrlMimeData::urlsFromMimeData(mimeData);
            urls = QSet<QUrl>(cut.begin(), cut.end());
        }
        if (urls == m_cutUrls)
            return;
        m_cutUrls = urls;

        // The whole row, though only the name column ghosts: working out which
        // rows changed costs more than the repaint.
        if (rowCount() > 0) {
            Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                               {Qt::DecorationRole});
        }
    }

    // A translucent copy of an icon, cached by the icon's own contents. The
    // cache is a requirement rather than an optimisation: data() runs for every
    // visible cell on every repaint.
    //
    // Keyed on cacheKey() rather than the icon name, which belongs to the MIME
    // type and is shared by every file of it. With previews on, the icon here
    // is one file's thumbnail, so a per-type key gave every ghosted photo in a
    // folder the first one's picture.
    QIcon fadedIcon(const QIcon &source, qreal opacity, bool cut) const
    {
        const QString key = (cut ? QStringLiteral("cut:") : QStringLiteral("hidden:"))
                          + QString::number(source.cacheKey());
        const auto cached = m_fadedIcons.constFind(key);
        if (cached != m_fadedIcons.constEnd())
            return cached.value();

        QIcon faded;
        // The sizes a listing actually asks for; availableSizes() comes back
        // empty for scalable themes.
        for (int size : {16, 22, 24, 32, 48, 64, 96, 128, 256}) {
            const QPixmap original = source.pixmap(size, size);
            if (original.isNull())
                continue;

            QPixmap ghost(original.size());
            ghost.setDevicePixelRatio(original.devicePixelRatio());
            ghost.fill(Qt::transparent);

            QPainter painter(&ghost);
            painter.setOpacity(opacity);
            painter.drawPixmap(0, 0, original);
            painter.end();

            faded.addPixmap(ghost);
        }

        // Bounded by hand, a key now being one icon rather than one MIME type:
        // a folder of thumbnails would otherwise keep an entry per file for the
        // window's lifetime. Dropped wholesale rather than by age, since what is
        // on screen is rebuilt on the next repaint anyway.
        if (m_fadedIcons.size() >= kMaxFadedIcons)
            m_fadedIcons.clear();

        m_fadedIcons.insert(key, faded);
        return faded;
    }

    QList<int> m_columns;
    QSet<QUrl> m_cutUrls;
    mutable QHash<QString, QIcon> m_fadedIcons;
};

DirectoryModel::DirectoryModel(QObject *parent)
    : QObject(parent)
    , m_dirModel(new KDirModel(this))
    , m_proxy(new Win7ColumnProxy(this))
{
    m_proxy->setSourceModel(m_dirModel);
    m_proxy->setSortFoldersFirst(true);
    // Win7 sorts case-insensitively, so "Documents" and "documents" sit
    // together rather than in two blocks.
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->sort(KDirModel::Name, Qt::AscendingOrder);

    // Name column only: the default of -1 matches every column, so typing a
    // size or date fragment would silently "find" files too.
    m_proxy->setFilterKeyColumn(KDirModel::Name);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    // KDirModel disables drops by default, so without this the view accepts the
    // drag and does nothing with it. Only advertises the rows as targets; the
    // drop itself is KIO::DropJob's, in the view.
    m_dirModel->setDropsAllowed(KDirModel::DropOnDirectory);

    KDirLister *lister = m_dirModel->dirLister();
    lister->setShowHiddenFiles(Settings::showHiddenFiles());
    lister->setAutoErrorHandlingEnabled(false);   // we surface errors ourselves

    connect(lister, &KCoreDirLister::started, this, &DirectoryModel::loadingStarted);
    connect(lister, &KCoreDirLister::completed, this, &DirectoryModel::loadingFinished);
    connect(lister, &KCoreDirLister::percent, this, &DirectoryModel::loadingProgress);
    connect(lister, &KCoreDirLister::jobError, this, [this](KIO::Job *job) {
        // The failing job names the directory it was reading, which for a
        // listing that walked into a subdirectory is not the lister's own.
        QUrl url;
        if (auto *simple = qobject_cast<KIO::SimpleJob *>(job))
            url = simple->url();
        Q_EMIT errorOccurred(job->error(), job->errorString(),
                             url.isValid() ? url : m_dirModel->dirLister()->url());
    });
    connect(lister, &KCoreDirLister::itemsAdded, this,
            [this](const QUrl &, const KFileItemList &items) {
        QList<QUrl> urls;
        urls.reserve(items.size());
        for (const KFileItem &item : items)
            urls.append(item.url());
        Q_EMIT itemsAdded(urls);
    });
}

QAbstractItemModel *DirectoryModel::model() const
{
    return m_proxy;
}

void DirectoryModel::setUrl(const QUrl &url)
{
    if (url.isValid())
        m_dirModel->openUrl(url);
}

QUrl DirectoryModel::url() const
{
    return m_dirModel->dirLister()->url();
}

void DirectoryModel::refresh()
{
    const QUrl current = url();
    if (current.isValid())
        m_dirModel->openUrl(current, KDirModel::Reload);
}

void DirectoryModel::setShowHiddenFiles(bool show)
{
    KDirLister *lister = m_dirModel->dirLister();
    if (lister->showHiddenFiles() == show)
        return;
    lister->setShowHiddenFiles(show);
    // The lister does not re-filter what it has cached.
    lister->emitChanges();
    // The proxy's filter reads this setting too, for the Windows-mode system
    // folders. invalidate() rather than invalidateFilter(), which is protected.
    m_proxy->invalidate();
    Settings::setShowHiddenFiles(show);
}

bool DirectoryModel::showHiddenFiles() const
{
    return m_dirModel->dirLister()->showHiddenFiles();
}

QList<int> DirectoryModel::visibleColumns() const
{
    return m_proxy->columns();
}

void DirectoryModel::setVisibleColumns(const QList<int> &sourceColumns)
{
    m_proxy->setColumns(sourceColumns);
}

void DirectoryModel::setColumnVisible(int sourceColumn, bool visible)
{
    QList<int> columns = m_proxy->columns();
    if (visible && !columns.contains(sourceColumn))
        columns.append(sourceColumn);
    else if (!visible)
        columns.removeAll(sourceColumn);
    m_proxy->setColumns(columns);
}

bool DirectoryModel::isColumnVisible(int sourceColumn) const
{
    return m_proxy->columns().contains(sourceColumn);
}

int DirectoryModel::viewColumnFor(int sourceColumn) const
{
    // The proxy keeps the survivors in source order, so a column's view index
    // is how many enabled columns sort before it.
    const QList<int> columns = m_proxy->columns();
    const int at = columns.indexOf(sourceColumn);
    return at;
}

int DirectoryModel::sourceColumnFor(int viewColumn) const
{
    const QList<int> columns = m_proxy->columns();
    if (viewColumn < 0 || viewColumn >= columns.size())
        return -1;
    return columns.at(viewColumn);
}

int DirectoryModel::sortColumn() const
{
    return sourceColumnFor(m_proxy->sortColumn());
}

Qt::SortOrder DirectoryModel::sortOrder() const
{
    return m_proxy->sortOrder();
}

void DirectoryModel::sort(int sourceColumn, Qt::SortOrder order)
{
    const int viewColumn = viewColumnFor(sourceColumn);
    if (viewColumn < 0)
        return;   // sorting by a column that is not on show has nothing to click
    m_proxy->sort(viewColumn, order);
}

void DirectoryModel::refreshDisplayNames()
{
    // Not a dataChanged sweep: the names feed the sort too, so the rows have to
    // be reordered rather than just redrawn.
    m_proxy->invalidate();
}

void DirectoryModel::setNameFilter(const QString &pattern)
{
    m_proxy->setFilterFixedString(pattern);
}

KFileItem DirectoryModel::itemForIndex(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid())
        return KFileItem();
    return m_dirModel->itemForIndex(m_proxy->mapToSource(proxyIndex));
}

QList<KFileItem> DirectoryModel::itemsForIndexes(const QModelIndexList &proxyIndexes) const
{
    QList<KFileItem> items;
    items.reserve(proxyIndexes.size());
    for (const QModelIndex &index : proxyIndexes) {
        // A row selection reports one index per column.
        if (index.column() != 0)
            continue;
        const KFileItem item = itemForIndex(index);
        if (!item.isNull())
            items.append(item);
    }
    return items;
}

QModelIndex DirectoryModel::indexForUrl(const QUrl &url) const
{
    const QModelIndex sourceIndex = m_dirModel->indexForUrl(url);
    if (!sourceIndex.isValid())
        return {};
    return m_proxy->mapFromSource(sourceIndex);
}

int DirectoryModel::rowCount() const
{
    return m_proxy->rowCount();
}
