#include "MenuEntries.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace MenuEntries {

namespace {

// The installed file names, which are also the names the desktop knows the
// entries by, so a copy under the same name replaces the installed one
const QString kExplorerEntry = QStringLiteral("linux-explorer.desktop");
const QString kControlPanelEntry = QStringLiteral("linux-controlpanel.desktop");

// Written into every copy made here, so only those are ever removed and a copy
// the user made with the desktop's menu editor is left alone
const QByteArray kMarker = QByteArrayLiteral("X-Explorer-HiddenByMode=true");

QString copyPath(const QString &entry)
{
    const QString folder =
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    return folder + QLatin1Char('/') + entry;
}

bool isOurCopy(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.readAll().contains(kMarker);
}

// True when the file on disk changed
bool setHidden(const QString &entry, const QString &name, bool hidden)
{
    const QString path = copyPath(entry);
    const bool exists = QFile::exists(path);

    if (!hidden) {
        if (exists && isOurCopy(path)) {
            QFile::remove(path);
            return true;
        }
        return false;
    }

    // Already hidden by an earlier run, or the user's own copy, which is
    // theirs to keep even though it leaves the entry showing
    if (exists)
        return false;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    // Only what the format requires, so the copy never goes stale when an
    // update changes the installed entry
    file.write("[Desktop Entry]\n");
    file.write("Type=Application\n");
    file.write("Name=" + name.toUtf8() + "\n");
    file.write("Hidden=true\n");
    file.write(kMarker + "\n");
    return true;
}

} // namespace

void apply(Settings::AppMode mode)
{
    const bool hideExplorer = mode == Settings::AppMode::ControlPanelOnly;
    const bool hideControlPanel = mode == Settings::AppMode::ExplorerOnly;

    bool changed = false;
    if (setHidden(kExplorerEntry, QStringLiteral("File Explorer"), hideExplorer))
        changed = true;
    if (setHidden(kControlPanelEntry, QStringLiteral("Control Panel"), hideControlPanel))
        changed = true;

    if (!changed)
        return;

    // The start menu reads a cache of every entry, which this rebuilds, and
    // without it the change only shows after the next login
    const QString rebuild = QStandardPaths::findExecutable(QStringLiteral("kbuildsycoca6"));
    if (!rebuild.isEmpty())
        QProcess::startDetached(rebuild, {});
}

} // namespace MenuEntries
