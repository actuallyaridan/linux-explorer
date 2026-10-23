#pragma once

#include <QString>
#include <QUrl>
#include <QUrlQuery>

// The synthetic Computer and Control Panel locations, and the search results
// location
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

// The Control Panel, another marker KIO knows nothing about, with the page as
// its path, such as controlpanel:///System and Security/System, and the bare
// root being the Control Panel's home page
inline QUrl controlPanel(const QString &page = QString())
{
    QUrl url;
    url.setScheme(QStringLiteral("controlpanel"));
    url.setPath(QLatin1Char('/') + page);
    return url;
}

inline bool isControlPanel(const QUrl &url)
{
    return url.scheme() == QLatin1String("controlpanel");
}

// The page without the slashes around it, so the home page is empty
inline QString controlPanelPage(const QUrl &url)
{
    if (!isControlPanel(url))
        return {};

    QString page = url.path();
    while (page.startsWith(QLatin1Char('/')))
        page.remove(0, 1);
    while (page.endsWith(QLatin1Char('/')))
        page.chop(1);
    return page;
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
