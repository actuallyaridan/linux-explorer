#include "FileOps.h"
#include "AccessDialogs.h"
#include "FileProgressDialog.h"

#include <KIO/ApplicationLauncherJob>
#include <KIO/AskUserActionInterface>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/DropJob>
#include <KIO/EmptyTrashJob>
#include <KIO/FileUndoManager>
#include <KIO/JobTracker>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkpathJob>
#include <KIO/OpenUrlJob>
#include <KIO/Paste>
#include <KIO/PasteJob>
#include <KIO/RestoreJob>
#include <KIO/WidgetsAskUserActionHandler>
#include <KJobTrackerInterface>
#include <KMessageBox>
#include <KPropertiesDialog>
#include <KTerminalLauncherJob>
#include <KUrlMimeData>

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <functional>

namespace {

// The standard KIO delegate, so conflicts raise the desktop's usual dialogs
KJobUiDelegate *delegateFor(QWidget *window)
{
    return KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window);
}

// Reporting only, the job staying KIO's, and jobs hide their progress info so
// the desktop's global tracker does not show a second indicator
void showProgress(KJob *job, QWidget *window,
                  const QString &source, const QString &destination)
{
    // The dialog decides for itself when to appear and owns its lifetime
    new FileProgressDialog(job, source, destination, window);
}

// Win7 names the folder rather than the file in flight
QString parentPath(const QList<QUrl> &sources)
{
    if (sources.isEmpty())
        return {};
    return KIO::upUrl(sources.first()).toDisplayString(QUrl::PreferLocalFile);
}

QString translate(const char *text)
{
    return QCoreApplication::translate("FileOps", text);
}

// For a message with room for one name, falling back to the location
QString nameOf(const QUrl &url)
{
    const QString name = url.fileName();
    return name.isEmpty() ? url.toDisplayString(QUrl::PreferLocalFile) : name;
}

QString nameOf(const QList<QUrl> &urls)
{
    return urls.isEmpty() ? QString() : nameOf(urls.first());
}

// A permission refusal gets Windows' own wording and dialog and everything else
// keeps KIO's, so the automatic error box is off to stop the two both appearing
// while the delegate stays, the conflict prompts coming from it
void reportFailures(KJob *job, QWidget *window, const QString &title,
                    const QString &subject)
{
    if (KJobUiDelegate *delegate = job->uiDelegate())
        delegate->setAutoErrorHandlingEnabled(false);

    QObject *context = window ? static_cast<QObject *>(window) : job;
    QObject::connect(job, &KJob::result, context,
                     [window, title, subject](KJob *finished) {
        const int error = finished->error();
        // Cancelling is not a failure
        if (error == KJob::NoError || error == KIO::ERR_USER_CANCELED)
            return;
        if (AccessDialogs::isPermissionError(error))
            AccessDialogs::showFailure(window, title, subject);
        else
            KMessageBox::error(window, finished->errorString());
    });
}

// KIO's own delete confirmation, whose answer arrives asynchronously, so the
// work comes in as a continuation and each request gets a handler that deletes
// itself once answered, sharing one crossing two deletions in flight
void confirmThen(const QList<QUrl> &urls, QWidget *window,
                 KIO::AskUserActionInterface::DeletionType type,
                 std::function<void(const QList<QUrl> &)> perform)
{
    auto *handler = new KIO::WidgetsAskUserActionHandler;
    QObject::connect(handler, &KIO::AskUserActionInterface::askUserDeleteResult,
                     handler, [handler, perform = std::move(perform)](
                         bool allowDelete, const QList<QUrl> &confirmedUrls,
                         KIO::AskUserActionInterface::DeletionType, QWidget *) {
        if (allowDelete)
            perform(confirmedUrls);
        handler->deleteLater();
    });

    handler->askUserDelete(urls, type,
                           KIO::AskUserActionInterface::DefaultConfirmation, window);
}

