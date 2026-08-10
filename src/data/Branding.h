#pragma once

// Windows-friendly folder naming.
//
// The Control Panel's Branding module substitutes words; this one substitutes
// whole directories, showing /home as "Users" and /usr as "Program Files" so
// somebody who knows Windows can find their way around. Persisted the same
// way, under [Branding] in the app's default QSettings.
//
// Only what is *displayed* changes: every URL, path handed to KIO and history
// entry stays canonical, so navigation, file operations and the path editor
// are unaffected, and typing the real path always works.
//
// The mapping is one table in the .cpp, kept small and obvious. Some entries
// are approximations, the two systems not partitioning a filesystem the same
// way, and a few directories (/proc, /sys) have no counterpart and are left
// alone.

#include <QString>
#include <QUrl>

namespace Branding {

// The master switch; with it off every folder shows its real name.
bool windowsFriendlyMode();
void setWindowsFriendlyMode(bool on);

// Whether the operating system's own folder claims to be Windows.
//
// /etc is the one directory whose label is not a fixed translation: it holds
// the OS's configuration, so it is named after the OS. Off it reads "Linux",
// on it reads "Windows". Meaningless unless windowsFriendlyMode() is on, and
// the menu greys it out in that case.
bool useWindowsNames();
void setUseWindowsNames(bool on);

// The Windows-style name for an absolute local path, or an empty string when
// the mode is off or the path has no counterpart. Callers fall back to the
// real name in that case.
QString folderName(const QString &absolutePath);

// True for a top-level directory with no Windows counterpart, which the mode
// keeps out of the root listing.
//
// The rule is "unmapped, therefore not part of C:\" rather than a list of known
// offenders, so a distribution with its own top-level directory needs no
// special case. That covers the usr-merge symlinks (/bin, /sbin, /lib), the
// kernel pseudo-filesystems (/proc, /sys, /dev, /run) and the odds and ends
// (/srv, /mnt, /root).
//
// Top level only, and everything reappears with "Show hidden files" on.
bool isSystemFolder(const QString &absolutePath);

// The name to display for `url`, falling back to `realName`.
QString displayName(const QUrl &url, const QString &realName);

// Drives are deliberately absent: a volume label is something the user set
// themselves and is shown as-is. If that means the trail reads "Computer >
// Linux > Linux" because the root partition is labelled Linux, that is correct.

} // namespace Branding
