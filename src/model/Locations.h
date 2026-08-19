#pragma once

#include <QString>
#include <QUrl>
#include <QUrlQuery>

// The synthetic Computer location and the search results location
//
// KIO has no protocol that lists a machine's drives, so the Computer location
// is a marker rather than something it can resolve, and navigating to it swaps
// the file list over to the drive model instead, which means every path
// forwarding a location to a job has to check for it first
namespace Locations {

inline QUrl computer()
{
    return QUrl(QStringLiteral("computer:///"));
}

inline bool isComputer(const QUrl &url)
{
    return url.scheme() == QLatin1String("computer");
}

// A recursive search of a folder, and unlike Computer this is a real KIO
// location, a worker walking the tree and reporting hits as an ordinary listing
//
// Searching contents is opt in, since without an indexer it reads every file it
// walks past
inline QUrl search(const QUrl &folder, const QString &term, bool contents = false)
{
    QUrl url;
    url.setScheme(QStringLiteral("filenamesearch"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("search"), term);
    query.addQueryItem(QStringLiteral("url"), folder.url());
    if (contents)
        query.addQueryItem(QStringLiteral("checkContent"), QStringLiteral("yes"));
    // Echoed back as the listing's display name
    query.addQueryItem(QStringLiteral("title"), term);
    url.setQuery(query);
    return url;
}

// So the search again strip can show which scope is in force
inline bool isContentSearch(const QUrl &url)
{
    return QUrlQuery(url).queryItemValue(QStringLiteral("checkContent"))
           == QLatin1String("yes");
}

inline bool isSearch(const QUrl &url)
{
    return url.scheme() == QLatin1String("filenamesearch");
}

inline QUrl searchFolder(const QUrl &url)
{
    if (!isSearch(url))
        return {};
    return QUrl(QUrlQuery(url).queryItemValue(QStringLiteral("url"),
                                              QUrl::FullyDecoded));
}

inline QString searchTerm(const QUrl &url)
{
    if (!isSearch(url))
        return {};
    return QUrlQuery(url).queryItemValue(QStringLiteral("search"),
                                         QUrl::FullyDecoded);
}

} // namespace Locations
