#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "PageId.h"

class QScrollArea;
class QVBoxLayout;

// The "Action Center" detail page, a Linux-flavoured take on the Windows 7
// "Review recent messages and resolve problems" screen.
//
// Like the Windows original it groups status into two collapsible sections,
// Security and Maintenance, each a stack of "item / state / description" rows
// with the state ("On"/"Off"/"OK"/…) right-aligned. The rows that map to a real
// Linux facility are filled from live state read the same cheap, root-free way
// the other pages use (ufw config for the firewall, an antivirus binary probe,
// the polkit authority for UAC); the remaining rows mirror Windows' wording with
// plausible fixed values, as there is no Linux equivalent to query.
class ActionCenterPage : public QWidget {
    Q_OBJECT

public:
    explicit ActionCenterPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar.
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

private:
    // Live facts backing the Security rows, gathered once in the constructor.
    struct AcInfo {
        bool    firewallOn = false;   // ufw.conf ENABLED=yes
        bool    avPresent  = false;   // an on-demand scanner (ClamAV) is installed
        QString avName;               // display name of that scanner
        bool    uacOn      = false;   // a polkit authority is on the bus
    };

    static AcInfo gatherInfo();

    // Builds one collapsible section (Security / Maintenance). `rows` is the
    // pre-populated body; the returned frame carries the clickable blue header
    // with the chevron that shows/hides that body.
    QWidget *buildSection(const QString &title, bool expanded, QWidget *rows);

    // A single "item, state, description" row inside a section body. `link`,
    // when non-empty, adds a blue task link under the description.
    QWidget *buildStatusRow(const QString &item,
                            const QString &state,
                            const QString &description,
                            const QString &link = QString());

    // The yellow-accent "action needed" alert (the Set up backup box), with a
    // right-aligned push button and a blue link beneath.
    QWidget *buildAlertBox(const QString &title,
                           const QString &description,
                           const QString &buttonText,
                           const QString &link);

    // The bottom "If you don't see your problem listed" icon + text pair.
    QWidget *buildBottomTask(std::initializer_list<const char *> iconNames,
                             const QString &title,
                             const QString &description);
};
