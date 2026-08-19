#include "DirectoryModel.h"
#include "Branding.h"
#include "Settings.h"
#include "aero/palette.h"

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

// Windows ghosts hidden entries rather than greying them
constexpr qreal kHiddenIconOpacity = 0.45;

// Fainter still, so a cut hidden file stays distinguishable
constexpr qreal kCutIconOpacity = 0.30;

// Comfortably more than a screenful in any view mode
constexpr int kMaxFadedIcons = 512;

// What the details view starts with, the rest coming from the header menu
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

// KIO's own date column prefers a relative wording, which is useless in a
// column where the point is comparing rows, and the system locale is used
// rather than the default since the two disagree over how a date is written
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

// Win7 prints the name in black and every other column in grey, done on the
// model rather than a delegate, which would owe the whole item's painting in
// order to change one colour
class Win7ColumnProxy : public KDirSortFilterProxyModel {
public:
    explicit Win7ColumnProxy(QObject *parent = nullptr)
        : KDirSortFilterProxyModel(parent)
        , m_columns(defaultColumns())
    {
        // From the clipboard rather than our own cut, since another file
        // manager's cut has to ghost these items too
        connect(QApplication::clipboard(), &QClipboard::dataChanged,
                this, &Win7ColumnProxy::refreshCutItems);
        refreshCutItems();
    }

    QList<int> columns() const { return m_columns; }

    void setColumns(const QList<int> &sourceColumns)
    {
        QList<int> wanted = sourceColumns;
        if (!wanted.contains(DirectoryModel::Name))
            wanted.prepend(DirectoryModel::Name);
        std::sort(wanted.begin(), wanted.end());
        if (wanted == m_columns)
            return;

        // Structural, since without the surrounding pair the views go on
        // addressing columns that no longer exist
        beginResetModel();
        m_columns = wanted;
        endResetModel();
    }

    // Renumbered in source order, which the column lookup relies on
    bool filterAcceptsColumn(int sourceColumn, const QModelIndex &) const override
    {
        return m_columns.contains(sourceColumn);
    }

    // Drops the directories friendly mode hides rather than renames, and
    // showing hidden files brings them back
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

    // Sorts on the displayed name, or the rows come out alphabetical by their
    // real ones and nonsense on screen, and folders first still comes from the
    // base class, applied before it delegates here
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

        // Source numbering, which matches the view's only while every column
        // is switched on
        const QModelIndex sourceIndex = mapToSource(index);
        const int column = sourceIndex.column();
        const KFileItem item = fileItem(sourceIndex);

        if (role == Qt::ForegroundRole && column != DirectoryModel::Name)
            return QBrush(Aero::Palette::rgb(Aero::Palette::MutedText));

        // Hidden entries and entries cut to the clipboard are ghosted, and
        // only the icon fades
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

            // Blank for folders, as in Win7, since the model reports only the
            // directory's own size and the real figure needs a tree walk
            case DirectoryModel::Size:
                if (item.isDir())
                    return QString();
                break;

            // Only the displayed string changes, the item's location being
            // left untouched
            case DirectoryModel::Name:
                return displayName(item);

            default:
                break;
            }
        }

        // Win7's infotip, which is what the icon views have instead of columns
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
            // Known means the desktop can name the type, and stripping an
            // unrecognised suffix would hide the only clue to what it is
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

        // The whole row, working out which changed costing more than a repaint
        if (rowCount() > 0) {
            Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                               {Qt::DecorationRole});
        }
    }

    // The cache is a requirement rather than an optimisation, this running for
    // every visible cell on every repaint, and it is keyed on the icon's own
    // contents rather than its name, which is shared by every file of a MIME
    // type and with previews on gave every ghosted photo the same picture
    QIcon fadedIcon(const QIcon &source, qreal opacity, bool cut) const
    {
        const QString key = (cut ? QStringLiteral("cut:") : QStringLiteral("hidden:"))
                          + QString::number(source.cacheKey());
        const auto cached = m_fadedIcons.constFind(key);
        if (cached != m_fadedIcons.constEnd())
            return cached.value();

        QIcon faded;
        // The available sizes come back empty for scalable themes
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

        // A key is one icon rather than one MIME type, so a folder of
        // thumbnails would otherwise keep an entry per file, and it is dropped
        // wholesale since what is on screen is rebuilt on the next repaint
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
    // Win7 ignores case when sorting
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->sort(KDirModel::Name, Qt::AscendingOrder);

    // Name column only, the default matching sizes and dates as well
    m_proxy->setFilterKeyColumn(KDirModel::Name);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    // The model disables drops by default, and this only advertises the rows
    // as targets, the drop itself belonging to the view
    m_dirModel->setDropsAllowed(KDirModel::DropOnDirectory);

    KDirLister *lister = m_dirModel->dirLister();
    lister->setShowHiddenFiles(Settings::showHiddenFiles());
    lister->setAutoErrorHandlingEnabled(false);   // we surface errors ourselves

    connect(lister, &KCoreDirLister::started, this, &DirectoryModel::loadingStarted);
    connect(lister, &KCoreDirLister::completed, this, &DirectoryModel::loadingFinished);
    connect(lister, &KCoreDirLister::percent, this, &DirectoryModel::loadingProgress);
    connect(lister, &KCoreDirLister::jobError, this, [this](KIO::Job *job) {
        // The failing job names the directory it was reading, which is not
        // always the lister's own
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
    // The lister does not filter what it has already cached
    lister->emitChanges();
    // The proxy's filter reads this setting too
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
    // The survivors stay in source order, so a column's view index is how many
    // enabled columns sort before it
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
    // Not a repaint, the names feeding the sort, so the rows have to be
    // reordered rather than merely redrawn
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
        // A row selection reports one index per column
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
