#pragma once

#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// External launchers shared by MainWindow's task links and the detail pages'
// sidebar links. Task and sidebar links with no dedicated page in this app hand
// off to the real KDE module, the way the Windows Control Panel opens an applet.
// ---------------------------------------------------------------------------

// Command for opening a KDE System Settings module, e.g. kcm("kcm_kscreen").
inline QStringList kcm(const char *module)
{
    return { QStringLiteral("kcmshell6"), QString::fromLatin1(module) };
}

// Desktop Gadgets links hook into KDE Plasma's real widget panels. The
// "Desktop Gadgets" heading and "Add gadgets to the desktop" link toggle
// Plasma's widget explorer (the "Add or Manage widgets" panel) over D-Bus;
// "Get more gadgets online" opens the Get-New-Widgets download dialog.
inline const QStringList kWidgetExplorerCmd = {
    QStringLiteral("qdbus6"), QStringLiteral("org.kde.plasmashell"),
    QStringLiteral("/PlasmaShell"),
    QStringLiteral("org.kde.PlasmaShell.toggleWidgetExplorer")
};
inline const QStringList kGetWidgetsCmd = {
    QStringLiteral("knewstuff-dialog6"),
    QStringLiteral("/usr/share/knsrcfiles/plasmoids.knsrc")
};

// "Device Manager" links launch the standalone devmgmt program.
inline const QStringList kDeviceManagerCmd = {
    QStringLiteral("devmgmt")
};
