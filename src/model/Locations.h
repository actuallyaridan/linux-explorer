#pragma once

#include <QString>
#include <QUrl>
#include <QUrlQuery>

// Explorer's synthetic "Computer" location, and the search results location.
//
// KF6 ships no computer:/ protocol (KDE 4 had one; it was dropped and never
// replaced), so nothing can list the machine's drives as a directory and
// Computer is built in-process from the places model instead.
//
// The URL is a marker, not something KIO can resolve: navigating to it swaps
// the file list over to ComputerModel, and handing it to KIO fails with
// "unsupported protocol". Every path that forwards a URL to DirectoryModel or
// to a KIO job has to check isComputer() first.
namespace Locations {

inline QUrl computer()
{
    return QUrl(QStringLiteral("computer:///"));
}

inline bool isComputer(const QUrl &url)
{
    return url.scheme() == QLatin1String("computer");
}

// A recursive search of `folder` for names containing `term`.
//
// Unlike computer:/ this IS a real KIO location: kio-filenamesearch walks the
// tree in a worker process and reports hits as an ordinary directory listing,
// which is the whole reason for going through it rather than recursing here.
// The results arrive in KDirModel like any other folder, so the columns, menus,
// details pane and file operations work without knowing it is a search.
//
// `contents` is Win7's "Search again in: File Contents", done by the same
// worker opening each file it walks past. Opt-in, because without Windows'
// indexer the only way to answer is to read the files now.
inline QUrl search(const QUrl &folder, const QString &term, bool contents = false)
{
    QUrl url;
    url.setScheme(QStringLiteral("filenamesearch"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("search"), term);
    query.addQueryItem(QStringLiteral("url"), folder.url());
    if (contents)
        query.addQueryItem(QStringLiteral("checkContent"), QStringLiteral("yes"));
    // The worker echoes this back as the listing's display name.
    query.addQueryItem(QStringLiteral("title"), term);
    url.setQuery(query);
    return url;
}

// Whether results came from a content search, so the "Search again in" strip
// can show which scope is already in force.
inline bool isContentSearch(const QUrl &url)
{
    return QUrlQuery(url).queryItemValue(QStringLiteral("checkContent"))
           == QLatin1String("yes");
}

inline bool isSearch(const QUrl &url)
{
    return url.scheme() == QLatin1String("filenamesearch");
}

// The folder a search was started from, for the crumb trail and Up.
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
