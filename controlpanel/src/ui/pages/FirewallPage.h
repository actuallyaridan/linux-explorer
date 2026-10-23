#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "PageId.h"

class QScrollArea;
class QVBoxLayout;

// The "Linux Firewall" detail page, a Linux-flavoured take on the Windows 7
// "Help protect your computer with Windows Firewall" screen.
//
// KDE's System Settings "Firewall" module (plasma-firewall) drives ufw on this
// machine, so this page mirrors exactly what that module shows by reading the
// same world-readable ufw configuration it does:
//   * /etc/ufw/ufw.conf      -> ENABLED (firewall on/off), LOGLEVEL (notifications)
//   * /etc/default/ufw       -> DEFAULT_INPUT_POLICY / DEFAULT_OUTPUT_POLICY
//   * /etc/ufw/user.rules     -> the count of configured allow/deny rules
// It only reads this state; it never changes it. Toggling the firewall in KDE
// settings rewrites those files, so reopening this page reflects the change.
class FirewallPage : public QWidget {
    Q_OBJECT

public:
    explicit FirewallPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar.
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

private:
    // Live firewall facts, read once in the constructor from the ufw config
    // files that KDE's firewall module also consults.
    struct FwInfo {
        bool    enabled = false;       // ufw.conf ENABLED=yes
        QString inputPolicy;           // DEFAULT_INPUT_POLICY ("DROP"/"ACCEPT"/"REJECT")
        QString outputPolicy;          // DEFAULT_OUTPUT_POLICY
        QString logLevel;              // ufw.conf LOGLEVEL ("off".."high")
        int     ruleCount = 0;         // number of user.rules entries
        bool    netConnected = false;  // a default route exists
        QString networkName;           // active network profile name
    };

    static FwInfo gatherInfo();

    // Builds one collapsible network-location panel (the green-striped boxes in
    // the reference). `expanded` panels show the firewall status grid beneath
    // the header; collapsed ones show only the header strip.
    QWidget *buildLocationPanel(const QString &title,
                                const QString &connState,
                                bool expanded,
                                const FwInfo &info);
};
