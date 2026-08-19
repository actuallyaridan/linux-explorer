#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// How the rest of the desktop asks a file manager to reveal a file, and the
// single instance lock that comes with owning a bus name
//
// Two names are claimed, Explorer's own, which decides which process is
// primary, and the shared one, best effort, so a rival file manager holding the
// shared name cannot swallow Explorer's launches
class FileManagerService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManagerService(QObject *parent = nullptr);

    // False when another Explorer already holds the private name, in which case
    // nothing was registered and the caller should forward rather than open
    bool claim();

    bool ownsSharedName() const { return m_ownsSharedName; }

    // Hands the locations to the Explorer already running
    static bool forward(const QStringList &uris, bool reveal);

    // The activation token this process was launched with, so a window opened
    // for another application may take the focus
    static QString startupId();

public Q_SLOTS:
    // CamelCase because these are wire names fixed by the specification
    void ShowFolders(const QStringList &uriList, const QString &startupId);
    void ShowItems(const QStringList &uriList, const QString &startupId);
    void ShowItemProperties(const QStringList &uriList, const QString &startupId);

private:
    bool m_ownsSharedName = false;
};
