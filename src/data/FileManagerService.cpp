#include "FileManagerService.h"
#include "FileOps.h"
#include "MainWindow.h"

#include <KIO/Global>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QList>
#include <QMap>
#include <QUrl>

namespace {

// Explorer's own name; whoever holds it is the primary instance.
const QLatin1String kAppService("io.github.actuallyaridan.LinuxExplorer");

// The shared name, and the object path the specification fixes for it. The
// object is exported once on the connection, so both names route to it.
const QLatin1String kSharedService("org.freedesktop.FileManager1");
const QLatin1String kObjectPath("/org/freedesktop/FileManager1");
const QLatin1String kInterface("org.freedesktop.FileManager1");

// The URIs a caller handed us, as real URLs. Anything unparseable is dropped
// rather than guessed at: these come from other applications, and opening the
// wrong folder is worse than opening nothing.
QList<QUrl> toUrls(const QStringList &uriList)
{
    QList<QUrl> urls;
    urls.reserve(uriList.size());
    for (const QString &uri : uriList) {
        const QUrl url(uri, QUrl::StrictMode);
        if (url.isValid() && !url.scheme().isEmpty())
            urls.append(url);
    }
    return urls;
}

} // namespace

FileManagerService::FileManagerService(QObject *parent)
    : QObject(parent)
{
}

QString FileManagerService::startupId()
{
    // Wayland's xdg-activation token first, then X11's startup notification id.
    const QByteArray wayland = qgetenv("XDG_ACTIVATION_TOKEN");
    if (!wayland.isEmpty())
        return QString::fromLocal8Bit(wayland);
    return QString::fromLocal8Bit(qgetenv("DESKTOP_STARTUP_ID"));
}

bool FileManagerService::claim()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return true;   // no session bus: run standalone rather than refusing to start

    // The private name decides primacy, so it is claimed before anything is
    // exported. No queuing: a launch waiting for the running instance to exit
    // is a hang, not a launch.
    QDBusConnectionInterface *iface = bus.interface();
    if (!iface)
        return true;
    const auto reply = iface->registerService(
        kAppService, QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid()
        || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
        return false;
    }

    bus.registerObject(kObjectPath, this, QDBusConnection::ExportAllSlots);

    // Replacement allowed, so a file manager the user later prefers can take it
    // over without a restart. Failure is ordinary: something else already holds
    // it.
    const auto shared = iface->registerService(
        kSharedService, QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::AllowReplacement);
    m_ownsSharedName = shared.isValid()
        && shared.value() == QDBusConnectionInterface::ServiceRegistered;

    return true;
}

bool FileManagerService::forward(const QStringList &uris, bool reveal)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusMessage call = QDBusMessage::createMethodCall(
        kAppService, kObjectPath, kInterface,
        reveal ? QStringLiteral("ShowItems") : QStringLiteral("ShowFolders"));
    call.setArguments({uris, startupId()});

    // Blocking deliberately: this process is about to exit, and returning early
    // would race the window against our own teardown.
    const QDBusMessage reply = bus.call(call);
    return reply.type() != QDBusMessage::ErrorMessage;
}

void FileManagerService::ShowFolders(const QStringList &uriList,
                                     const QString &startupId)
{
    const QList<QUrl> urls = toUrls(uriList);
    if (urls.isEmpty()) {
        MainWindow::openWindow(QUrl(), {}, startupId);
        return;
    }
    for (const QUrl &url : urls)
        MainWindow::openWindow(url, {}, startupId);
}

void FileManagerService::ShowItems(const QStringList &uriList,
                                   const QString &startupId)
{
    // Files, not folders: each is revealed in its parent with itself selected.
    // Several files in one folder become one window with a multiple selection,
    // which is what a caller passing a whole download batch means.
    QMap<QUrl, QList<QUrl>> byParent;
    const QList<QUrl> urls = toUrls(uriList);
    for (const QUrl &url : urls)
        byParent[KIO::upUrl(url)].append(url);

    for (auto it = byParent.cbegin(); it != byParent.cend(); ++it)
        MainWindow::openWindow(it.key(), it.value(), startupId);
}

void FileManagerService::ShowItemProperties(const QStringList &uriList,
                                            const QString &startupId)
{
    Q_UNUSED(startupId)
    const QList<QUrl> urls = toUrls(uriList);
    if (urls.isEmpty())
        return;

    KFileItemList items;
    for (const QUrl &url : urls)
        items.append(KFileItem(url));
    // No parent: the caller asked for a properties dialog, not an Explorer
    // window to host it, and this process may have none open.
    FileOps::showProperties(items, nullptr);
}
