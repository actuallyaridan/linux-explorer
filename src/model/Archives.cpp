#include "Archives.h"

#include <KProtocolInfo>

#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QMimeType>

namespace Archives {

namespace {

// MIME type to KIO protocol, limited to the families kio-extras ships a worker
// for: offering to open a .7z as a folder and then failing is worse than
// leaving it to the desktop's handler.
//
// Deliberately absent: the formats that are zip containers but not archives to
// a user. A .odt, .jar, .docx and .epub are all zip files, and turning a
// double-click on a document into a folder listing would be a bug.
const QHash<QString, QString> &protocolByMimeType()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("application/zip"),                        QStringLiteral("zip")},
        {QStringLiteral("application/x-zip-compressed"),           QStringLiteral("zip")},

        {QStringLiteral("application/x-tar"),                      QStringLiteral("tar")},
        {QStringLiteral("application/x-gtar"),                     QStringLiteral("tar")},
        {QStringLiteral("application/x-compressed-tar"),           QStringLiteral("tar")},
        {QStringLiteral("application/x-bzip-compressed-tar"),      QStringLiteral("tar")},
        {QStringLiteral("application/x-bzip2-compressed-tar"),     QStringLiteral("tar")},
        {QStringLiteral("application/x-xz-compressed-tar"),        QStringLiteral("tar")},
        {QStringLiteral("application/x-lzma-compressed-tar"),      QStringLiteral("tar")},
        {QStringLiteral("application/x-lz4-compressed-tar"),       QStringLiteral("tar")},
        {QStringLiteral("application/x-zstd-compressed-tar"),      QStringLiteral("tar")},
        {QStringLiteral("application/x-tarz"),                     QStringLiteral("tar")},

        {QStringLiteral("application/x-archive"),                  QStringLiteral("ar")},

        // Registered by the same worker, and protocolFor() checks it is
        // installed before handing back a scheme.
        {QStringLiteral("application/x-7z-compressed"),            QStringLiteral("sevenz")},
    };
    return map;
}

} // namespace

QString protocolFor(const KFileItem &item)
{
    if (item.isNull() || item.isDir())
        return {};

    // Browsed off the filesystem, so only a real local file qualifies: one on
    // an smb:// share, or inside another archive, would have to be downloaded
    // first and is left alone.
    const QUrl url = item.targetUrl();
    if (!url.isLocalFile())
        return {};

    const QString protocol = protocolByMimeType().value(item.mimetype());
    if (protocol.isEmpty())
        return {};

    // A separate package from KIO itself.
    if (!KProtocolInfo::isKnownProtocol(protocol))
        return {};

    return protocol;
}

QUrl urlFor(const KFileItem &item)
{
    const QString protocol = protocolFor(item);
    if (protocol.isEmpty())
        return {};

    // The workers take the archive file's own path and walk deeper by appending
    // to it, so the conversion is a scheme swap.
    QUrl url = item.targetUrl();
    url.setScheme(protocol);
    return url;
}

bool isInsideArchive(const QUrl &url)
{
    const QString scheme = url.scheme();
    return scheme == QLatin1String("zip") || scheme == QLatin1String("tar")
        || scheme == QLatin1String("ar") || scheme == QLatin1String("sevenz");
}

QUrl archiveFileFor(const QUrl &url)
{
    if (!isInsideArchive(url))
        return {};

    // The path is the archive file followed by the path inside it, with nothing
    // marking the boundary. The archive is the longest leading portion that is
    // a file on disk, which is how the workers resolve it too.
    QString path = url.path();
    while (!path.isEmpty() && path != QLatin1String("/")) {
        const QFileInfo info(path);
        if (info.isFile())
            return QUrl::fromLocalFile(path);
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0)
            break;
        path.truncate(slash);
    }
    return {};
}

} // namespace Archives
