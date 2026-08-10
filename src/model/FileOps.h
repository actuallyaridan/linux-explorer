#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <KFileItem>

class QDropEvent;
class QWidget;

// Every destructive or data-moving operation Explorer offers, each a thin call
// into KIO.
//
// The dangerous parts of moving a user's files — overwrite and conflict
// resolution, symlinks and permissions, cross-device copies, trash semantics
// the desktop's Recycle Bin can restore from, cancellable progress, undo — are
// all solved inside KIO and shared with Dolphin. These functions only marshal
// arguments, attach the standard UI delegate so the native conflict dialogs
// appear, and register the job with KIO's undo manager.
//
// The rule to keep: anything that would read or write file contents directly
// belongs in KIO instead.
namespace FileOps {

// `window` parents KIO's progress and conflict dialogs; without it the
// overwrite prompt can appear behind the window.
void copy(const QList<QUrl> &sources, const QUrl &destination, QWidget *window);
void move(const QList<QUrl> &sources, const QUrl &destination, QWidget *window);

// Both prompt through KIO's own confirmation dialog, so the wording and the
// "do not ask again" handling match the rest of the desktop.
void moveToTrash(const QList<QUrl> &urls, QWidget *window);
void deletePermanently(const QList<QUrl> &urls, QWidget *window);

// Empties the trash, after KIO's own confirmation prompt.
void emptyTrash(QWidget *window);

void rename(const QUrl &url, const QString &newName, QWidget *window);

// Win7's rename of a multiple selection: one base name plus a counter, each
// file keeping its own extension. One KIO job per file, so each lands in the
// undo history separately and a collision fails without taking the batch down.
void renameBatch(const QList<KFileItem> &items, const QString &baseName,
                 QWidget *window);
void createFolder(const QUrl &parentDir, const QString &name, QWidget *window);

// Win7's "Create shortcut". A symlink rather than a .desktop file, that being
// what the rest of the system follows.
void createLink(const QList<QUrl> &sources, const QUrl &destination, QWidget *window);

// The trash's own restore rather than a move out of the trash directory, so it
// recreates the original path even if the folder is gone.
void restoreFromTrash(const QList<QUrl> &urls, QWidget *window);

void copyToClipboard(const QList<KFileItem> &items);
void cutToClipboard(const QList<KFileItem> &items);
bool canPaste();
void pasteFromClipboard(const QUrl &destination, QWidget *window);

// Performs a drop onto `destination`. `event` must still be live: KIO reads the
// modifier keys off it to tell a copy from a move from a link, and offers the
// menu when the user has not said.
void dropOn(QDropEvent *event, const QUrl &destination, QWidget *window);

// The selection's paths as plain text, one per line. Unlike copyToClipboard,
// which puts the files themselves on the clipboard.
void copyPathToClipboard(const QList<KFileItem> &items);

// Win7's "Extract All". A plain KIO copy with an unusual source URL: the
// archive is read through KIO's read-only archive worker, so nothing here
// knows how a zip is laid out. `destination` is created as a directory.
void extractArchive(const QUrl &archiveUrl, const QUrl &destination,
                    QWidget *window);

// Win7's "Send to > Compressed (zipped) folder". KIO's archive workers are
// read-only, so this one goes to Ark instead. Returns false when Ark is not
// installed, the only way it can fail synchronously.
bool compress(const QList<QUrl> &sources, QWidget *window);
bool canCompress();

// The desktop's normal open-with machinery, including the executable and
// untrusted-script prompts.
void openItem(const KFileItem &item, QWidget *window);

// The "Open with..." dialog, for picking an application explicitly.
void openWith(const QList<KFileItem> &items, QWidget *window);

// Opens the user's terminal in `directory`, as chosen in the desktop's own
// setting.
void openTerminalAt(const QUrl &directory, QWidget *window);

void showProperties(const QList<KFileItem> &items, QWidget *window);

void undo();
bool isUndoAvailable();

} // namespace FileOps
