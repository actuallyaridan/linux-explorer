<div align="center">

  # linux-explorer
  <i>The Windows 7 Explorer, on Linux</i>

  <p>
    A faithful recreation of Windows 7's Explorer built with Qt6, KDE Frameworks, and <a href="https://gitgud.io/atmk/libaero-qt">libAeroQt</a>, with every file operation handled by <a href="https://invent.kde.org/frameworks/kio">KIO</a>. Best enjoyed with <a href="https://github.com/aeroshell-desktop/aerothemeplasma">AeroThemePlasma</a>.
  </p>

</div>
<br>

> [!WARNING]
> **Very much a work in progress.** <br>Most things are done, but there are still things that need work. Not really daily-driveable yet.

## Installation

### Arch / CachyOS (AUR)

```bash
yay -S linux-explorer
```

### Pre-built binaries

Download the latest binary from the [Releases](https://github.com/actuallyaridan/linux-explorer/releases/latest) page and place it somewhere in your `$PATH`. It still needs Qt6, KF6 and libAeroQt installed, same as building from source, but skips the compile:

```bash
chmod +x explorer-x86_64
sudo mv explorer-x86_64 /usr/local/bin/explorer
```

## Building

Built for CachyOS / Arch. Other distros should work given Qt6, KF6 and libAeroQt.

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
| `kio-extras` | Real search behind the search box, and archives that open as folders |
| `kio-admin` | "Open as Administrator", this desktop's version of the UAC prompt |
| `ark` | Send to → Compressed (zipped) folder |
| `ffmpegthumbs`, `kdegraphics-thumbnailers` | Thumbnails for video, PDF and RAW |
| `kdenetwork-filesharing` | Samba browsing behind Map network drive |

### Being the system's file manager

Explorer answers `org.freedesktop.FileManager1`, which is what every "Open Containing Folder" in the desktop calls. Dolphin ships a service file for the same name and usually gets there first, so hand it over for your user:

```bash
mkdir -p ~/.local/share/dbus-1/services && cp dbus/org.freedesktop.FileManager1.service ~/.local/share/dbus-1/services/
xdg-mime default linux-explorer.desktop inode/directory
```

## Why not a fork of Dolphin?

The hard part of a file manager is KIO: overwrite and conflict resolution, permissions, cross-device copies, restorable trash, cancellable progress and undo. Dolphin links against it directly, and so does this project. `src/model/FileOps.cpp` is a thin facade over KIO jobs, and nothing in this repository reads or writes file contents itself.

What's left is the Windows 7 look and feel, which is the part worth building from scratch rather than skinning on top of Dolphin's own UI.

### Keyboard

| Key | Does |
|-----|------|
| <kbd>Ctrl</kbd>+<kbd>L</kbd>, <kbd>Alt</kbd>+<kbd>D</kbd>, <kbd>F4</kbd> | Edit the address bar |
| <kbd>Ctrl</kbd>+<kbd>E</kbd>, <kbd>Ctrl</kbd>+<kbd>F</kbd>, <kbd>F3</kbd> | Search box |
| <kbd>Backspace</kbd>, <kbd>Alt</kbd>+<kbd>←</kbd> / <kbd>Alt</kbd>+<kbd>↑</kbd> | Back / up one level |
| <kbd>F2</kbd> / <kbd>F5</kbd> / <kbd>F11</kbd> | Rename / refresh / full screen |
| <kbd>F6</kbd> / <kbd>Shift</kbd>+<kbd>F6</kbd> | Cycle panes |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>1</kbd>..<kbd>8</kbd> | View modes, or <kbd>Ctrl</kbd>+wheel |
| <kbd>Alt</kbd>+<kbd>P</kbd> / <kbd>Alt</kbd>+<kbd>Enter</kbd> | Preview pane / properties |

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