// Resolved to a real path where one exists, which is what applications that
// cannot speak KIO paste
void putOnClipboard(const QList<KFileItem> &items, bool cut)
{
    if (items.isEmpty())
        return;

    QList<QUrl> urls;
    QList<QUrl> mostLocalUrls;
    urls.reserve(items.size());
    mostLocalUrls.reserve(items.size());
    for (const KFileItem &item : items) {
        urls.append(item.url());
        mostLocalUrls.append(item.mostLocalUrl());
    }

    auto *mimeData = new QMimeData;
    KUrlMimeData::setUrls(urls, mostLocalUrls, mimeData);
    KIO::setClipboardDataCut(mimeData, cut);
    QApplication::clipboard()->setMimeData(mimeData);
}

} // namespace

namespace FileOps {

void copy(const QList<QUrl> &sources, const QUrl &destination, QWidget *window)
{
    if (sources.isEmpty())
        return;
    KIO::CopyJob *job = KIO::copy(sources, destination, KIO::HideProgressInfo);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window, translate("Error Copying File or Folder"),
                   translate("Cannot copy %1.").arg(nameOf(sources)));
    KIO::FileUndoManager::self()->recordCopyJob(job);
    showProgress(job, window, parentPath(sources),
                 destination.toDisplayString(QUrl::PreferLocalFile));
}

void move(const QList<QUrl> &sources, const QUrl &destination, QWidget *window)
{
    if (sources.isEmpty())
        return;
    KIO::CopyJob *job = KIO::move(sources, destination, KIO::HideProgressInfo);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window, translate("Error Moving File or Folder"),
                   translate("Cannot move %1.").arg(nameOf(sources)));
    KIO::FileUndoManager::self()->recordCopyJob(job);
    showProgress(job, window, parentPath(sources),
                 destination.toDisplayString(QUrl::PreferLocalFile));
}

void moveToTrash(const QList<QUrl> &urls, QWidget *window)
{
    if (urls.isEmpty())
        return;

    confirmThen(urls, window, KIO::AskUserActionInterface::Trash,
                [window](const QList<QUrl> &confirmed) {
        KIO::CopyJob *job = KIO::trash(confirmed, KIO::HideProgressInfo);
        job->setUiDelegate(delegateFor(window));
        reportFailures(job, window, translate("Error Deleting File or Folder"),
                       translate("Cannot delete %1.").arg(nameOf(confirmed)));
        showProgress(job, window, parentPath(confirmed), QString());
        // Not recorded as a copy, or undo moves the files back from wherever
        // the trash put them instead of going through its own restore
        KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, confirmed,
                                                QUrl(QStringLiteral("trash:/")), job);
    });
}

void deletePermanently(const QList<QUrl> &urls, QWidget *window)
{
    if (urls.isEmpty())
        return;

    confirmThen(urls, window, KIO::AskUserActionInterface::Delete,
                [window](const QList<QUrl> &confirmed) {
        // An undo entry that cannot restore the files is worse than none
        KIO::DeleteJob *job = KIO::del(confirmed, KIO::HideProgressInfo);
        job->setUiDelegate(delegateFor(window));
        reportFailures(job, window, translate("Error Deleting File or Folder"),
                       translate("Cannot delete %1.").arg(nameOf(confirmed)));
        showProgress(job, window, parentPath(confirmed), QString());
    });
}

void emptyTrash(QWidget *window)
{
    // KIO's own prompt, which takes no locations
    confirmThen({}, window, KIO::AskUserActionInterface::EmptyTrash,
                [window](const QList<QUrl> &) {
        KIO::EmptyTrashJob *job = KIO::emptyTrash();

        // This job cannot be told to hide its progress, so it registers with
        // the desktop's tracker and unregistering leaves ours the only dialog
        KIO::getJobTracker()->unregisterJob(job);

        job->setUiDelegate(delegateFor(window));
        reportFailures(job, window, translate("Error Deleting File or Folder"),
                       translate("The Recycle Bin could not be emptied."));
        showProgress(job, window, QString(), QString());
    });
}

void rename(const QUrl &url, const QString &newName, QWidget *window)
{
    if (newName.isEmpty() || newName == url.fileName())
        return;

    QUrl target = url.adjusted(QUrl::RemoveFilename);
    target.setPath(target.path() + newName);

    KIO::CopyJob *job = KIO::moveAs(url, target);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window, translate("Error Renaming File or Folder"),
                   translate("Cannot rename %1.").arg(nameOf(url)));
    KIO::FileUndoManager::self()->recordCopyJob(job);
}

