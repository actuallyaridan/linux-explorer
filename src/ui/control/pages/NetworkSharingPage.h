#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "ui/control/PageId.h"

class QScrollArea;
class QVBoxLayout;

namespace ControlPanel {

// The "Network and Sharing Center" detail page, a Linux-flavoured take on the
// Windows 7 "View your basic network information and set up connections"
// screen. The network map, active-network panel, and connection name are
// populated from the live machine (/proc/net/route, /sys/class/net, QHostInfo)
class NetworkSharingPage : public QWidget {
    Q_OBJECT

public:
    explicit NetworkSharingPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

private:
    // Live network facts, collected once in the constructor
    struct NetInfo {
        // Host name, upper-cased (e.g. "CACHYOS-X8664")
        QString computerName;
        // Active network profile name (e.g. "Network")
        QString networkName;
        // "Internet" / "No network access"
        QString accessType;
        // "Local Area Connection" / "Wireless Network Connection"
        QString connectionName;
        // Default route is over a wireless interface
        bool    wireless = false;
        // A default route exists
        bool    connected = false;
    };

    static NetInfo gatherInfo();

    // Builds one node of the top network map: a large icon with caption(s)
    // centred beneath it
    QWidget *buildMapNode(const QStringList &iconNames,
                          const QString &caption,
                          const QString &subCaption = QString());

    // Builds a thin connector laid out so it lines up with the map-node icons
    QWidget *buildMapLink(bool active);

    // Adds one "Change your networking settings" task: icon + blue title link
    // with a grey description line beneath
    void addTask(QVBoxLayout *into, const QStringList &iconNames,
                 const QString &title, const QString &description);
};

} // namespace ControlPanel
