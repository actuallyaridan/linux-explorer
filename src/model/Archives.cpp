#include "Archives.h"

#include <KProtocolInfo>

#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QMimeType>

namespace Archives {

namespace {

// MIME type to the protocol that can list it, limited to the families KIO
// ships a worker for, and the zip containers that are documents to a user are
// deliberately absent
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

        // Registered by the same worker
        {QStringLiteral("application/x-7z-compressed"),            QStringLiteral("sevenz")},
    };
    return map;
}

} // namespace

QString protocolFor(const KFileItem &item)
{
    if (item.isNull() || item.isDir())
        return {};

    // Only a real local file, anything remote having to be downloaded first
    const QUrl url = item.targetUrl();
    if (!url.isLocalFile())
        return {};

    const QString protocol = protocolByMimeType().value(item.mimetype());
    if (protocol.isEmpty())
        return {};

    if (!KProtocolInfo::isKnownProtocol(protocol))
        return {};

    return protocol;
}

QUrl urlFor(const KFileItem &item)
{
    const QString protocol = protocolFor(item);
    if (protocol.isEmpty())
        return {};

    // The workers take the archive's own path and append to it, so the
    // conversion is a scheme swap
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

    // Nothing marks the boundary between the archive and the path inside it,
    // so the archive is the longest leading portion that is a file on disk
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
