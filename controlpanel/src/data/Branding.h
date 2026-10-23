#pragma once

// Central branding configuration for the Control Panel.
//
// Two orthogonal, persisted concerns (stored under the app's default QSettings
// in the [Branding] group; org/app are set to "controlpanel" in main.cpp):
//
//  1. os()/brand(), word substitution. When "Use Windows names" is on, every
//     user-facing "Linux X" mention is displayed as "Windows X". The rest of
//     the app keeps its canonical "Linux ..." strings (routing keys, data
//     tables); only what is *shown* is rewritten, at widget-build time. Because
//     every page/home/category view is rebuilt on navigation, toggling the
//     setting and re-navigating refreshes the UI with no caching to invalidate.
//
//  2. The System-page edition badge, the branding style picks the distro's
//     real identity or a fake "Windows 7" one (name, logo, copyright). Choosing
//     the Windows 7 style automatically shows the fake edition; there is no
//     separate "fake version" toggle.
//
// Defaults reproduce today's behaviour exactly: real distro, "Linux" wording.

#include <QString>
#include <QIcon>
#include <QPixmap>

namespace Branding {

enum class Style {
    Distro = 0,     // the real distribution (PRETTY_NAME / NAME)
    Windows7 = 1,   // fake "Windows 7" branding
};

// ---- Persisted settings ---------------------------------------------------

Style style();               void setStyle(Style s);
bool  useWindowsNames();     void setUseWindowsNames(bool on);

// ---- Word substitution ----------------------------------------------------

// "Windows" when the rename switch is on, otherwise "Linux".
QString os();

// canonical -> display: replaces the standalone word "Linux" with os(). A word
// boundary is used, so "linux-control", file paths and identifiers are safe.
QString brand(const QString &canonical);

// display -> canonical: the inverse, replacing the standalone os() word with
// "Linux". A no-op when os() is already "Linux".
QString debrand(const QString &shown);

// ---- Edition badge --------------------------------------------------------

// The badge title: "Windows 7 Ultimate" with the fake Windows 7 style active,
// otherwise the distro's real PRETTY_NAME.
QString editionName(const QString &realPretty);

// Badge copyright holder: "Microsoft Corporation" for the fake Windows badge,
// otherwise the distro's developer org (`distroVendor`, resolved by the caller
// from os-release, e.g. "Fedora Project" / "CachyOS").
QString copyrightHolder(const QString &distroVendor);

// Badge copyright year: 2009 (Windows 7's release) for the fake Windows badge;
// otherwise the year the running kernel was built (parsed from /proc/version),
// falling back to the current year.
int copyrightYear();

// True when the fake Windows 7 badge (logo + edition text) should be shown,
// i.e. the branding style is Windows 7.
bool fakeBadgeActive();

// The Windows 7 logo as a QIcon, from the theme's "distributor-logo". Suitable
// for small chrome (e.g. the settings-dialog window icon), where the theme's
// orb-backed <=48px rasters are used directly.
QIcon windowsLogo();

// The glossy Windows 7 orb at `px`, scaled from the high-resolution branding
// image AeroThemePlasma's "About this System" uses (kcm-about-distrorc's
// LogoPath, typically /usr/share/aerothemeplasma/branding/kcminfo.png). Falls
// back to the icon theme's small (<=48px) orb raster if that isn't installed.
QPixmap windowsOrb(int px);

} // namespace Branding
