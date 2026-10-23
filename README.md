<div align="center">

  # linux-explorer
  <i>Windows 7's Explorer and Control Panel, on Linux</i>

  <p>
    This is Windows 7's Explorer rebuilt for Linux with Qt6 and KDE Frameworks. The Aero look comes from <a href="https://gitgud.io/atmk/libaero-qt">libAeroQt</a>, and all the actual file work (copying, moving, the trash and so on) is done by KDE's <a href="https://invent.kde.org/frameworks/kio">KIO</a>, the same thing Dolphin uses. The Control Panel is in here too, and just like on Windows 7 it opens inside an Explorer window. It looks best with <a href="https://github.com/aeroshell-desktop/aerothemeplasma">AeroThemePlasma</a>.
  </p>

</div>
<br>

> [!WARNING]
> **Still a work in progress.** Most of it works, but there are rough edges and it's not really ready to be your only file manager yet.

## Installing

On Arch or CachyOS it's on the AUR:

```bash
yay -S linux-explorer
```

If you'd rather not compile anything, grab the binary from the [Releases](https://github.com/actuallyaridan/linux-explorer/releases/latest) page. You still need Qt6, KF6 and libAeroQt installed for it to run.

```bash
chmod +x explorer-x86_64
sudo mv explorer-x86_64 /usr/local/bin/explorer
```

## Building it yourself

It's made on CachyOS, but any distro with Qt6, KF6 and libAeroQt should work.

```bash
sudo pacman -S qt6-base qt6-multimedia cmake kio kcoreaddons kwidgetsaddons kwindowsystem kio-extras libcups openssl zlib
cmake -B build
cmake --build build -j
./build/explorer
```

You'll also need libAeroQt. Build and install it from [libaero-qt](https://gitgud.io/atmk/libaero-qt) first.

### Optional dependencies

You don't need these, but some features won't work without them:

`kio-extras` for searching subfolders and opening zip files like folders\
`kio-admin` for Open as Administrator\
`ark` for Send to > Compressed (zipped) folder\
`ffmpegthumbs` and `kdegraphics-thumbnailers` for video, PDF and RAW thumbnails\
`kdenetwork-filesharing` for browsing Samba shares\
`polkit` for uninstalling programs from the Control Panel\
`yay` for reinstalling AUR programs from the Control Panel\
`ufw` for the Control Panel's Firewall page

Everything except yay is in the official repos:

```bash
sudo pacman -S --needed kio-extras kio-admin ark ffmpegthumbs kdegraphics-thumbnailers kdenetwork-filesharing polkit ufw
```

## The Control Panel

Click **Open Control Panel** in Computer, type `Control Panel` into the address bar, or use the Control Panel entry in your Start menu.

Most pages show real info from your system. Linux Update checks for and installs updates with pacman, Programs and Features lists your programs and can uninstall them, and the Performance page has its own version of the Windows Experience Index. A few pages are still placeholders.

> [!NOTE]
> Linux Update and Programs and Features use pacman, so they only work on Arch-based distros for now.

If you don't want the Control Panel, go to **Organize > Folder and search options** and pick **Explorer only** under **Programs**. That turns it off and takes it out of the Start menu. You can also go the other way with **Control Panel only**.

## Making it your default file manager

When another app wants to show a file in its folder, it asks whichever file manager owns `org.freedesktop.FileManager1`. That's usually Dolphin. To give it to Explorer instead:

```bash
mkdir -p ~/.local/share/dbus-1/services && cp dbus/org.freedesktop.FileManager1.service ~/.local/share/dbus-1/services/
xdg-mime default linux-explorer.desktop inode/directory
```

## Keyboard shortcuts

| Key | What it does |
|-----|--------------|
| <kbd>Ctrl</kbd>+<kbd>L</kbd>, <kbd>Alt</kbd>+<kbd>D</kbd>, <kbd>F4</kbd> | Edit the address bar |
| <kbd>Ctrl</kbd>+<kbd>E</kbd>, <kbd>Ctrl</kbd>+<kbd>F</kbd>, <kbd>F3</kbd> | Jump to the search box |
| <kbd>Backspace</kbd>, <kbd>Alt</kbd>+<kbd>Left</kbd> / <kbd>Alt</kbd>+<kbd>Up</kbd> | Back / up one folder |
| <kbd>F2</kbd> / <kbd>F5</kbd> / <kbd>F11</kbd> | Rename / refresh / full screen |
| <kbd>F6</kbd> / <kbd>Shift</kbd>+<kbd>F6</kbd> | Move between panes |
| <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>1</kbd>..<kbd>8</kbd> | Change the view, or <kbd>Ctrl</kbd>+scroll |
| <kbd>Alt</kbd>+<kbd>P</kbd> / <kbd>Alt</kbd>+<kbd>Enter</kbd> | Preview pane / properties |

## Command line

```bash
explorer ~/Documents          # open a folder
explorer --select ~/a.txt     # open the folder with a.txt selected
explorer --control-panel      # open the Control Panel
```

## Why not just fork Dolphin?

The hard part of a file manager is everything under the hood: copying between drives, handling conflicts and permissions, the trash, undo. KDE's KIO already does all of that well, and it's what Dolphin uses too. So this project uses KIO directly and puts its effort into the Windows 7 look and feel instead. `src/model/FileOps.cpp` is basically a thin wrapper around KIO, and nothing in here reads or writes your files by itself.

## The rest of the WSL series

WSL here stands for Windows-alike Software for Linux. Check out the others too:

- [Linux Device Manager](https://github.com/actuallyaridan/linux-devmgmt)
- [Linux Minesweeper](https://github.com/actuallyaridan/linux-minesweeper)
