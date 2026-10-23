<div align="center">

  # linux-control
  <i>The Windows Control Panel, on Linux</i>

  <p>
    A faithful recreation of the Windows Control Panel built with Qt6, KDE Frameworks, and <a href="https://gitgud.io/atmk/libaero-qt">libAeroQt</a>, backed by real system data via <code>pacman</code>/<code>pkexec</code>/<code>ufw</code>. Best enjoyed with <a href="https://github.com/aeroshell-desktop/aerothemeplasma">AeroThemePlasma</a>.
  </p>

</div>
<br>

> [!WARNING]
> **Very much a work in progress.** Several features are placeholders or don't do anything yet, the code is janky and buggy in places, and it needs a lot more work before it's daily-driveable. Expect rough edges until about August.

> [!NOTE]
> **Built for CachyOS / Arch Linux.** Some features (package updates, AUR reinstall, driver-adjacent firewall config) depend on `pacman`, `yay`, and `ufw`. Other distros may need minor adjustments.

## Installation

### Arch / CachyOS (AUR)

```bash
yay -S linux-control
```

### Pre-built binaries

Download the latest archive for your architecture from the [Releases](https://github.com/actuallyaridan/linux-control/releases/latest) page and extract it somewhere in your `$PATH`:

```bash
tar -xf linux-control-x86_64.tar.gz
sudo mv control libAeroQt.so* /usr/local/bin/
```

`libAeroQt.so` is a separate runtime dependency (not statically linked), so it needs to ship alongside `control`.

## Building

### Arch / CachyOS

```bash
sudo pacman -S qt6-base qt6-multimedia cmake kwidgetsaddons kwindowsystem openssl zlib
cmake -B build
cmake --build build -j
./build/control
```

You'll also need `libAeroQt.so` on your system, built from [libaero-qt](https://gitgud.io/atmk/libaero-qt). Place the resulting `libAeroQt.so*` next to `build/control` (or install it to your library path).

## Runtime dependencies

On Arch, you need these installed separately.

| Tool | Purpose |
|------|---------|
| `pkexec` | Privilege escalation for package installs/uninstalls |
| `pacman` | Update checks, installed-package listing, program uninstall |
| `yay` | *(optional)* Reinstalling AUR/foreign packages from Programs & Features |
| `ufw` | Firewall status/rules shown on the Firewall page (read-only) |

## Features

- Windows 7-style Control Panel home screen, organized by category
- Windows Update page backed by real `pacman` update checks
- Installed Updates history from `pacman.log`
- Programs and Features: list, uninstall, and reinstall (AUR-aware) installed packages
- Network and Sharing Center overview
- Firewall status and rule summary (reads `ufw` configuration)
- Power Options
- Performance page with a Windows Experience Index-style benchmark

## Part of the WSL (Windows-alike Software for Linux) series

Why don't you also check out the other ones?

- [Linux Device Manager](https://github.com/actuallyaridan/linux-devmgmt)
- Linux Control Panel
