#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// The desktop's file-manager D-Bus interface, org.freedesktop.FileManager1,
// and the single-instance lock that comes with owning a bus name.
//
// This is how the rest of the system asks a file manager to do something.
// "Open Containing Folder" in Firefox and "Show in folder" in Ark, Okular, VLC,
// Steam and Discord all call ShowItems here rather than launching a binary,
// there being no command-line convention for opening a folder *with the file
// highlighted*. An Explorer that does not answer here is one half the desktop
// cannot open, whatever the user set as their default file manager.
//
// Two names are claimed:
//
//   - kAppService is Explorer's own and nothing else ever holds it. Claiming it
//     is what makes this process the primary instance; a second launch finds it
//     taken, hands its arguments over and exits. Separate from FileManager1
//     deliberately: were they the same, a running Dolphin holding FileManager1
//     would make every Explorer launch forward its arguments to Dolphin.
//
//   - FileManager1 is the shared, well-known name, claimed best-effort. Only
//     one process on the session bus can hold it. See the README for making
//     Explorer the session's file manager permanently.
class FileManagerService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManagerService(QObject *parent = nullptr);

    // Claims Explorer's own name and exports this object on it, then claims the
    // shared FileManager1 name if it is free. False when another Explorer
    // already holds the private name: nothing was registered, and the caller
    // should forward() rather than open windows.
    bool claim();

    // Whether the shared FileManager1 name was claimed too, which decides
    // whether the desktop's "show in folder" reaches Explorer.
    bool ownsSharedName() const { return m_ownsSharedName; }

    // Hands `uris` to the Explorer already running, returning true if it
    // accepted them. `reveal` picks ShowItems over ShowFolders.
    static bool forward(const QStringList &uris, bool reveal);

    // The activation token this process was launched with, so a window opened
    // for another application may take the focus. Both Wayland and X11 leave it
    // in the environment.
    static QString startupId();

public Q_SLOTS:
    // CamelCase because these are wire names fixed by the specification.
    void ShowFolders(const QStringList &uriList, const QString &startupId);
    void ShowItems(const QStringList &uriList, const QString &startupId);
    void ShowItemProperties(const QStringList &uriList, const QString &startupId);

private:
    bool m_ownsSharedName = false;
};
