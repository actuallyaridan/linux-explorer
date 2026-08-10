#pragma once

#include <QString>
#include <QUrl>

#include <KFileItem>

// Walking into an archive as if it were a folder, as Windows has done with .zip
// since XP.
//
// Nothing here unpacks anything: kio-extras ships read-only workers for the zip
// and tar families that present an archive as an ordinary directory listing.
// Navigating to the URL below hands it to KDirModel like any other folder,
// which is why the columns, the details pane, dragging out and Extract All (a
// plain KIO copy off that URL) all work without this file knowing how a zip is
// laid out.
//
// Those workers are read-only, so dragging files *into* a zip folder is the one
// part of Win7's behaviour not reproduced. Creating archives goes through Ark;
// see FileOps::compress.
namespace Archives {

// The KIO protocol that can list `item` as a directory, or an empty string when
// it is not a browsable archive. Also empty when the worker is not installed,
// so callers need no separate check.
QString protocolFor(const KFileItem &item);

inline bool isBrowsable(const KFileItem &item)
{
    return !protocolFor(item).isEmpty();
}

// `item` as a location that can be navigated to, or an invalid URL if it is
// not a browsable archive.
QUrl urlFor(const KFileItem &item);

// True while `url` points inside an archive rather than at a real directory.
// The workers are read-only, so New, Paste and Rename have nothing to act on.
bool isInsideArchive(const QUrl &url);

// The archive file an archive URL is a view of, as a local file URL, so the
// crumb trail and Up can lead back out to the folder it lives in.
QUrl archiveFileFor(const QUrl &url);

} // namespace Archives
