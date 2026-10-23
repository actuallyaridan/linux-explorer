#pragma once

#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "MessageBoxes.h"

namespace ControlPanel {

// External launchers shared by MainWindow's task links and the detail pages'
// sidebar links. Task and sidebar links with no dedicated page in this app hand
// off to the real KDE module, the way the Windows Control Panel opens an applet

// Command for opening a KDE System Settings module, e.g. kcm("kcm_kscreen")
inline QStringList kcm(const char *module)
{
    return { QStringLiteral("kcmshell6"), QString::fromLatin1(module) };
}

// Desktop Gadgets links hook into KDE Plasma's real widget panels. The
// "Desktop Gadgets" heading and "Add gadgets to the desktop" link toggle
// Plasma's widget explorer (the "Add or Manage widgets" panel) over D-Bus;
// "Get more gadgets online" opens the Get-New-Widgets download dialog
inline const QStringList kWidgetExplorerCmd = {
    QStringLiteral("qdbus6"), QStringLiteral("org.kde.plasmashell"),
    QStringLiteral("/PlasmaShell"),
    QStringLiteral("org.kde.PlasmaShell.toggleWidgetExplorer")
};
inline const QStringList kGetWidgetsCmd = {
    QStringLiteral("knewstuff-dialog6"),
    QStringLiteral("/usr/share/knsrcfiles/plasmoids.knsrc")
};

// "Device Manager" links launch the standalone devmgmt program
inline const QStringList kDeviceManagerCmd = {
    QStringLiteral("devmgmt")
};

// Launch an external helper (a kcmshell6 module, kfontview, a settings applet…)
// detached. If the program isn't on PATH, tell the user rather than failing
// silently, the same courtesy MainWindow extends to its own command links
inline void launchDetached(QWidget *parent, const QStringList &cmd)
{
    if (cmd.isEmpty())
        return;
    QStringList args = cmd;
    const QString program = args.takeFirst();
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        MessageBoxes::showError(parent, QStringLiteral("Control Panel"),
            QStringLiteral("\"%1\" is not installed.").arg(program));
        return;
    }
    QProcess::startDetached(program, args);
}

} // namespace ControlPanel