void renameBatch(const QList<KFileItem> &items, const QString &baseName,
                 QWidget *window)
{
    if (items.isEmpty() || baseName.isEmpty())
        return;

    if (items.size() == 1) {
        rename(items.first().url(), baseName, window);
        return;
    }

    // The editor is prefilled with one file's real name, so an unchanged
    // commit still carries its extension and every file would end up with two
    QString stem = baseName;
    const QString firstName = items.constFirst().name();
    const int firstDot = firstName.lastIndexOf(QLatin1Char('.'));
    if (firstDot > 0) {
        const QString firstSuffix = firstName.mid(firstDot);
        if (stem.size() > firstSuffix.size() && stem.endsWith(firstSuffix))
            stem.chop(firstSuffix.size());
    }

    // Windows numbers from one, before the extension
    int counter = 1;
    for (const KFileItem &item : items) {
        const QString name = item.name();
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        const QString suffix = (dot > 0 && !item.isDir()) ? name.mid(dot) : QString();
        rename(item.url(), QStringLiteral("%1 (%2)%3")
                               .arg(stem)
                               .arg(counter++)
                               .arg(suffix),
               window);
    }
}

void extractArchive(const QUrl &archiveUrl, const QUrl &destination,
                    QWidget *window)
{
    if (!archiveUrl.isValid() || !destination.isValid())
        return;

    // Copied as the chosen folder, or the contents land in a real directory
    // named after the archive instead
    KIO::CopyJob *job = KIO::copyAs(archiveUrl, destination, KIO::HideProgressInfo);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window,
                   translate("Extract Compressed (Zipped) Folders"),
                   translate("Cannot extract %1.").arg(nameOf(archiveUrl)));
    KIO::FileUndoManager::self()->recordCopyJob(job);
    showProgress(job, window, archiveUrl.toDisplayString(QUrl::PreferLocalFile),
                 destination.toDisplayString(QUrl::PreferLocalFile));
}

bool canCompress()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ark")).isEmpty();
}

bool compress(const QList<QUrl> &sources, QWidget *window)
{
    Q_UNUSED(window)
    if (sources.isEmpty() || !canCompress())
        return false;

    // The one operation not going through KIO, whose archive workers are read
    // only, and Ark is asked for its dialogless mode, which names the archive
    // after its contents the way Win7 does
    QStringList arguments{QStringLiteral("--batch"),
                          QStringLiteral("--autofilename"),
                          QStringLiteral("zip")};
    for (const QUrl &url : sources)
        arguments << url.toLocalFile();

    return QProcess::startDetached(QStringLiteral("ark"), arguments,
                                   KIO::upUrl(sources.first()).toLocalFile());
}

void createFolder(const QUrl &parentDir, const QString &name, QWidget *window)
{
    if (name.isEmpty())
        return;

    QUrl target = parentDir;
    target.setPath(parentDir.path() + QLatin1Char('/') + name);

    KIO::MkpathJob *job = KIO::mkpath(target, parentDir);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window, translate("Unable to create folder"),
                   translate("Cannot create the folder %1.").arg(name));
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkpath, {},
                                            target, job);
}

void createLink(const QList<QUrl> &sources, const QUrl &destination, QWidget *window)
{
    if (sources.isEmpty())
        return;
    KIO::CopyJob *job = KIO::link(sources, destination, KIO::HideProgressInfo);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window, translate("Error Creating Shortcut"),
                   translate("Cannot create a shortcut to %1.")
                       .arg(nameOf(sources)));
    KIO::FileUndoManager::self()->recordCopyJob(job);
}

void restoreFromTrash(const QList<QUrl> &urls, QWidget *window)
{
    if (urls.isEmpty())
        return;

    // No confirmation, restoring destroying nothing and Windows not asking
    KIO::RestoreJob *job = KIO::restoreFromTrash(urls, KIO::HideProgressInfo);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window, translate("Error Restoring File or Folder"),
                   translate("Cannot restore %1.").arg(nameOf(urls)));
    showProgress(job, window, QStringLiteral("trash:/"), QString());
}

