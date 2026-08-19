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

// Explorer's own name, and whoever holds it is the primary instance
const QLatin1String kAppService("io.github.actuallyaridan.LinuxExplorer");

// The shared name and its specified object path, the object being exported
// once so both names route to it
const QLatin1String kSharedService("org.freedesktop.FileManager1");
const QLatin1String kObjectPath("/org/freedesktop/FileManager1");
const QLatin1String kInterface("org.freedesktop.FileManager1");

// Anything unparseable is dropped rather than guessed at
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
    // The Wayland activation token first, then the X11 startup notification id
    const QByteArray wayland = qgetenv("XDG_ACTIVATION_TOKEN");
    if (!wayland.isEmpty())
        return QString::fromLocal8Bit(wayland);
    return QString::fromLocal8Bit(qgetenv("DESKTOP_STARTUP_ID"));
}

bool FileManagerService::claim()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return true;   // no session bus, so run standalone rather than refuse to start

    // The private name decides primacy, so it is claimed before anything is
    // exported, and there is no queuing since a launch that waits is a hang
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

    // Replacement allowed, so another file manager can take it over without a
    // restart, and failure here is ordinary
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

    // Blocking, since this process is about to exit and returning early would
    // race the window against our own teardown
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
    // Each file is revealed in its parent and selected, and several in one
    // folder become one window with a multiple selection
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
    // No parent, this process possibly having no window open
    FileOps::showProperties(items, nullptr);
}
