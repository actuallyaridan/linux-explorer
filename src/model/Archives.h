#pragma once

#include <QString>
#include <QUrl>

#include <KFileItem>

// Walking into an archive as if it were a folder
//
// Nothing here unpacks anything, KIO shipping read only workers that present an
// archive as an ordinary directory listing, so the model handles it like any
// other folder
namespace Archives {

// Empty when the item is not a browsable archive or the worker is missing
QString protocolFor(const KFileItem &item);

inline bool isBrowsable(const KFileItem &item)
{
    return !protocolFor(item).isEmpty();
}

QUrl urlFor(const KFileItem &item);

// The workers are read only, so New, Paste and Rename have nothing to act on
bool isInsideArchive(const QUrl &url);

// The archive file itself, so the crumb trail and Up can lead back out
QUrl archiveFileFor(const QUrl &url);

} // namespace Archives
