#pragma once

// Windows friendly folder naming, where only the displayed name changes and
// every URL, KIO path and history entry stays canonical

#include <QString>
#include <QUrl>

namespace Branding {

// The master switch, and with it off every folder shows its real name
bool windowsFriendlyMode();
void setWindowsFriendlyMode(bool on);

// The system config folder is named after the OS rather than given a fixed
// translation, and this is only meaningful in friendly mode
bool useWindowsNames();
void setUseWindowsNames(bool on);

// Empty when the mode is off or the path has no counterpart, and callers then
// fall back to the real name
QString folderName(const QString &absolutePath);

// True for a top level directory with no Windows counterpart, which the mode
// keeps out of the root listing, so a distribution with a top level directory
// of its own needs no special case, and showing hidden files brings it back
bool isSystemFolder(const QString &absolutePath);

QString displayName(const QUrl &url, const QString &realName);

// Drives are absent, a volume label being the user's own and shown as it is

} // namespace Branding
