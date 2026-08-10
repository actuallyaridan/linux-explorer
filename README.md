<div align="center">

  # linux-explorer
  <i>The Windows 7 Explorer, on Linux</i>

  <p>
    A faithful recreation of Windows 7's Explorer built with Qt6, KDE Frameworks, and <a href="https://gitgud.io/atmk/libaero-qt">libAeroQt</a>, with every file operation handled by <a href="https://invent.kde.org/frameworks/kio">KIO</a>. Best enjoyed with <a href="https://github.com/aeroshell-desktop/aerothemeplasma">AeroThemePlasma</a>.
  </p>

</div>
<br>

> [!WARNING]
> **Very much a work in progress.** The window chrome, navigation and file operations are in place, but plenty is still missing or rough. Not daily-driveable yet.

> [!NOTE]
> **Built for CachyOS / Arch Linux.** Other distros should work given Qt6, KF6 and libAeroQt, but the build instructions below are Arch-flavoured.

## Building

### Arch / CachyOS

```bash
sudo pacman -S qt6-base qt6-multimedia cmake kio kcoreaddons kwidgetsaddons kwindowsystem kio-extras
cmake -B build
cmake --build build -j
./build/explorer
```

You'll also need `libAeroQt.so` on your system, built from [libaero-qt](https://gitgud.io/atmk/libaero-qt).

### Optional packages

Everything below is checked for at runtime. Without one, the feature it backs is hidden or falls back rather than failing.

| Package | What it gives you |
|---------|-------------------|
| `kio-extras` | `filenamesearch` behind the search box, and the archive workers that let you open a `.zip` as a folder. Without it, search falls back to filtering the current folder and archives open in whatever handles them. |
| `kio-admin` | "Open as Administrator",  this desktop's version of the UAC prompt, offered on folders you can't write to. |
| `ark` | Send to → Compressed (zipped) folder. Extracting needs nothing extra; that's a plain KIO copy. |
| `ffmpegthumbs`, `kdegraphics-thumbnailers` | Thumbnails for video, PDF and RAW. Without them those file types show a generic icon and it looks like a bug in the preview code. |
| `kdenetwork-filesharing` | Samba browsing behind Map network drive. |

### Being the system's file manager

Explorer answers `org.freedesktop.FileManager1`, which is what "Open Containing Folder" in Firefox, "Show in folder" in Ark, Okular, VLC and Steam, and KIO's own reveal all call. It claims that name at startup whenever it's free.

Dolphin ships a D-Bus service file for the same name, so on a KDE system Dolphin usually gets there first. To hand it to Explorer permanently, override that file for your user:

```bash
mkdir -p ~/.local/share/dbus-1/services && cp dbus/org.freedesktop.FileManager1.service ~/.local/share/dbus-1/services/
```

Then set Explorer as the handler for folders:

```bash
xdg-mime default linux-explorer.desktop inode/directory
```

## Why not a fork of Dolphin?

Because the part of a file manager that is genuinely hard to get right, and dangerous to get wrong, isn't Dolphin. It's KIO.

Overwrite and conflict resolution, symlink and permission handling, cross-device copies, trash semantics the desktop's Recycle Bin can actually restore from, cancellable progress reporting and undo all live in KIO, which is a KDE Framework any application can link. Dolphin calls into it for all of the above; so does this.

So `src/model/FileOps.cpp` is a thin facade over KIO jobs, and nothing in this repository reads or writes file contents itself. What's left to write is the Windows 7 chrome, which is the part worth writing by hand, and which a fork would have made permanently painful to keep in sync with upstream.


## Features

- Windows 7-style window chrome: navigation bar, breadcrumb address bar with per-segment dropdowns, command bar, details pane, and the classic menu bar on Alt
- All eight view modes (extra large through content) on Ctrl+Shift+1..8, remembered per folder
- Thumbnails and a preview pane (Alt+P), both from the desktop's own preview plugins
- Details view with a column chooser, sorting, and Win7's "Group by"
- Drag and drop, into folders, onto the navigation pane, and to and from other applications
- Navigation pane backed by the desktop's shared bookmarks, editable in place, expanding to follow the current folder
- Back / forward / up navigation with full history and a recent-locations dropdown
- Cut, copy, paste, in-place rename, trash, permanent delete, shortcuts and Send To, all through KIO
- Rename a whole selection at once, the way Win7's F2 does: one base name, a counter, each file's own extension kept
- Undo, via KIO's file undo manager
- Filter as you type; press Enter to search subfolders, then "Search again in: File Contents" or Computer to widen it
- `.zip`, `.tar.*`, `.7z` and `.ar` open as folders, with Extract All and Send to → Compressed (zipped) folder
- Map network drive, for SMB, SFTP, FTP, WebDAV and NFS shares, in UNC (`\\server\share`) or URL form
- Open as Administrator on folders you can't write to, through `kio-admin` and polkit
- Spring-loaded folders: rest a drag on a folder and it opens
- An Options dialog laid out like Win7's Folder Options (General / View / Search), covering browse-in-new-window, single vs double click, hidden files, extensions, selection check boxes, Apply to Folders, search scope, and the Windows-friendly naming switches
- Ctrl+wheel to resize icons, F11 for full screen, F6 to cycle panes
- Answers `org.freedesktop.FileManager1`, so the rest of the desktop's "show in folder" opens Explorer with the file already selected
- One instance per session: a second launch opens in the running Explorer instead of a new process
- Window size, pane widths, columns and view settings all remembered between runs

### Keyboard

| Key | Does |
|-----|------|
| <kbd>Ctrl</kbd>+<kbd>L</kbd>, <kbd>Alt</kbd>+<kbd>D</kbd> | Edit the address bar |
| <kbd>F4</kbd> | Edit the address bar and drop its history |
| <kbd>Ctrl</kbd>+<kbd>E</kbd>, <kbd>Ctrl</kbd>+<kbd>F</kbd>, <kbd>F3</kbd> | Search box |
| <kbd>Backspace</kbd>, <kbd>Alt</kbd>+<kbd>←</kbd> | Back |
| <kbd>Alt</kbd>+<kbd>↑</kbd> | Up one level |
| <kbd>F6</kbd> / <kbd>Shift</kbd>+<kbd>F6</kbd> | Cycle panes |
| <kbd>F2</kbd> | Rename, one file or a whole selection |
| <kbd>F5</kbd> | Refresh |
| <kbd>F11</kbd> | Full screen |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>1</kbd>..<kbd>8</kbd> | View modes, or <kbd>Ctrl</kbd>+wheel |
| <kbd>Alt</kbd>+<kbd>P</kbd> | Preview pane |
| <kbd>Alt</kbd>+<kbd>Enter</kbd> | Properties |

### Command line

```bash
explorer ~/Documents          # open a folder
explorer --select ~/a.txt     # open its folder with the file selected
```

## Part of the WSL (Windows-alike Software for Linux) series

Why don't you also check out the other ones?

- [Linux Device Manager](https://github.com/actuallyaridan/linux-devmgmt)
- [Linux Control Panel](https://github.com/actuallyaridan/linux-control)
- [Linux Minesweeper](https://github.com/actuallyaridan/linux-minesweeper)
