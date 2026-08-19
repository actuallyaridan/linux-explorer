#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <KFileItem>

class QDropEvent;
class QWidget;

// Every destructive or data moving operation, each a thin call into KIO
//
// These only marshal arguments, attach the standard delegate and register the
// job with the undo manager, so anything that would read or write file contents
// directly belongs in KIO instead
namespace FileOps {

// The window parents KIO's progress and conflict dialogs
void copy(const QList<QUrl> &sources, const QUrl &destination, QWidget *window);
void move(const QList<QUrl> &sources, const QUrl &destination, QWidget *window);

// Both prompt through KIO's own confirmation dialog
void moveToTrash(const QList<QUrl> &urls, QWidget *window);
void deletePermanently(const QList<QUrl> &urls, QWidget *window);

void emptyTrash(QWidget *window);

void rename(const QUrl &url, const QString &newName, QWidget *window);

// Win7's multiple rename, one base name and a counter, run as a job per file
// so a collision fails without taking the batch down
void renameBatch(const QList<KFileItem> &items, const QString &baseName,
                 QWidget *window);
void createFolder(const QUrl &parentDir, const QString &name, QWidget *window);

// Win7's create shortcut, as a symlink rather than a desktop entry
void createLink(const QList<QUrl> &sources, const QUrl &destination, QWidget *window);

// The trash's own restore, so it recreates the original path
void restoreFromTrash(const QList<QUrl> &urls, QWidget *window);

void copyToClipboard(const QList<KFileItem> &items);
void cutToClipboard(const QList<KFileItem> &items);
bool canPaste();
void pasteFromClipboard(const QUrl &destination, QWidget *window);

// The event must still be live, KIO reading the modifier keys off it to tell a
// copy from a move from a link
void dropOn(QDropEvent *event, const QUrl &destination, QWidget *window);

// Paths as plain text, unlike copyToClipboard which copies the files
void copyPathToClipboard(const QList<KFileItem> &items);

// Win7's extract all, a plain copy off the archive worker's location
void extractArchive(const QUrl &archiveUrl, const QUrl &destination,
                    QWidget *window);

// Win7's send to compressed folder, which goes to Ark since KIO's archive
// workers are read only, and false when Ark is not installed
bool compress(const QList<QUrl> &sources, QWidget *window);
bool canCompress();

// The desktop's normal open machinery, including the executable prompts
void openItem(const KFileItem &item, QWidget *window);

void openWith(const QList<KFileItem> &items, QWidget *window);

void openTerminalAt(const QUrl &directory, QWidget *window);

void showProperties(const QList<KFileItem> &items, QWidget *window);

void undo();
bool isUndoAvailable();

} // namespace FileOps
