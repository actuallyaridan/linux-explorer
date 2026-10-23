#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "PageId.h"
#include <QList>
#include <QHash>

class QScrollArea;
class QTreeWidget;
class QLabel;

// The "Installed Updates" detail page, a Linux-flavoured take on the Windows 7
// "Programs and Features > Installed Updates" (Uninstall an update) screen.
//
// Real "installed updates" on Arch are the package upgrades recorded in
// /var/log/pacman.log. Each `upgraded foo (a -> b)` line is an update that was
// installed; we keep the most recent version per package and group the rows by
// the repository that ships the package (resolved once via `pacman -Sl`).
class InstalledUpdatesPage : public QWidget {
    Q_OBJECT

public:
    explicit InstalledUpdatesPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    // Left-nav entries shown by MainWindow's subpage sidebar.
    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

private:
    // One installed update: the latest recorded upgrade of a package.
    struct UpdateInfo {
        QString name;       // package name, e.g. "linux"
        QString version;    // version it was upgraded to, e.g. "6.7-1"
        QString repo;       // repository the package ships from, e.g. "extra"
        QString publisher;  // vendor-style label derived from the repo
        QString date;       // install date, e.g. "6/11/2026"
    };

    // Collect the latest upgrade per package from /var/log/pacman.log. Runs on a
    // worker thread (it shells out to pacman), so it must not touch the UI.
    static QList<UpdateInfo> gatherUpdates();
    // pkg -> repo map built from `pacman -Sl`; absent packages are foreign (AUR).
    static QHash<QString, QString> repositoryMap();

    // Fill the tree from gathered data, replacing the "Searching..." placeholder.
    void populate(const QList<UpdateInfo> &updates);

    QTreeWidget *m_tree     = nullptr;
    QLabel      *m_countLbl = nullptr;
};