void copyToClipboard(const QList<KFileItem> &items)
{
    putOnClipboard(items, false);
}

void cutToClipboard(const QList<KFileItem> &items)
{
    putOnClipboard(items, true);
}

bool canPaste()
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    return mimeData && KIO::canPasteMimeData(mimeData);
}

void pasteFromClipboard(const QUrl &destination, QWidget *window)
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData || !KIO::canPasteMimeData(mimeData))
        return;

    // Not a paste job, which transfers in a subjob and reports none of its
    // progress, leaving the dialog blank, where a copy job reports it all
    const QList<QUrl> urls = KUrlMimeData::urlsFromMimeData(mimeData);
    if (!urls.isEmpty()) {
        if (KIO::isClipboardDataCut(mimeData))
            move(urls, destination, window);
        else
            copy(urls, destination, window);
        return;
    }

    // Anything that is not a file is pasted into a new one, and those finish
    // at once so the missing progress costs nothing
    KIO::PasteJob *job = KIO::paste(mimeData, destination, KIO::HideProgressInfo);
    job->setUiDelegate(delegateFor(window));
    reportFailures(job, window, translate("Error Copying File or Folder"),
                   translate("Cannot paste into %1.").arg(nameOf(destination)));
}

void dropOn(QDropEvent *event, const QUrl &destination, QWidget *window)
{
    if (!event || !destination.isValid())
        return;

    KIO::DropJob *job = KIO::drop(event, destination, KIO::HideProgressInfo);
    job->setUiDelegate(delegateFor(window));
    // Which of copy, move and link this became is not known yet, so the
    // message names where the files were going
    reportFailures(job, window, translate("Error Copying File or Folder"),
                   translate("Cannot copy to %1.").arg(nameOf(destination)));

    // A drop reports no progress of its own, and waiting for the copy job it
    // starts is what lets the dialog show a real percentage
    QObject::connect(job, &KIO::DropJob::copyJobStarted, window,
                     [window](KIO::CopyJob *copyJob) {
        showProgress(copyJob, window,
                     parentPath(copyJob->srcUrls()),
                     copyJob->destUrl().toDisplayString(QUrl::PreferLocalFile));
    });
}

void copyPathToClipboard(const QList<KFileItem> &items)
{
    if (items.isEmpty())
        return;

    QStringList paths;
    paths.reserve(items.size());
    for (const KFileItem &item : items) {
        // A local file yields a plain path, and remote locations keep their
        // full form, having no path to prefer
        paths.append(item.url().toDisplayString(QUrl::PreferLocalFile));
    }
    QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
}

void openItem(const KFileItem &item, QWidget *window)
{
    if (item.isNull())
        return;

    auto *job = new KIO::OpenUrlJob(item.url(), item.mimetype());
    job->setUiDelegate(delegateFor(window));
    job->setShowOpenOrExecuteDialog(true);   // the run or display prompt
    job->start();
}

void openWith(const QList<KFileItem> &items, QWidget *window)
{
    if (items.isEmpty())
        return;

    QList<QUrl> urls;
    urls.reserve(items.size());
    for (const KFileItem &item : items)
        urls.append(item.url());

    // With no application set the desktop shows its own open with dialog
    auto *job = new KIO::ApplicationLauncherJob();
    job->setUrls(urls);
    job->setUiDelegate(delegateFor(window));
    job->start();
}

void openTerminalAt(const QUrl &directory, QWidget *window)
{
    if (!directory.isLocalFile())
        return;   // there is no working directory to hand a shell for a remote URL

    auto *job = new KTerminalLauncherJob(QString());
    job->setWorkingDirectory(directory.toLocalFile());
    job->setUiDelegate(delegateFor(window));
    job->start();
}

void showProperties(const QList<KFileItem> &items, QWidget *window)
{
    if (items.isEmpty())
        return;

    auto *dialog = new KPropertiesDialog(KFileItemList(items), window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void undo()
{
    KIO::FileUndoManager::self()->undo();
}

bool isUndoAvailable()
{
    return KIO::FileUndoManager::self()->isUndoAvailable();
}

} // namespace FileOps
