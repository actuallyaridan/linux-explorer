#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "ui/control/PageId.h"
#include <QList>
#include <QDate>

class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

class DetailsPane;

namespace ControlPanel {

// The "Programs and Features" detail page, a Linux-flavoured take on the
// Windows 7 "Uninstall or change a program" screen
//
// The "installed programs" are the explicitly-installed packages reported by
// `pacman -Qei`; each block gives the name, version, install date and
// installed size, which map onto the Name / Version / Installed On / Size
// columns. The Publisher column shows the software's actual developer
// (AppStream data) or, for AUR packages, its AUR maintainer, see
// applyPublishers()
class ProgramsFeaturesPage : public QWidget {
    Q_OBJECT

public:
    explicit ProgramsFeaturesPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

private:
    // One installed program (explicitly-installed package)
    struct ProgramInfo {
        // Package name, e.g. "7zip"
        QString name;
        // Friendly name from its .desktop file, else name
        QString display;
        // .desktop Icon= (theme name or path); may be empty
        QString iconName;
        // Version, e.g. "26.01-1.1"
        QString version;
        // Developer/maintainer, e.g. "Valve Corporation"
        QString publisher;
        // Install date
        QDate   installed;
        // Installed size in bytes
        qint64  sizeBytes = 0;
    };

    // Collect explicitly-installed packages from `pacman -Qei`. Runs on a worker
    // thread (it shells out to pacman), so it must not touch the UI
    static QList<ProgramInfo> gatherPrograms();

    // Replace each program's display name (and icon) with the friendly Name/Icon
    // from its .desktop file, where it has one. Worker-thread only
    static void applyFriendlyNames(QList<ProgramInfo> &programs);

    // Replace the Packager-derived publisher with the upstream developer from
    // the AppStream catalog (repo packages) or the AUR maintainer from the AUR
    // RPC (foreign packages), keeping the fallback where neither is known.
    // Worker-thread only (shells out to pacman, gzip and curl)
    static void applyPublishers(QList<ProgramInfo> &programs);

    // Show the "Searching..." placeholder and (re)launch the worker-thread load
    void startLoad();
    // Fill the tree from gathered data, replacing the "Searching..." placeholder
    void populate(const QList<ProgramInfo> &programs);

    // Double-clicking a program opens a Windows-style Uninstall/Reinstall/Cancel
    // dialog; the chosen action shells out to pacman/yay
    void onProgramActivated(QTreeWidgetItem *item, int column);
    void runPackageAction(const QString &pkg, const QString &display, bool reinstall);

    QTreeWidget *m_tree     = nullptr;
    DetailsPane *m_details = nullptr;
};

} // namespace ControlPanel
